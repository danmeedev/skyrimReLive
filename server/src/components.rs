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
    /// When this connection's owner last landed a `CombatEvent`. Used for
    /// rate-limiting attacks (anti-spam), not for damage cooldown logic.
    pub last_attack_at: Option<Instant>,
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
/// Phase 2.3 — combat HP for damage authority. Death/respawn arrives in
/// a later sub-step; for now HP is informational and clamps at 0.
#[derive(Component, Debug, Clone, Copy)]
pub struct Health {
    pub current: f32,
    pub max: f32,
}

impl Default for Health {
    fn default() -> Self {
        Self {
            current: 100.0,
            max: 100.0,
        }
    }
}

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
    // Phase 2.5: aim pitch (radians, +down/-up).
    pub pitch: f32,
}

/// Phase 3.1: which cell the player is in. 0 = unknown / exterior wildcard.
#[derive(Component, Debug, Default, Clone, Copy)]
pub struct Cell {
    pub form_id: u32,
}

/// Zeus Phase 0: character identity from the loaded save.
#[derive(Component, Debug, Clone)]
pub struct CharacterInfo {
    pub character_name: String,
    pub level: u16,
    pub top_skills: Vec<(String, f32)>,
}

impl Default for CharacterInfo {
    fn default() -> Self {
        Self {
            character_name: String::new(),
            level: 1,
            top_skills: Vec::new(),
        }
    }
}
