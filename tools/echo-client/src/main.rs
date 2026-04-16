use std::time::{Duration, SystemTime, UNIX_EPOCH};

use anyhow::{bail, Context, Result};
use flatbuffers::FlatBufferBuilder;
use skyrim_relive_server::proto::v1::{
    AttackClass, CombatEvent, CombatEventArgs, DamageApply, Disconnect, Heartbeat, HeartbeatArgs,
    Hello, HelloArgs, LeaveNotify, LeaveNotifyArgs, MessageType, PlayerInput, PlayerInputArgs,
    Transform as FbTransform, Vec3 as FbVec3, Welcome, WorldSnapshot,
};
use skyrim_relive_server::wire;
use tokio::net::UdpSocket;
use tokio::time::{interval, sleep, timeout, Instant};

// Echo-client is a procedural test driver; one long top-level fn is the
// clearest shape, not worth splitting up.
#[allow(clippy::too_many_lines)]
#[tokio::main]
async fn main() -> Result<()> {
    let mut name = String::from("dovahkiin");
    let mut server = String::from("127.0.0.1:27015");
    let mut force_bad_version = false;
    let mut keepalive_secs: u64 = 0;
    let mut send_leave = false;
    // --attack <player_id>: every 1 s during keepalive, send a CombatEvent
    // targeting that id with synthetic weapon stats. Used to validate
    // server-side combat authority + DamageApply round-trip.
    let mut attack_target: Option<u32> = None;

    let args: Vec<String> = std::env::args().skip(1).collect();
    let mut i = 0;
    while i < args.len() {
        match args[i].as_str() {
            "--name" => {
                name = args.get(i + 1).context("--name needs a value")?.clone();
                i += 2;
            }
            "--server" => {
                server = args.get(i + 1).context("--server needs a value")?.clone();
                i += 2;
            }
            "--bad-version" => {
                force_bad_version = true;
                i += 1;
            }
            "--keepalive" => {
                keepalive_secs = args
                    .get(i + 1)
                    .context("--keepalive needs a value")?
                    .parse()?;
                i += 2;
            }
            "--leave" => {
                send_leave = true;
                i += 1;
            }
            "--attack" => {
                attack_target = Some(
                    args.get(i + 1)
                        .context("--attack needs a target player_id")?
                        .parse()?,
                );
                i += 2;
            }
            "-h" | "--help" => {
                println!(
                    "usage: echo-client [--name <name>] [--server host:port] \
                     [--bad-version] [--keepalive <secs>] [--leave] \
                     [--attack <target_player_id>]"
                );
                return Ok(());
            }
            other => bail!("unknown arg: {other}"),
        }
    }

    let socket = UdpSocket::bind("0.0.0.0:0").await?;
    socket.connect(&server).await?;

    // ---- Hello ----------------------------------------------------------
    let mut fbb = FlatBufferBuilder::with_capacity(64);
    let name_str = fbb.create_string(&name);
    let hello = Hello::create(
        &mut fbb,
        &HelloArgs {
            name: Some(name_str),
            client_protocol_version: wire::PROTOCOL_VERSION,
        },
    );
    fbb.finish(hello, None);

    let mut packet = wire::encode(MessageType::Hello, fbb.finished_data());
    if force_bad_version {
        packet[2] = 0xff;
    }
    socket.send(&packet).await?;
    println!("sent: Hello {{ name = {name:?}, version = {} }}", packet[2]);

    let mut buf = [0u8; 2048];
    let n = timeout(Duration::from_secs(2), socket.recv(&mut buf))
        .await
        .context("timed out waiting for server reply")??;

    let (mt, body) = wire::parse(&buf[..n])?;
    match mt {
        MessageType::Welcome => {
            let w = flatbuffers::root::<Welcome<'_>>(body)?;
            println!(
                "Welcome {{ player_id = {}, tick = {} Hz, snapshot = {} Hz, your_addr = {:?} }}",
                w.player_id(),
                w.server_tick_rate_hz(),
                w.server_snapshot_rate_hz(),
                w.your_addr(),
            );
        }
        MessageType::Disconnect => {
            let d = flatbuffers::root::<Disconnect<'_>>(body)?;
            println!(
                "Disconnect {{ code = {:?}, reason = {:?} }}",
                d.code(),
                d.reason(),
            );
            return Ok(());
        }
        _ => println!("unexpected reply type: {mt:?}"),
    }

    // ---- Optional keepalive loop ---------------------------------------
    if keepalive_secs > 0 {
        println!(
            "keeping alive for {keepalive_secs}s, sending Heartbeat @ 1Hz + PlayerInput @ 20Hz"
        );
        let deadline = Instant::now() + Duration::from_secs(keepalive_secs);
        let mut hb_ticker = interval(Duration::from_secs(1));
        let mut input_ticker = interval(Duration::from_millis(50));
        let mut attack_ticker = interval(Duration::from_secs(1));
        let mut damage_received: u32 = 0;
        let mut snapshot_count: u64 = 0;
        let mut other_pids = std::collections::BTreeSet::<u32>::new();
        let mut last_seen_tick: u64 = 0;
        let mut last_saw_other_pos: Option<(f32, f32, f32)> = None;
        let mut frame: u32 = 0;

        loop {
            tokio::select! {
                _ = hb_ticker.tick() => {
                    if Instant::now() >= deadline { break; }
                    send_heartbeat(&socket).await?;
                }
                _ = input_ticker.tick() => {
                    if Instant::now() >= deadline { break; }
                    // Synthetic circular motion so snapshots carry non-zero data.
                    frame = frame.wrapping_add(1);
                    #[allow(clippy::cast_precision_loss)]
                    let t = frame as f32 * 0.05;
                    send_player_input(&socket, t.cos() * 100.0, t.sin() * 100.0, 0.0, t).await?;
                }
                _ = attack_ticker.tick() => {
                    if Instant::now() >= deadline { break; }
                    if let Some(target) = attack_target {
                        send_combat_event(&socket, target, 50.0, 100.0).await?;
                    }
                }
                read = socket.recv(&mut buf) => {
                    let Ok(n) = read else { continue };
                    let Ok((mt, body)) = wire::parse(&buf[..n]) else { continue };
                    match mt {
                        MessageType::Disconnect => {
                            let d = flatbuffers::root::<Disconnect<'_>>(body)?;
                            println!("server sent Disconnect: code={:?} reason={:?}",
                                d.code(), d.reason());
                            return Ok(());
                        }
                        MessageType::WorldSnapshot => {
                            let snap = flatbuffers::root::<WorldSnapshot<'_>>(body)?;
                            snapshot_count += 1;
                            last_seen_tick = snap.server_tick();
                            if let Some(players) = snap.players() {
                                for p in &players {
                                    other_pids.insert(p.player_id());
                                    // PlayerState is a table in v2 — transform() now returns Option.
                                    let Some(t) = p.transform() else { continue };
                                    let pos = t.pos();
                                    last_saw_other_pos = Some((pos.x(), pos.y(), pos.z()));
                                }
                            }
                        }
                        MessageType::DamageApply => {
                            let d = flatbuffers::root::<DamageApply<'_>>(body)?;
                            damage_received += 1;
                            println!(
                                "DamageApply: from player_id={} damage={:.1} stagger={} new_hp={:.1}",
                                d.attacker_player_id(),
                                d.damage(),
                                d.stagger(),
                                d.new_hp(),
                            );
                        }
                        _ => {}
                    }
                }
            }
        }
        if let Some((x, y, z)) = last_saw_other_pos {
            println!("last other-player pos seen: ({x:.1}, {y:.1}, {z:.1})");
        }
        if attack_target.is_some() {
            println!("DamageApply messages received: {damage_received}");
        }

        // Lossy cast to f64 is fine — these counters never exceed ~2^32.
        #[allow(clippy::cast_precision_loss)]
        let secs = keepalive_secs as f64;
        #[allow(clippy::cast_precision_loss)]
        let rate = snapshot_count as f64 / secs;
        println!(
            "snapshots received: {snapshot_count} ({rate:.1}/s), last server_tick={last_seen_tick}, \
             saw other player_ids = {other_pids:?}"
        );
    }

    // ---- Optional graceful leave ---------------------------------------
    if send_leave {
        let mut fbb = FlatBufferBuilder::with_capacity(64);
        let reason = fbb.create_string("client exiting");
        let lv = LeaveNotify::create(
            &mut fbb,
            &LeaveNotifyArgs {
                reason: Some(reason),
            },
        );
        fbb.finish(lv, None);
        let packet = wire::encode(MessageType::LeaveNotify, fbb.finished_data());
        socket.send(&packet).await?;
        println!("sent: LeaveNotify");
        // Tiny pause so the packet leaves the kernel before we drop the socket.
        sleep(Duration::from_millis(50)).await;
    }

    Ok(())
}

