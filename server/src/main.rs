use std::collections::HashMap;
use std::net::SocketAddr;
use std::time::{Duration, Instant, SystemTime, UNIX_EPOCH};

use anyhow::Result;
use bevy_ecs::prelude::*;
use flatbuffers::FlatBufferBuilder;
use skyrim_relive_server::components::{AnimState, Connection, Player, Transform, Velocity};
use skyrim_relive_server::config::Config;
use skyrim_relive_server::proto::v1::{
    Disconnect, DisconnectArgs, DisconnectCode, Hello, MessageType, PlayerInput,
    PlayerState as FbPlayerState, PlayerStateArgs as FbPlayerStateArgs, Transform as FbTransform,
    Vec3 as FbVec3, Welcome, WelcomeArgs, WorldSnapshot, WorldSnapshotArgs,
};
use skyrim_relive_server::wire;
use tokio::net::UdpSocket;
use tokio::time::interval;
use tracing::{info, warn};

struct ServerState {
    config: Config,
    world: World,
    socket: UdpSocket,
    /// Fast peer→entity lookup so we don't scan the world on every packet.
    addr_to_entity: HashMap<SocketAddr, Entity>,
    next_player_id: u32,
    server_tick: u64,
    server_start: Instant,
    /// Seconds per sim tick, precomputed from `config.tick_rate_hz`.
    sim_dt: f32,
    /// Reusable builder for snapshot broadcasts — avoids one allocation per
    /// connected peer per snapshot tick (~20 Hz).
    snap_fbb: FlatBufferBuilder<'static>,
}

impl ServerState {
    fn new(config: Config, socket: UdpSocket) -> Self {
        let sim_dt = 1.0 / f32::from(config.tick_rate_hz);
        Self {
            config,
            world: World::new(),
            socket,
            addr_to_entity: HashMap::new(),
            next_player_id: 1,
            server_tick: 0,
            server_start: Instant::now(),
            sim_dt,
            snap_fbb: FlatBufferBuilder::with_capacity(256),
        }
    }

    async fn run(&mut self) -> Result<()> {
        let mut buf = vec![0u8; 2048];
        let sim_period = Duration::from_micros(1_000_000 / u64::from(self.config.tick_rate_hz));
        let snap_period =
            Duration::from_micros(1_000_000 / u64::from(self.config.snapshot_rate_hz));
        let gc_period = Duration::from_millis(self.config.gc_interval_ms);

        let mut sim_tick = interval(sim_period);
        let mut snap_tick = interval(snap_period);
        let mut gc_tick = interval(gc_period);
        for t in [&mut sim_tick, &mut snap_tick, &mut gc_tick] {
            t.set_missed_tick_behavior(tokio::time::MissedTickBehavior::Delay);
        }

        loop {
            tokio::select! {
                recv = self.socket.recv_from(&mut buf) => {
                    match recv {
                        Ok((len, peer)) => self.handle_packet(peer, &buf[..len]).await,
                        // ECONNRESET on UDP is the kernel telling us our last
                        // send_to elicited an ICMP "port unreachable" — i.e.
                        // the client we tried to Disconnect already exited.
                        // Expected after a timeout, not a real error.
                        Err(e) if e.raw_os_error() == Some(10054) => {}
                        Err(e) => warn!(error = %e, "recv_from failed"),
                    }
                }
                _ = sim_tick.tick() => self.advance_sim(),
                _ = snap_tick.tick() => self.broadcast_snapshot().await,
                _ = gc_tick.tick() => self.expire_stale().await,
            }
        }
    }

    async fn handle_packet(&mut self, peer: SocketAddr, packet: &[u8]) {
        let (mt, body) = match wire::parse(packet) {
            Ok(x) => x,
            Err(wire::DecodeError::VersionMismatch(v)) => {
                warn!(%peer, got = v, "version mismatch; sending Disconnect");
                send_disconnect(
                    &self.socket,
                    peer,
                    DisconnectCode::VersionMismatch,
                    &format!("server speaks v{}", wire::PROTOCOL_VERSION),
                )
                .await;
                return;
            }
            Err(e) => {
                warn!(%peer, error = %e, "packet rejected");
                return;
            }
        };

        match mt {
            MessageType::Hello => self.handle_hello(peer, body).await,
            MessageType::Heartbeat => self.handle_heartbeat(peer),
            MessageType::LeaveNotify => self.handle_leave(peer),
            MessageType::PlayerInput => self.handle_player_input(peer, body),
            MessageType::Welcome | MessageType::Disconnect | MessageType::WorldSnapshot => {
                warn!(%peer, ?mt, "client sent server-only message; ignoring");
            }
            _ => warn!(%peer, ?mt, "unhandled message type"),
        }
    }

