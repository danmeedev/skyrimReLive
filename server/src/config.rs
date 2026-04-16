//! Runtime configuration, loaded from TOML.
//!
//! Load order: `$RELIVE_CONFIG` env var → `./server.toml` next to cwd →
//! compiled-in defaults. Missing or malformed file falls back to defaults
//! with a warning; never a hard failure so a fresh clone runs out of the box.

use std::path::{Path, PathBuf};

use serde::{Deserialize, Serialize};

#[derive(Deserialize, Serialize, Debug, Clone)]
#[serde(default, deny_unknown_fields)]
pub struct Config {
    /// UDP bind address. Use `0.0.0.0:27015` for all interfaces on LAN.
    pub bind: String,
    /// Server simulation tick. Phase 1 integrates Transform from Velocity.
    pub tick_rate_hz: u8,
    /// `WorldSnapshot` broadcast rate to every connected client.
    pub snapshot_rate_hz: u8,
    /// A connection with no packets within this many seconds is despawned.
    pub connection_timeout_s: u64,
    /// How often the timeout sweep runs, in milliseconds.
    pub gc_interval_ms: u64,
    /// Whether cross-player damage is allowed. Default off — hosts opt in.
    pub pvp_enabled: bool,
    /// Flat damage used for spell hits (true magnitude lookup is deferred).
    pub spell_damage_default: f32,
}

impl Default for Config {
    fn default() -> Self {
        Self {
            bind: "0.0.0.0:27015".into(),
            tick_rate_hz: 60,
            snapshot_rate_hz: 20,
            connection_timeout_s: 5,
            gc_interval_ms: 500,
            pvp_enabled: false,
            spell_damage_default: 25.0,
        }
    }
}

impl Config {
    /// Pick the config path: `$RELIVE_CONFIG` if set, otherwise `server.toml`
    /// next to the current working directory.
    #[must_use]
    pub fn default_path() -> PathBuf {
        std::env::var_os("RELIVE_CONFIG")
            .map_or_else(|| PathBuf::from("server.toml"), PathBuf::from)
    }

    /// Load from a TOML file. Any error (missing, bad syntax, unknown field)
    /// is returned — the caller decides whether to fall back.
    pub fn load(path: &Path) -> anyhow::Result<Self> {
        let text = std::fs::read_to_string(path)?;
        Ok(toml::from_str(&text)?)
    }

    /// Load or fall back. Logs via `tracing` so startup never fails on a
    /// missing config.
    #[must_use]
    pub fn load_or_default(path: &Path) -> Self {
        match Self::load(path) {
            Ok(c) => {
                tracing::info!(path = %path.display(), "config loaded");
                c
            }
            Err(e) => {
                tracing::warn!(
                    path = %path.display(),
                    error = %e,
                    "config load failed; using defaults"
                );
                Self::default()
            }
        }
    }
}

#[cfg(test)]
mod tests {
    #![allow(clippy::unwrap_used)]

    use super::*;

    #[test]
    fn defaults_round_trip_through_toml() {
        let toml_str = toml::to_string(&Config::default()).unwrap();
        let parsed: Config = toml::from_str(&toml_str).unwrap();
        assert_eq!(parsed.bind, "0.0.0.0:27015");
        assert_eq!(parsed.tick_rate_hz, 60);
        assert_eq!(parsed.snapshot_rate_hz, 20);
    }

    #[test]
    fn partial_toml_fills_in_defaults() {
        let partial = "tick_rate_hz = 30\n";
        let c: Config = toml::from_str(partial).unwrap();
        assert_eq!(c.tick_rate_hz, 30);
        assert_eq!(c.snapshot_rate_hz, 20); // default
    }

    #[test]
    fn unknown_field_is_rejected() {
        let bad = "nonsense_field = 1\n";
        let r: Result<Config, _> = toml::from_str(bad);
        assert!(r.is_err());
    }
}
