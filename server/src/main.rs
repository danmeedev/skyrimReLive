use std::collections::HashMap;
use std::net::SocketAddr;
use std::time::{Duration, Instant, SystemTime, UNIX_EPOCH};

use anyhow::Result;
use bevy_ecs::prelude::*;
use flatbuffers::FlatBufferBuilder;
use skyrim_relive_server::components::{
    AnimState, Cell, CharacterInfo, Connection, Health, Player, Transform, Velocity,
};
use skyrim_relive_server::config::Config;
use skyrim_relive_server::proto::v1::{
    AdminAuth, AdminAuthResult, AdminAuthResultArgs, AdminCommand, AdminCommandResult,
    AdminCommandResultArgs, AttackClass, ChatMessage, ChatMessageArgs, CombatEvent, DamageApply,
    DamageApplyArgs, Disconnect, DisconnectArgs, DisconnectCode, Hello, MessageType, PlayerInput,
    PlayerList as FbPlayerList, PlayerListArgs as FbPlayerListArgs,
    PlayerListEntry as FbPlayerListEntry, PlayerListEntryArgs as FbPlayerListEntryArgs,
    PlayerState as FbPlayerState, PlayerStateArgs as FbPlayerStateArgs, ServerCommand,
    ServerCommandArgs, SkillEntry as FbSkillEntry, SkillEntryArgs as FbSkillEntryArgs,
    Transform as FbTransform, Vec3 as FbVec3, Welcome, WelcomeArgs, WorldSnapshot,
    WorldSnapshotArgs,
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

        let player_list_period = if self.config.player_list_poll_s > 0 {
            Duration::from_secs(self.config.player_list_poll_s)
        } else {
            Duration::from_secs(3600)
        };
        let player_list_enabled = self.config.player_list_poll_s > 0;

        let mut sim_tick = interval(sim_period);
        let mut snap_tick = interval(snap_period);
        let mut gc_tick = interval(gc_period);
        let mut pl_tick = interval(player_list_period);
        for t in [&mut sim_tick, &mut snap_tick, &mut gc_tick, &mut pl_tick] {
            t.set_missed_tick_behavior(tokio::time::MissedTickBehavior::Delay);
        }

        loop {
            tokio::select! {
                recv = self.socket.recv_from(&mut buf) => {
                    match recv {
                        Ok((len, peer)) => self.handle_packet(peer, &buf[..len]).await,
                        Err(e) if e.raw_os_error() == Some(10054) => {}
                        Err(e) => warn!(error = %e, "recv_from failed"),
                    }
                }
                _ = sim_tick.tick() => self.advance_sim(),
                _ = snap_tick.tick() => self.broadcast_snapshot().await,
                _ = gc_tick.tick() => self.expire_stale().await,
                _ = pl_tick.tick(), if player_list_enabled => {
                    self.broadcast_player_list().await;
                }
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
            MessageType::CombatEvent => self.handle_combat_event(peer, body).await,
            MessageType::ChatMessage => self.handle_chat_message(peer, body).await,
            MessageType::AdminAuth => self.handle_admin_auth(peer, body).await,
            MessageType::AdminCommand => self.handle_admin_command(peer, body).await,
            MessageType::Welcome
            | MessageType::Disconnect
            | MessageType::WorldSnapshot
            | MessageType::DamageApply
            | MessageType::PlayerList
            | MessageType::AdminAuthResult
            | MessageType::AdminCommandResult
            | MessageType::ServerCommand => {
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
        let char_name = hello.character_name().unwrap_or("(unknown)").to_owned();
        let char_level = hello.character_level();
        let top_skills: Vec<(String, f32)> = hello
            .top_skills()
            .map(|v| {
                v.iter()
                    .map(|s| (s.name().unwrap_or("?").to_owned(), s.level()))
                    .collect()
            })
            .unwrap_or_default();

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
                    last_attack_at: None,
                    is_admin: false,
                },
                Transform::default(),
                Velocity::default(),
                AnimState::default(),
                Health::default(),
                Cell::default(),
                CharacterInfo {
                    character_name: char_name.clone(),
                    level: char_level,
                    top_skills,
                },
            ))
            .id();
        self.addr_to_entity.insert(peer, entity);

        info!(
            %peer,
            %name,
            %char_name,
            char_level,
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
            anim.pitch = input.pitch();
        }
        if let Some(mut cell) = self.world.get_mut::<Cell>(entity) {
            cell.form_id = input.cell_form_id();
        }
        // Touch last_heard regardless — acts as heartbeat even when idle.
        if let Some(mut conn) = self.world.get_mut::<Connection>(entity) {
            conn.last_heard = Instant::now();
        }
    }

    /// Phase 2.3 — server-authoritative combat. Client reports a hit on a
    /// peer; server validates (rate-limit, range vs `weapon_reach`, target
    /// exists), clamps damage, applies it, and sends `DamageApply` to the
    /// target. Tightens H1 from Phase 1's "trust everything" baseline.
    #[allow(clippy::too_many_lines)]
    async fn handle_combat_event(&mut self, peer: SocketAddr, body: &[u8]) {
        const MAX_DAMAGE: f32 = 200.0; // sanity clamp
        const REACH_SLACK: f32 = 50.0; // forgive small lag-induced overruns
        const STAGGER_THRESHOLD: f32 = 30.0;
        const MIN_ATTACK_INTERVAL: Duration = Duration::from_millis(200); // 5 hits/sec max
        const MAX_RANGED_DIST: f32 = 5000.0; // ~one cell

        if !self.config.pvp_enabled {
            info!(%peer, "CombatEvent dropped: PvP disabled");
            return;
        }

        let Some(&attacker_entity) = self.addr_to_entity.get(&peer) else {
            warn!(%peer, "CombatEvent from unknown peer (no Hello?)");
            return;
        };

        let event = match flatbuffers::root::<CombatEvent<'_>>(body) {
            Ok(e) => e,
            Err(e) => {
                warn!(%peer, error = %e, "bad CombatEvent payload");
                return;
            }
        };

        // Rate-limit: reject if attacker is swinging too fast.
        let now = Instant::now();
        if let Some(mut conn) = self.world.get_mut::<Connection>(attacker_entity) {
            if let Some(last) = conn.last_attack_at {
                if now.duration_since(last) < MIN_ATTACK_INTERVAL {
                    warn!(%peer, "attack rate-limited");
                    return;
                }
            }
            conn.last_attack_at = Some(now);
        }

        let target_id = event.target_player_id();
        if target_id == 0 {
            // Area attack with no specific target — Phase 2.3 only
            // handles directed combat. Future sub-step adds AoE.
            return;
        }
        // Reject self-attack. Won't happen via real plugin (TESHitEvent
        // filters), but echo-client tests can hit it.
        let attacker_pid = self
            .world
            .get::<Player>(attacker_entity)
            .map_or(0, |p| p.id);
        if target_id == attacker_pid {
            warn!(%peer, target_id, "self-attack rejected");
            return;
        }

        // Look up target entity by player_id. Linear scan over connections
        // is fine for Phase 2 (8-player target); Phase 6 will index this.
        let mut target_entity: Option<Entity> = None;
        let mut target_addr: Option<SocketAddr> = None;
        for (e, p, c) in self
            .world
            .query::<(Entity, &Player, &Connection)>()
            .iter(&self.world)
        {
            if p.id == target_id {
                target_entity = Some(e);
                target_addr = Some(c.addr);
                break;
            }
        }
        let (Some(target_entity), Some(target_addr)) = (target_entity, target_addr) else {
            warn!(%peer, target_id, "CombatEvent target not found");
            return;
        };

        // Range check: Euclidean distance attacker→target. Policy depends
        // on AttackClass — melee uses weapon_reach + slack; ranged trusts
        // the engine's TESHitEvent and only caps at MAX_RANGED_DIST.
        let attacker_pos = self.world.get::<Transform>(attacker_entity).map(|t| t.pos);
        let target_pos = self.world.get::<Transform>(target_entity).map(|t| t.pos);
        let (Some(a), Some(t)) = (attacker_pos, target_pos) else {
            warn!(%peer, target_id, "missing transform on combatants");
            return;
        };
        let dx = a[0] - t[0];
        let dy = a[1] - t[1];
        let dz = a[2] - t[2];
        let dist = (dx * dx + dy * dy + dz * dz).sqrt();
        let max_dist = match event.attack_class() {
            AttackClass::BowArrow | AttackClass::Spell => MAX_RANGED_DIST,
            _ => event.weapon_reach() + REACH_SLACK,
        };
        if dist > max_dist {
            warn!(
                %peer,
                target_id,
                dist,
                max_dist,
                attack_class = event.attack_class().0,
                "CombatEvent rejected: out of range"
            );
            return;
        }

        // Damage clamp + Health update.
        let raw_damage = event.weapon_base_damage();
        let damage = raw_damage.clamp(0.0, MAX_DAMAGE);
        let mut new_hp = 0.0;
        if let Some(mut hp) = self.world.get_mut::<Health>(target_entity) {
            hp.current = (hp.current - damage).max(0.0);
            new_hp = hp.current;
        }

        let server_time_ms = u64::try_from(
            SystemTime::now()
                .duration_since(UNIX_EPOCH)
                .map(|d| d.as_millis())
                .unwrap_or(0),
        )
        .unwrap_or(0);

        info!(
            attacker = attacker_pid,
            target = target_id,
            damage,
            new_hp,
            "combat hit applied"
        );

        // Send DamageApply to the target's client.
        let mut fbb = FlatBufferBuilder::with_capacity(64);
        let dmg = DamageApply::create(
            &mut fbb,
            &DamageApplyArgs {
                attacker_player_id: attacker_pid,
                damage,
                stagger: damage >= STAGGER_THRESHOLD,
                new_hp,
                server_time_ms,
            },
        );
        fbb.finish(dmg, None);
        let packet = wire::encode(MessageType::DamageApply, fbb.finished_data());
        if let Err(e) = self.socket.send_to(&packet, target_addr).await {
            warn!(peer = %target_addr, error = %e, "DamageApply send failed");
        }
    }

    async fn broadcast_server_command(&mut self, command: &str, args: &str) {
        let mut fbb = FlatBufferBuilder::with_capacity(64);
        let cmd_off = fbb.create_string(command);
        let args_off = fbb.create_string(args);
        let sc = ServerCommand::create(
            &mut fbb,
            &ServerCommandArgs {
                command: Some(cmd_off),
                args: Some(args_off),
            },
        );
        fbb.finish(sc, None);
        let packet = wire::encode(MessageType::ServerCommand, fbb.finished_data());
        for (_, conn) in self
            .world
            .query::<(Entity, &Connection)>()
            .iter(&self.world)
        {
            let _ = self.socket.send_to(&packet, conn.addr).await;
        }
    }

    async fn send_server_command_to(&mut self, target_pid: u32, command: &str, args: &str) {
        let mut fbb = FlatBufferBuilder::with_capacity(64);
        let cmd_off = fbb.create_string(command);
        let args_off = fbb.create_string(args);
        let sc = ServerCommand::create(
            &mut fbb,
            &ServerCommandArgs {
                command: Some(cmd_off),
                args: Some(args_off),
            },
        );
        fbb.finish(sc, None);
        let packet = wire::encode(MessageType::ServerCommand, fbb.finished_data());
        for (_, p, c) in self
            .world
            .query::<(Entity, &Player, &Connection)>()
            .iter(&self.world)
        {
            if p.id == target_pid {
                let _ = self.socket.send_to(&packet, c.addr).await;
                break;
            }
        }
    }

    async fn send_admin_result(&self, peer: SocketAddr, success: bool, msg: &str) {
        let mut fbb = FlatBufferBuilder::with_capacity(128);
        let msg_off = fbb.create_string(msg);
        let r = AdminCommandResult::create(
            &mut fbb,
            &AdminCommandResultArgs {
                success,
                message: Some(msg_off),
            },
        );
        fbb.finish(r, None);
        let packet = wire::encode(MessageType::AdminCommandResult, fbb.finished_data());
        let _ = self.socket.send_to(&packet, peer).await;
    }

    async fn handle_admin_auth(&mut self, peer: SocketAddr, body: &[u8]) {
        let Some(&entity) = self.addr_to_entity.get(&peer) else {
            warn!(%peer, "AdminAuth from unknown peer");
            return;
        };
        let auth = match flatbuffers::root::<AdminAuth<'_>>(body) {
            Ok(a) => a,
            Err(e) => {
                warn!(%peer, error = %e, "bad AdminAuth payload");
                return;
            }
        };
        let password = auth.password().unwrap_or("");
        // Empty admin_password = no password required (friend-trust default).
        let success =
            self.config.admin_password.is_empty() || password == self.config.admin_password;
        if success {
            if let Some(mut conn) = self.world.get_mut::<Connection>(entity) {
                conn.is_admin = true;
            }
        }
        let pid = self.world.get::<Player>(entity).map_or(0, |p| p.id);
        info!(%peer, pid, success, "admin auth attempt");
        let mut fbb = FlatBufferBuilder::with_capacity(64);
        let reason_str = if success {
            "admin access granted"
        } else {
            "wrong password"
        };
        let reason = fbb.create_string(reason_str);
        let r = AdminAuthResult::create(
            &mut fbb,
            &AdminAuthResultArgs {
                success,
                reason: Some(reason),
            },
        );
        fbb.finish(r, None);
        let packet = wire::encode(MessageType::AdminAuthResult, fbb.finished_data());
        let _ = self.socket.send_to(&packet, peer).await;
    }

    #[allow(clippy::too_many_lines)]
    async fn handle_admin_command(&mut self, peer: SocketAddr, body: &[u8]) {
        let Some(&entity) = self.addr_to_entity.get(&peer) else {
            return;
        };
        let is_admin = self
            .world
            .get::<Connection>(entity)
            .is_some_and(|c| c.is_admin);
        if !is_admin {
            self.send_admin_result(
                peer,
                false,
                "not authenticated; use `rl admin <password>` first",
            )
            .await;
            return;
        }
        let cmd = match flatbuffers::root::<AdminCommand<'_>>(body) {
            Ok(c) => c,
            Err(e) => {
                warn!(%peer, error = %e, "bad AdminCommand payload");
                return;
            }
        };
        let command = cmd.command().unwrap_or("").to_owned();
        let parts: Vec<&str> = command.split_whitespace().collect();
        if parts.is_empty() {
            self.send_admin_result(peer, false, "empty command").await;
            return;
        }
        let admin_pid = self.world.get::<Player>(entity).map_or(0, |p| p.id);
        info!(%peer, admin_pid, %command, "admin command");

        match parts[0] {
            "pvp" => {
                if parts.len() < 2 {
                    self.send_admin_result(peer, false, "usage: pvp on|off")
                        .await;
                    return;
                }
                match parts[1] {
                    "on" | "true" | "1" => {
                        self.config.pvp_enabled = true;
                        self.send_admin_result(peer, true, "PvP enabled").await;
                    }
                    "off" | "false" | "0" => {
                        self.config.pvp_enabled = false;
                        self.send_admin_result(peer, true, "PvP disabled").await;
                    }
                    _ => {
                        self.send_admin_result(peer, false, "usage: pvp on|off")
                            .await;
                    }
                }
            }
            "kick" => {
                if parts.len() < 2 {
                    self.send_admin_result(peer, false, "usage: kick <player_id>")
                        .await;
                    return;
                }
                let Ok(target_id) = parts[1].parse::<u32>() else {
                    self.send_admin_result(peer, false, "bad player_id").await;
                    return;
                };
                let mut found = false;
                let mut target_addr = None;
                for (_e, p, c) in self
                    .world
                    .query::<(Entity, &Player, &Connection)>()
                    .iter(&self.world)
                {
                    if p.id == target_id {
                        target_addr = Some(c.addr);
                        found = true;
                        break;
                    }
                }
                if !found {
                    self.send_admin_result(
                        peer,
                        false,
                        &format!("player_id {target_id} not found"),
                    )
                    .await;
                    return;
                }
                let Some(addr) = target_addr else { return };
                send_disconnect(&self.socket, addr, DisconnectCode::Ok, "kicked by admin").await;
                // Find and despawn the entity
                let to_remove: Vec<(Entity, u32)> = self
                    .world
                    .query::<(Entity, &Player, &Connection)>()
                    .iter(&self.world)
                    .filter(|(_, p, _)| p.id == target_id)
                    .map(|(e, p, _)| (e, p.id))
                    .collect();
                for (e, pid) in to_remove {
                    self.addr_to_entity.remove(&addr);
                    self.world.despawn(e);
                    info!(pid, "player kicked by admin");
                }
                self.send_admin_result(peer, true, &format!("kicked player_id {target_id}"))
                    .await;
            }
            "time" => {
                if parts.len() < 2 {
                    self.send_admin_result(peer, false, "usage: time <hour> (0-23)")
                        .await;
                    return;
                }
                let Ok(hour) = parts[1].parse::<f32>() else {
                    self.send_admin_result(peer, false, "bad hour value").await;
                    return;
                };
                if !(0.0..=24.0).contains(&hour) {
                    self.send_admin_result(peer, false, "hour must be 0-24")
                        .await;
                    return;
                }
                self.broadcast_server_command("time", parts[1]).await;
                self.send_admin_result(
                    peer,
                    true,
                    &format!("time set to {hour:.0}:00 for all players"),
                )
                .await;
            }
            "weather" => {
                if parts.len() < 2 {
                    self.send_admin_result(
                        peer,
                        false,
                        "usage: weather <formid> (hex, e.g. 10e1f2) or: weather clear|rain|snow|storm",
                    )
                    .await;
                    return;
                }
                let arg = match parts[1] {
                    "clear" => "0x81a",
                    "rain" | "rainy" => "0x10a23e",
                    "snow" | "snowy" => "0x10e1f2",
                    "storm" | "thunder" => "0x10a241",
                    "fog" | "foggy" => "0x10e1f0",
                    other => other,
                };
                self.broadcast_server_command("weather", arg).await;
                self.send_admin_result(
                    peer,
                    true,
                    &format!("weather set to {arg} for all players"),
                )
                .await;
            }
            "give" => {
                // give <player_id> <item_form_id> [count]
                if parts.len() < 3 {
                    self.send_admin_result(
                        peer,
                        false,
                        "usage: give <player_id> <item_formid> [count]",
                    )
                    .await;
                    return;
                }
                let Ok(target_pid) = parts[1].parse::<u32>() else {
                    self.send_admin_result(peer, false, "bad player_id").await;
                    return;
                };
                let item_arg = parts[2];
                let count = parts
                    .get(3)
                    .and_then(|s| s.parse::<u32>().ok())
                    .unwrap_or(1);
                let args_str = format!("{item_arg} {count}");
                self.send_server_command_to(target_pid, "give", &args_str)
                    .await;
                self.send_admin_result(
                    peer,
                    true,
                    &format!("gave {count}x {item_arg} to player_id {target_pid}"),
                )
                .await;
            }
            "spawn" => {
                // spawn <base_form_id>
                if parts.len() < 2 {
                    self.send_admin_result(peer, false, "usage: spawn <base_formid>")
                        .await;
                    return;
                }
                let base_arg = parts[1];
                // Look up admin's current position for spawn location.
                let pos = self
                    .world
                    .get::<Transform>(entity)
                    .map_or([0.0_f32, 0.0, 0.0], |t| t.pos);
                let args_str = format!("{base_arg} {:.1} {:.1} {:.1}", pos[0], pos[1], pos[2]);
                self.broadcast_server_command("spawn", &args_str).await;
                self.send_admin_result(
                    peer,
                    true,
                    &format!(
                        "spawning {base_arg} at ({:.0}, {:.0}, {:.0}) for all",
                        pos[0], pos[1], pos[2]
                    ),
                )
                .await;
            }
            "tp" => {
                // tp <player_id> <x> <y> <z>   — teleport to coords
                // tp <player_id> tome           — teleport to admin
                if parts.len() < 3 {
                    self.send_admin_result(
                        peer,
                        false,
                        "usage: tp <player_id> <x y z> OR tp <player_id> tome",
                    )
                    .await;
                    return;
                }
                let Ok(target_pid) = parts[1].parse::<u32>() else {
                    self.send_admin_result(peer, false, "bad player_id").await;
                    return;
                };
                let args_str = if parts[2] == "tome" || parts[2] == "here" {
                    let pos = self
                        .world
                        .get::<Transform>(entity)
                        .map_or([0.0_f32, 0.0, 0.0], |t| t.pos);
                    format!("{:.1} {:.1} {:.1}", pos[0], pos[1], pos[2])
                } else if parts.len() >= 5 {
                    format!("{} {} {}", parts[2], parts[3], parts[4])
                } else {
                    self.send_admin_result(
                        peer,
                        false,
                        "usage: tp <player_id> <x y z> OR tp <player_id> tome",
                    )
                    .await;
                    return;
                };
                self.send_server_command_to(target_pid, "tp", &args_str)
                    .await;
                self.send_admin_result(
                    peer,
                    true,
                    &format!("teleporting player_id {target_pid} to ({args_str})"),
                )
                .await;
            }
            "obj" => {
                if parts.len() < 3 {
                    self.send_admin_result(
                        peer,
                        false,
                        "usage: obj <zeus_id> <order> [args]\norders: delete, moveto <x y z>",
                    )
                    .await;
                    return;
                }
                let remainder = parts[1..].join(" ");
                self.broadcast_server_command("obj", &remainder).await;
                self.send_admin_result(peer, true, &format!("obj order sent: {remainder}"))
                    .await;
            }
            "npc" => {
                if parts.len() < 3 {
                    self.send_admin_result(
                        peer,
                        false,
                        "usage: npc <zeus_id> <order> [args]\norders: follow, wait, moveto <x y z>, aggro <0-3>, confidence <0-4>, combat, passive, delete",
                    )
                    .await;
                    return;
                }
                let remainder = parts[1..].join(" ");
                self.broadcast_server_command("npc", &remainder).await;
                self.send_admin_result(peer, true, &format!("npc order sent: {remainder}"))
                    .await;
            }
            "npcs" => {
                // List NPCs — this is client-local, so just tell the admin
                // to check their own console. Broadcast a "npcs" command
                // that the admin's client handles.
                self.send_server_command_to(admin_pid, "npcs", "").await;
                self.send_admin_result(peer, true, "listing spawned NPCs...")
                    .await;
            }
            "help" => {
                self.send_admin_result(
                    peer,
                    true,
                    "admin commands:\n  pvp on|off\n  kick <id>\n  time <hour>\n  weather <type|formid>\n  give <pid> <item> [n]\n  spawn <base>\n  tp <pid> <x y z> | tp <pid> tome\n  npc <zeus_id> <order> [args]\n  npcs\n  help",
                )
                .await;
            }
            _ => {
                self.send_admin_result(
                    peer,
                    false,
                    &format!("unknown admin command '{}'; try `rl cmd help`", parts[0]),
                )
                .await;
            }
        }
    }

    async fn handle_chat_message(&mut self, peer: SocketAddr, body: &[u8]) {
        let Some(&sender_entity) = self.addr_to_entity.get(&peer) else {
            warn!(%peer, "ChatMessage from unknown peer");
            return;
        };
        let msg = match flatbuffers::root::<ChatMessage<'_>>(body) {
            Ok(m) => m,
            Err(e) => {
                warn!(%peer, error = %e, "bad ChatMessage payload");
                return;
            }
        };
        let text = msg.text().unwrap_or("").to_owned();
        if text.is_empty() {
            return;
        }
        let (sender_pid, sender_name) = self
            .world
            .get::<Player>(sender_entity)
            .map_or((0, String::new()), |p| (p.id, p.name.clone()));

        info!(%peer, sender_pid, %sender_name, %text, "chat");

        let now_ms = u64::try_from(
            SystemTime::now()
                .duration_since(UNIX_EPOCH)
                .map(|d| d.as_millis())
                .unwrap_or(0),
        )
        .unwrap_or(0);

        let mut fbb = FlatBufferBuilder::with_capacity(128);
        let name_off = fbb.create_string(&sender_name);
        let text_off = fbb.create_string(&text);
        let cm = ChatMessage::create(
            &mut fbb,
            &ChatMessageArgs {
                player_id: sender_pid,
                sender_name: Some(name_off),
                text: Some(text_off),
                server_time_ms: now_ms,
            },
        );
        fbb.finish(cm, None);
        let packet = wire::encode(MessageType::ChatMessage, fbb.finished_data());

        for (_, conn) in self
            .world
            .query::<(Entity, &Connection)>()
            .iter(&self.world)
        {
            let _ = self.socket.send_to(&packet, conn.addr).await;
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
        let all: Vec<(u32, Transform, AnimState, Cell, SocketAddr)> = self
            .world
            .query::<(&Player, &Transform, &AnimState, &Cell, &Connection)>()
            .iter(&self.world)
            .map(|(p, t, a, cell, c)| (p.id, *t, *a, *cell, c.addr))
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

        for (self_pid, _, _, self_cell, self_addr) in &all {
            self.snap_fbb.reset();

            // AoI filter: include peers in the same cell. cell_form_id=0
            // means "unknown/exterior" — treat as wildcard (visible to
            // everyone in the same worldspace). Two interior players only
            // see each other if their cell FormIDs match.
            let player_offsets: Vec<_> = all
                .iter()
                .filter(|(pid, _, _, peer_cell, _)| {
                    if pid == self_pid {
                        return false;
                    }
                    // 0 = exterior / unknown → visible to everyone
                    if self_cell.form_id == 0 || peer_cell.form_id == 0 {
                        return true;
                    }
                    self_cell.form_id == peer_cell.form_id
                })
                .map(|(pid, t, a, cell, _)| {
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
                            pitch: a.pitch,
                            cell_form_id: cell.form_id,
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

    async fn broadcast_player_list(&mut self) {
        let now_ms = u64::try_from(
            SystemTime::now()
                .duration_since(UNIX_EPOCH)
                .map(|d| d.as_millis())
                .unwrap_or(0),
        )
        .unwrap_or(0);

        let mut fbb = FlatBufferBuilder::with_capacity(512);

        let entries: Vec<_> = self
            .world
            .query::<(
                &Player,
                &CharacterInfo,
                &Transform,
                &Cell,
                &Health,
                &Connection,
            )>()
            .iter(&self.world)
            .map(|(p, ci, t, cell, hp, _)| {
                let display_name = fbb.create_string(&p.name);
                let char_name = fbb.create_string(&ci.character_name);
                let skills: Vec<_> = ci
                    .top_skills
                    .iter()
                    .map(|(name, level)| {
                        let n = fbb.create_string(name);
                        FbSkillEntry::create(
                            &mut fbb,
                            &FbSkillEntryArgs {
                                name: Some(n),
                                level: *level,
                            },
                        )
                    })
                    .collect();
                let skills_vec = fbb.create_vector(&skills);
                let pos = FbVec3::new(t.pos[0], t.pos[1], t.pos[2]);
                FbPlayerListEntry::create(
                    &mut fbb,
                    &FbPlayerListEntryArgs {
                        player_id: p.id,
                        display_name: Some(display_name),
                        character_name: Some(char_name),
                        character_level: ci.level,
                        top_skills: Some(skills_vec),
                        pos: Some(&pos),
                        cell_form_id: cell.form_id,
                        hp: hp.current,
                        hp_max: hp.max,
                    },
                )
            })
            .collect();

        let players_vec = fbb.create_vector(&entries);
        let pl = FbPlayerList::create(
            &mut fbb,
            &FbPlayerListArgs {
                server_time_ms: now_ms,
                players: Some(players_vec),
            },
        );
        fbb.finish(pl, None);
        let packet = wire::encode(MessageType::PlayerList, fbb.finished_data());

        for (_, conn) in self
            .world
            .query::<(Entity, &Connection)>()
            .iter(&self.world)
        {
            let _ = self.socket.send_to(&packet, conn.addr).await;
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
        pvp = config.pvp_enabled,
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