    async fn handle_hello(&mut self, peer: SocketAddr, body: &[u8]) {
        let hello = match flatbuffers::root::<Hello<'_>>(body) {
            Ok(h) => h,
            Err(e) => {
                warn!(%peer, error = %e, "bad Hello payload");
                return;
            }
        };
        let name = hello.name().unwrap_or("(anonymous)").to_owned();

        // Reconnect from the same addr → drop the old entity first so we
        // don't end up with ghost duplicates.
        if let Some(prev) = self.addr_to_entity.remove(&peer) {
            self.world.despawn(prev);
            info!(%peer, "replacing previous connection from same addr");
        }

        let pid = self.next_player_id;
        self.next_player_id += 1;
        let entity = self
            .world
            .spawn((
                Player {
                    id: pid,
                    name: name.clone(),
                },
                Connection {
                    addr: peer,
                    last_heard: Instant::now(),
                },
                Transform::default(),
                Velocity::default(),
                AnimState::default(),
            ))
            .id();
        self.addr_to_entity.insert(peer, entity);

        info!(
            %peer,
            %name,
            player_id = pid,
            entity = ?entity,
            connected = self.addr_to_entity.len(),
            "Hello accepted, entity spawned"
        );

        let mut fbb = FlatBufferBuilder::with_capacity(128);
        let your_addr = fbb.create_string(&peer.to_string());
        let welcome = Welcome::create(
            &mut fbb,
            &WelcomeArgs {
                player_id: pid,
                server_tick_rate_hz: self.config.tick_rate_hz,
                server_snapshot_rate_hz: self.config.snapshot_rate_hz,
                your_addr: Some(your_addr),
            },
        );
        fbb.finish(welcome, None);
        let packet = wire::encode(MessageType::Welcome, fbb.finished_data());

