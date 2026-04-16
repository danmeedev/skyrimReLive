# Security policy

## Reporting a vulnerability

**Do not** open a public issue for security bugs. Instead, contact the
maintainers privately. Until a dedicated address exists, email the lead
maintainer (see commit history for the latest contact).

Include:

- A description of the vulnerability
- Steps to reproduce, ideally with a minimal PoC
- The affected version(s) and platform(s)
- Your name/handle for credit (optional)

We aim to acknowledge within 7 days and ship a fix or mitigation within 30
days for high-severity issues.

## Scope

In scope:

- Server: remote code execution, unauthorized state mutation, auth bypass,
  panics from malformed input, denial of service via cheap packets
- Client plugin: crashes from server-sent data, local privilege escalation,
  data exfiltration beyond what the user intended to share
- Build / supply chain: malicious dependencies, build-script RCE

Out of scope:

- Cheating in single-server, peer-trust co-op deployments (use a server you
  trust, or run an authoritative service for strangers)
- Denial of service requiring administrative access to the host
- Social engineering of self-hosters

## Threat model (short)

The server is the trust boundary. Clients are assumed potentially hostile —
the server validates everything that affects other players or persistent
state. The plugin trusts its own server (you point your client at servers
you trust); it does not trust packets that fail signature/sequence checks.