async fn send_player_input(socket: &UdpSocket, x: f32, y: f32, z: f32, yaw: f32) -> Result<()> {
    let mut fbb = FlatBufferBuilder::with_capacity(96);
    let pos = FbVec3::new(x, y, z);
    let xform = FbTransform::new(&pos, yaw);
    let now_ms = u64::try_from(
        std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .map(|d| d.as_millis())
            .unwrap_or(0),
    )
    .unwrap_or(0);
    let input = PlayerInput::create(
        &mut fbb,
        &PlayerInputArgs {
            transform: Some(&xform),
            client_time_ms: now_ms,
            // Phase 2.1: synthetic locomotion — pretend we're running.
            // Lets the server-side ECS receive non-default anim values
            // so headless tests exercise the new fields.
            anim_speed: 350.0,
            anim_direction: 0.0,
            anim_is_running: true,
            anim_is_sprinting: false,
            anim_is_sneaking: false,
            // Phase 2.2: synthetic "weapon out, one-handed sword" state.
            anim_is_equipping: false,
            anim_is_unequipping: false,
            anim_weapon_state: 1, // iState=1 == sword/dagger right hand
            weapon_drawn: true,
            pitch: 0.0,
            cell_form_id: 0,
        },
    );
    fbb.finish(input, None);
    let packet = wire::encode(MessageType::PlayerInput, fbb.finished_data());
    socket.send(&packet).await?;
    Ok(())
}