        if let Err(e) = self.socket.send_to(&packet, peer).await {
            warn!(%peer, error = %e, "Welcome send failed");
        }
    }

    /// Accept a client's reported transform and write it into the ECS.
    /// Phase 1 trusts the client for the local player's transform (known
    /// H1 caveat in proposal 0001); server-side plausibility checks land
    /// in Phase 2 alongside combat authority.
    fn handle_player_input(&mut self, peer: SocketAddr, body: &[u8]) {
        let Some(&entity) = self.addr_to_entity.get(&peer) else {
            warn!(%peer, "PlayerInput from unknown peer (no Hello?)");
            return;
        };
        let input = match flatbuffers::root::<PlayerInput<'_>>(body) {
            Ok(i) => i,
            Err(e) => {
                warn!(%peer, error = %e, "bad PlayerInput payload");
                return;
            }
        };
        let Some(t) = input.transform() else {
            warn!(%peer, "PlayerInput missing transform");
            return;
        };
        let pos = t.pos();
        let new_pos = [pos.x(), pos.y(), pos.z()];
        let new_yaw = t.yaw();

        // Only write the transform when it actually changed — standing-still
        // clients still send PlayerInput as a heartbeat, but we can skip the
        // ECS mutation (and the resulting change-detection churn).
        if let Some(mut xf) = self.world.get_mut::<Transform>(entity) {
            const EPS: f32 = 1e-4;
            let same = (xf.pos[0] - new_pos[0]).abs() < EPS
                && (xf.pos[1] - new_pos[1]).abs() < EPS
                && (xf.pos[2] - new_pos[2]).abs() < EPS
                && (xf.yaw - new_yaw).abs() < EPS;
            if !same {
                xf.pos = new_pos;
                xf.yaw = new_yaw;
            }
        }
        // Phase 2.1/2.2: copy anim variables from the input.
        if let Some(mut anim) = self.world.get_mut::<AnimState>(entity) {
            anim.speed = input.anim_speed();
            anim.direction = input.anim_direction();
            anim.is_running = input.anim_is_running();
            anim.is_sprinting = input.anim_is_sprinting();
            anim.is_sneaking = input.anim_is_sneaking();
            anim.is_equipping = input.anim_is_equipping();
            anim.is_unequipping = input.anim_is_unequipping();
            anim.weapon_state = input.anim_weapon_state();
            anim.weapon_drawn = input.weapon_drawn();
        }
        // Touch last_heard regardless — acts as heartbeat even when idle.
        if let Some(mut conn) = self.world.get_mut::<Connection>(entity) {
            conn.last_heard = Instant::now();
        }
    }

    fn handle_heartbeat(&mut self, peer: SocketAddr) {
        if !self.touch_connection(peer) {
            warn!(%peer, "Heartbeat from unknown peer (no Hello?)");
        }
    }

    fn handle_leave(&mut self, peer: SocketAddr) {
        let Some(entity) = self.addr_to_entity.remove(&peer) else {
            warn!(%peer, "LeaveNotify from unknown peer");
            return;
        };
        let pid = self.world.get::<Player>(entity).map_or(0, |p| p.id);
        self.world.despawn(entity);
        info!(
            %peer,
            player_id = pid,
            entity = ?entity,
            connected = self.addr_to_entity.len(),
            "LeaveNotify accepted, entity despawned"
        );
    }

    /// Update `Connection.last_heard` for a known peer. Returns true if the
    /// peer was known.
    fn touch_connection(&mut self, peer: SocketAddr) -> bool {
        let Some(&entity) = self.addr_to_entity.get(&peer) else {
            return false;
        };
        if let Some(mut conn) = self.world.get_mut::<Connection>(entity) {
            conn.last_heard = Instant::now();
        }
        true
    }

    /// Step the world forward one server tick. Trivial in Phase 1: just
    /// integrate `Transform` from `Velocity`. `Velocity` stays zero until
    /// step 4 wires `PlayerInput` into the ECS.
    fn advance_sim(&mut self) {
        self.server_tick = self.server_tick.wrapping_add(1);
        let mut q = self.world.query::<(&mut Transform, &Velocity)>();
        for (mut t, v) in q.iter_mut(&mut self.world) {
            t.pos[0] += v.v[0] * self.sim_dt;
            t.pos[1] += v.v[1] * self.sim_dt;
            t.pos[2] += v.v[2] * self.sim_dt;
        }
    }

    /// For each connected client, send a `WorldSnapshot` containing every
    /// *other* connected player's transform. `AoI` culling is a no-op in
    /// Phase 1 (one cell, everyone visible to everyone) — replace this with
    /// a grid lookup in Phase 3.
    async fn broadcast_snapshot(&mut self) {
        // Read phase: snapshot the whole world before any await.
        let all: Vec<(u32, Transform, AnimState, SocketAddr)> = self
            .world
            .query::<(&Player, &Transform, &AnimState, &Connection)>()
            .iter(&self.world)
            .map(|(p, t, a, c)| (p.id, *t, *a, c.addr))
            .collect();

        if all.len() < 2 {
            // Nothing to broadcast — solo player has no peers to mirror.
            return;
        }

        let now_ms = u64::try_from(
            SystemTime::now()
                .duration_since(UNIX_EPOCH)
                .map(|d| d.as_millis())
                .unwrap_or(0),
        )
        .unwrap_or(0);

        for (self_pid, _, _, self_addr) in &all {
            self.snap_fbb.reset();

            // PlayerState is a v2 table now, so we collect WIPOffsets
            // first and then build the vector.
            let player_offsets: Vec<_> = all
                .iter()
                .filter(|(pid, _, _, _)| pid != self_pid)
                .map(|(pid, t, a, _)| {
                    let v3 = FbVec3::new(t.pos[0], t.pos[1], t.pos[2]);
                    let xform = FbTransform::new(&v3, t.yaw);
                    FbPlayerState::create(
                        &mut self.snap_fbb,
                        &FbPlayerStateArgs {
                            player_id: *pid,
                            transform: Some(&xform),
                            anim_speed: a.speed,
                            anim_direction: a.direction,
                            anim_is_running: a.is_running,
                            anim_is_sprinting: a.is_sprinting,
                            anim_is_sneaking: a.is_sneaking,
                            anim_is_equipping: a.is_equipping,
                            anim_is_unequipping: a.is_unequipping,
                            anim_weapon_state: a.weapon_state,
                            weapon_drawn: a.weapon_drawn,
                        },
                    )
                })
                .collect();

            if player_offsets.is_empty() {
                continue;
            }

            let players_vec = self.snap_fbb.create_vector(&player_offsets);
            let snap = WorldSnapshot::create(
                &mut self.snap_fbb,
                &WorldSnapshotArgs {
                    server_tick: self.server_tick,
                    server_time_ms: now_ms,
                    players: Some(players_vec),
                },
            );
            self.snap_fbb.finish(snap, None);
            let packet = wire::encode(MessageType::WorldSnapshot, self.snap_fbb.finished_data());

            if let Err(e) = self.socket.send_to(&packet, *self_addr).await {
                warn!(peer = %self_addr, error = %e, "WorldSnapshot send failed");
            }
        }
    }

    async fn expire_stale(&mut self) {
        let now = Instant::now();
        let timeout = Duration::from_secs(self.config.connection_timeout_s);
        let stale: Vec<(Entity, SocketAddr, u32)> = self
            .world
            .query::<(Entity, &Connection, &Player)>()
            .iter(&self.world)
            .filter(|(_, c, _)| now.duration_since(c.last_heard) > timeout)
            .map(|(e, c, p)| (e, c.addr, p.id))
            .collect();

        for (entity, addr, pid) in stale {
            info!(
                peer = %addr,
                player_id = pid,
                entity = ?entity,
                "connection timed out, despawning"
            );
            send_disconnect(&self.socket, addr, DisconnectCode::Timeout, "no heartbeat").await;
            self.addr_to_entity.remove(&addr);
            self.world.despawn(entity);
        }
    }
}

