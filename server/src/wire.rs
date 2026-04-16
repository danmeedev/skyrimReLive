//! Packet framing for wire-format v1.
//!
//! Header (4 bytes):
//!
//! ```text
//! +------+------+--------+--------+----------------+
//! | 0x52 | 0x4C |  ver   |  type  |  flatbuf body  |
//! |  R   |  L   | 1 byte | 1 byte |   variable     |
//! +------+------+--------+--------+----------------+
//! ```
//!
//! Magic + version are checked before any flatbuffer parsing so junk traffic
//! is rejected cheaply.

use std::fmt;

use crate::proto::v1::MessageType;

pub const MAGIC: [u8; 2] = [b'R', b'L'];
// v1: Phase 0/1 (struct PlayerState). v2: Phase 2.1+ (table PlayerState
// with locomotion fields). v1 servers/clients refuse v2 packets cleanly.
pub const PROTOCOL_VERSION: u8 = 2;
pub const HEADER_LEN: usize = 4;

#[derive(Debug, PartialEq, Eq)]
pub enum DecodeError {
    TooShort(usize),
    BadMagic,
    VersionMismatch(u8),
    UnknownType(u8),
}

impl fmt::Display for DecodeError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::TooShort(n) => write!(f, "packet too short ({n} bytes)"),
            Self::BadMagic => write!(f, "bad magic"),
            Self::VersionMismatch(v) => {
                write!(f, "version mismatch (got {v}, expected {PROTOCOL_VERSION})")
            }
            Self::UnknownType(t) => write!(f, "unknown message type {t}"),
        }
    }
}

impl std::error::Error for DecodeError {}

/// Build a packet by prepending the 4-byte header onto `body`.
#[must_use]
pub fn encode(msg_type: MessageType, body: &[u8]) -> Vec<u8> {
    let mut packet = Vec::with_capacity(HEADER_LEN + body.len());
    packet.extend_from_slice(&MAGIC);
    packet.push(PROTOCOL_VERSION);
    packet.push(msg_type.0);
    packet.extend_from_slice(body);
    packet
}

/// Validate the header and return `(MessageType, body_slice)`.
///
/// `VersionMismatch` carries the offending version so the caller can ship
/// a Disconnect with that detail.
pub fn parse(packet: &[u8]) -> Result<(MessageType, &[u8]), DecodeError> {
    if packet.len() < HEADER_LEN {
        return Err(DecodeError::TooShort(packet.len()));
    }
    if packet[0..2] != MAGIC {
        return Err(DecodeError::BadMagic);
    }
    if packet[2] != PROTOCOL_VERSION {
        return Err(DecodeError::VersionMismatch(packet[2]));
    }
    let raw = packet[3];
    let mt = MessageType(raw);
    // MessageType is an open enum — accept any value the schema declares,
    // reject anything else here so dispatch can be exhaustive.
    match mt {
        MessageType::Hello
        | MessageType::Welcome
        | MessageType::Heartbeat
        | MessageType::LeaveNotify
        | MessageType::Disconnect
        | MessageType::PlayerInput
        | MessageType::WorldSnapshot
        | MessageType::CombatEvent
        | MessageType::DamageApply => Ok((mt, &packet[HEADER_LEN..])),
        _ => Err(DecodeError::UnknownType(raw)),
    }
}

#[cfg(test)]
mod tests {
    #![allow(clippy::unwrap_used, clippy::expect_used, clippy::panic)]

    use super::*;

    #[test]
    fn round_trip() {
        let packet = encode(MessageType::Hello, b"hello-body");
        let (mt, body) = parse(&packet).unwrap();
        assert_eq!(mt, MessageType::Hello);
        assert_eq!(body, b"hello-body");
        // Sanity: encoded packet starts with magic + current protocol version.
        assert_eq!(&packet[0..3], &[b'R', b'L', PROTOCOL_VERSION]);
    }

    #[test]
    fn rejects_too_short() {
        assert_eq!(parse(b"RL").unwrap_err(), DecodeError::TooShort(2));
    }

    #[test]
    fn rejects_bad_magic() {
        assert_eq!(parse(b"XX\x01\x01").unwrap_err(), DecodeError::BadMagic);
    }

    #[test]
    fn rejects_version_mismatch() {
        // Use a version != PROTOCOL_VERSION (currently 2). Bump as needed
        // when the protocol version changes.
        assert_eq!(
            parse(b"RL\x09\x01").unwrap_err(),
            DecodeError::VersionMismatch(9)
        );
    }

    #[test]
    fn rejects_unknown_type() {
        let pkt = [b'R', b'L', PROTOCOL_VERSION, 0xff];
        assert_eq!(parse(&pkt).unwrap_err(), DecodeError::UnknownType(255));
    }
}
