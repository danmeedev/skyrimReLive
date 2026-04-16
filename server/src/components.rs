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
#[derive(Component, Debug, Default, Clone, Copy)]
pub struct AnimState {
    pub speed: f32,
    pub direction: f32,
    pub is_running: bool,
    pub is_sprinting: bool,
    pub is_sneaking: bool,
}
