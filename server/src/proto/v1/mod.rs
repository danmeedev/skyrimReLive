// Wire-format v1. Generated flatbuffer modules are wrapped here; consumers
// use the re-exports below rather than reaching into `*_generated::skyrim_relive::v_1::*`.
//
// Decode a packet body with `flatbuffers::root::<Hello<'_>>(body)?` etc; the
// older `root_as_*` free functions are no longer emitted by flatc.

mod lifecycle_generated;
mod types_generated;
mod world_generated;

pub use lifecycle_generated::skyrim_relive::v_1::{
    AdminAuth, AdminAuthResult, AdminAuthResultArgs, AdminCommand, AdminCommandResult,
    AdminCommandResultArgs, ChatMessage, ChatMessageArgs, Disconnect, DisconnectArgs, Heartbeat,
    HeartbeatArgs, Hello, HelloArgs, LeaveNotify, LeaveNotifyArgs, ServerCommand,
    ServerCommandArgs, Welcome, WelcomeArgs,
};
pub use types_generated::skyrim_relive::v_1::{
    DisconnectCode, MessageType, SkillEntry, SkillEntryArgs,
};
pub use world_generated::skyrim_relive::v_1::{
    AttackClass, CombatEvent, CombatEventArgs, DamageApply, DamageApplyArgs, PlayerInput,
    PlayerInputArgs, PlayerList, PlayerListArgs, PlayerListEntry, PlayerListEntryArgs, PlayerState,
    PlayerStateArgs, Transform, Vec3, WorldSnapshot, WorldSnapshotArgs,
};
