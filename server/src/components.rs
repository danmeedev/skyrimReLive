//! ECS components for connected players.
//!
//! Phase 1 keeps the component set minimal:
//! - `Player` — identity (assigned id, display name)
//! - `Connection` — UDP peer addr + when we last heard from them
//! - `Transform` — replicated position + yaw
//! - `Velocity` — last-known motion (zero in Phase 1; populated when
//!   `PlayerInput` carries it in Phase 1.x)
//!
//! Cell ownership is implicit (one cell, everyone visible to everyone). A
//! `Cell` component arrives in Phase 3 alongside cell transitions.

use std::net::SocketAddr;
use std::time::Instant;

use bevy_ecs::prelude::Component;

#[derive(Component, Debug)]
pub struct Player {
    pub id: u32,
    pub name: String,
}

#[derive(Component, Debug)]
pub struct Connection {
    pub addr: SocketAddr,
    pub last_heard: Instant,
}

#[derive(Component, Debug, Default, Clone, Copy)]
pub struct Transform {
    pub pos: [f32; 3],
    pub yaw: f32,
}

#[derive(Component, Debug, Default, Clone, Copy)]
pub struct Velocity {
    pub v: [f32; 3],
}

/// Phase 2.1 locomotion animation state. Mirrors the small set of
/// animation-graph variables we replicate (`Speed`, `Direction`, `IsRunning`,
/// `IsSprinting`, `IsSneaking`). Will grow as Phase 2 sub-steps add weapon
/// and combat state.
// Clippy flags >3 bools as a smell; here it's intentional — the struct
// mirrors a fixed wire-format layout where each bool maps to a specific
// animation graph variable. Bitfield/enum compression is an optimization
// for Phase 5/6 once we have many more.
#[allow(clippy::struct_excessive_bools)]
#[derive(Component, Debug, Default, Clone, Copy)]
pub struct AnimState {
    // Phase 2.1: locomotion.
    pub speed: f32,
    pub direction: f32,
    pub is_running: bool,
    pub is_sprinting: bool,
    pub is_sneaking: bool,
    // Phase 2.2: weapon state.
    pub is_equipping: bool,
    pub is_unequipping: bool,
    pub weapon_state: i32,
    pub weapon_drawn: bool,
}