async fn send_combat_event(
    socket: &UdpSocket,
    target_player_id: u32,
    weapon_reach: f32,
    weapon_base_damage: f32,
) -> Result<()> {
    let mut fbb = FlatBufferBuilder::with_capacity(64);
    let now_ms = u64::try_from(
        std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .map(|d| d.as_millis())
            .unwrap_or(0),
    )
    .unwrap_or(0);
    let evt = CombatEvent::create(
        &mut fbb,
        &CombatEventArgs {
            target_player_id,
            attack_type: 0,
            weapon_reach,
            weapon_base_damage,
            client_time_ms: now_ms,
            attack_class: AttackClass::Melee,
        },
    );
    fbb.finish(evt, None);
    let packet = wire::encode(MessageType::CombatEvent, fbb.finished_data());
    socket.send(&packet).await?;
    println!("sent: CombatEvent target={target_player_id} damage={weapon_base_damage}");
    Ok(())
}

async fn send_heartbeat(socket: &UdpSocket) -> Result<()> {
    let mut fbb = FlatBufferBuilder::with_capacity(32);
    let hb = Heartbeat::create(
        &mut fbb,
        &HeartbeatArgs {
            client_time_ms: u64::try_from(
                SystemTime::now()
                    .duration_since(UNIX_EPOCH)
                    .map(|d| d.as_millis())
                    .unwrap_or(0),
            )
            .unwrap_or(0),
        },
    );
    fbb.finish(hb, None);
    let packet = wire::encode(MessageType::Heartbeat, fbb.finished_data());
    socket.send(&packet).await?;
    println!("sent: Heartbeat");
    Ok(())
}