#[tokio::main]
async fn main() -> Result<()> {
    tracing_subscriber::fmt()
        .with_env_filter(
            tracing_subscriber::EnvFilter::try_from_default_env().unwrap_or_else(|_| "info".into()),
        )
        .init();

    let config = Config::load_or_default(&Config::default_path());
    let bind: SocketAddr = config.bind.parse()?;
    let socket = bind_udp(bind)?;
    info!(
        %bind,
        version = wire::PROTOCOL_VERSION,
        tick_hz = config.tick_rate_hz,
        snap_hz = config.snapshot_rate_hz,
        timeout_s = config.connection_timeout_s,
        dual_stack = bind.is_ipv6(),
        "skyrim-relive-server listening"
    );

    let mut state = ServerState::new(config, socket);
    let res = state.run().await;
    info!(
        uptime_s = state.server_start.elapsed().as_secs(),
        "server exiting"
    );
    res
}

/// Bind a UDP socket. For IPv6 addresses we explicitly disable `IPV6_V6ONLY`
/// so the socket accepts both v6 peers and v4-mapped-v6 peers — that's the
/// dual-stack behavior most self-hosters expect from `[::]`.
fn bind_udp(addr: SocketAddr) -> Result<UdpSocket> {
    use socket2::{Domain, Protocol, Socket, Type};

    let domain = if addr.is_ipv6() {
        Domain::IPV6
    } else {
        Domain::IPV4
    };
    let sock = Socket::new(domain, Type::DGRAM, Some(Protocol::UDP))?;
    if addr.is_ipv6() {
        sock.set_only_v6(false)?;
    }
    sock.set_nonblocking(true)?;
    sock.bind(&addr.into())?;
    Ok(UdpSocket::from_std(sock.into())?)
}

async fn send_disconnect(socket: &UdpSocket, peer: SocketAddr, code: DisconnectCode, reason: &str) {
    let mut fbb = FlatBufferBuilder::with_capacity(64);
    let reason_str = fbb.create_string(reason);
    let disc = Disconnect::create(
        &mut fbb,
        &DisconnectArgs {
            code,
            reason: Some(reason_str),
        },
    );
    fbb.finish(disc, None);
    let packet = wire::encode(MessageType::Disconnect, fbb.finished_data());
    let _ = socket.send_to(&packet, peer).await;
}
