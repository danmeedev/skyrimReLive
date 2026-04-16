// The server crate exposes wire + proto + components + config so test
// clients (echo-client) and integration tests can share the same
// encoders/decoders and ECS types. The binary entry point lives in main.rs.

pub mod components;
pub mod config;
pub mod proto;
pub mod wire;
