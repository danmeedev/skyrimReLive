# Tailscale Hardening — locking down your SkyrimReLive host

By default, every device in your Tailscale tailnet can reach every other
device on every port. That's fine if it's just your laptop and your phone.
It's *not* fine the moment you invite friends in — they can suddenly reach
your printer, your NAS, your work VPN, your home automation, and any other
service running on your PC.

This doc walks you through pinning your friends down to **only UDP port
27015** (the SkyrimReLive server) and **only on the host machine that runs
the game server**. Combined with the operational habit of *only running the
server when you're playing*, this means friends can't reach anything else
on your network even if their account is compromised.

Estimated time: 10 minutes.

---

## Threat model — what we're protecting against

Roughly in order of likelihood:

1. **Friend accidentally pivots from their machine to yours.** Maybe they
   ran a sketchy program on their PC and now it's scanning their tailnet
   neighbours (you).
2. **Friend's Tailscale account gets phished or password-leaked.** Attacker
   inherits whatever access friend had.
3. **Curious friend pokes around** your tailnet "to see what's there."
   Most people won't, but better not to put the temptation in front of
   them.
4. **Friend you've fallen out with** still has access until you remove
   them — easier to remove if their access was scoped narrowly to start.

We're not defending against a sophisticated attacker who already has root
on your machine — that's outside Tailscale's scope.

---

## What you'll set up

- A **group** in your tailnet called `group:friends` containing the people
  you've invited to play.
- A **tag** on your gaming PC called `tag:skyrimrelive` so the ACL can
  target it specifically (and so you don't have to rewrite the ACL if
  the machine name changes).
- An **ACL policy** that says: "members of `group:friends` can talk to
  `tag:skyrimrelive` on UDP 27015 — and nothing else."

End result: friends see your gaming PC in their device list and can
connect to the game, but `ping`, `RDP`, file shares, web browsers, and
every other port on your machine return immediately with no response.

---

## Step 1 — Tag your gaming PC

A tag is Tailscale's way of labelling devices so ACLs can refer to them
without listing exact device names.

1. Open https://login.tailscale.com/admin/acls/file in your browser.
2. You'll see your tailnet's policy file (probably the default
   "allow everything" rule).
3. Find or add the `tagOwners` section. It tells Tailscale who is allowed
   to apply this tag. Replace `you@example.com` with your Tailscale email:

   ```json
   "tagOwners": {
     "tag:skyrimrelive": ["you@example.com"]
   }
   ```

4. **Save** the policy (button at the top of the editor).
5. Now apply the tag to your gaming PC. Open PowerShell **as
   Administrator** on the gaming PC and run:

   ```powershell
   & "C:\Program Files\Tailscale\tailscale.exe" set --advertise-tags=tag:skyrimrelive
   ```

6. Confirm the tag stuck:

   ```powershell
   & "C:\Program Files\Tailscale\tailscale.exe" status
   ```

   Your machine's row should include `tag:skyrimrelive` next to its name.

---

## Step 2 — Define the friends group

Friends are added by their Tailscale-account email (whatever email they
used to sign up — Google, GitHub, or plain email).

1. Back in https://login.tailscale.com/admin/acls/file, find or add the
   `groups` section:

   ```json
   "groups": {
     "group:friends": [
       "friend1@example.com",
       "friend2@example.com"
     ]
   }
   ```

2. Add one entry per friend. Strict rule: the email **must** match what
   they signed up with. Wrong email = silent denial later.

---

## Step 3 — The ACL itself

Replace the default `"action": "accept", "src": ["*"], "dst": ["*:*"]`
rule with these two:

```json
"acls": [
  // You can reach anything in your own tailnet (so admin / debug works).
  {
    "action": "accept",
    "src": ["you@example.com"],
    "dst": ["*:*"]
  },
  // Friends can reach the SkyrimReLive server only, only on UDP 27015.
  {
    "action": "accept",
    "src": ["group:friends"],
    "proto": "udp",
    "dst": ["tag:skyrimrelive:27015"]
  }
]
```

Key points:

- The first rule keeps **you** unrestricted on your own tailnet. Without
  it you'd lock yourself out of admin tasks too.
- The second rule is the actual lockdown. `proto: "udp"` makes sure TCP
  is blocked even on port 27015 (no curiosity probes via web browsers,
  netcat, etc.).
- Nothing else is allowed, because Tailscale ACLs are **default deny** —
  if no rule says "accept" for a given (src, dst, port, proto), the
  packet is dropped.

---

## Putting it all together — a complete ACL policy

Here's the full JSON-with-comments policy file. Paste this into
https://login.tailscale.com/admin/acls/file replacing whatever's there,
edit the email addresses, and save.

```hujson
{
  // Tags allow targeting devices by role rather than by name/IP.
  // Only the tag-owner email below can apply this tag to a machine.
  "tagOwners": {
    "tag:skyrimrelive": ["you@example.com"]
  },

  // Friends invited to the game. Add one email per line.
  "groups": {
    "group:friends": [
      "friend1@example.com",
      "friend2@example.com"
    ]
  },

  "acls": [
    // Admin (you): full access to your tailnet.
    {
      "action": "accept",
      "src":    ["you@example.com"],
      "dst":    ["*:*"]
    },

    // Friends: UDP port 27015 on the gaming PC only.
    {
      "action": "accept",
      "src":    ["group:friends"],
      "proto":  "udp",
      "dst":    ["tag:skyrimrelive:27015"]
    }
  ],

  // Disable SSH access via Tailscale entirely (we don't need it,
  // and it removes another attack surface).
  "ssh": []
}
```

The Tailscale admin console will preview the rules before saving. Read the
preview carefully — it shows exactly which devices can reach which
devices on which ports.

---

## Step 4 — Verify the lockdown actually works

From your friend's PC (or a second device of yours signed in as a "friend"
test account), try these:

```powershell
# Should SUCCEED — UDP 27015 to gaming PC:
& "C:\Program Files\Tailscale\tailscale.exe" ping 100.114.239.92
# (the ICMP ping itself may be blocked, but the server hears UDP packets)

# Should FAIL — TCP to gaming PC:
Test-NetConnection 100.114.239.92 -Port 27015
# Expected: TcpTestSucceeded = False

# Should FAIL — any other port on gaming PC:
Test-NetConnection 100.114.239.92 -Port 80
Test-NetConnection 100.114.239.92 -Port 3389  # RDP, common pivot target
# Both expected: TcpTestSucceeded = False
```

If TCP-to-anything succeeds, the ACL didn't apply — re-check the policy
file in the admin console.

You can also run **danme's friend installer** from the friend account
machine; the connectivity probe at the end of `INSTALL.bat` confirms
UDP 27015 is reachable.

---

## Operational pattern — "only when the server is running"

Tailscale ACLs are static; they don't know whether your server is running.
Service-up gating is enforced by the server's presence on the port:

- **Server running** → port 27015 has a listener → friends connect.
- **Server not running** → port 27015 is closed → packets are dropped by
  the OS (`ICMP unreachable` returned, kernel-level).

The result is exactly what you want: friends can connect *only* when you
have the server running. They can't poke around your machine the rest of
the time because the ACL blocks every other port, and they can't connect
to the game because the port is closed.

So your habit becomes:

1. **Want to play:** start the server (`.\tools\launch.ps1`).
2. **Done playing:** close the server console window.

That's it. No firewall rules to flip, no Tailscale config to change.

If you want truly aggressive lockdown — e.g., "Tailscale itself shouldn't
even be running on the gaming PC unless I'm playing" — open the Tailscale
tray icon and click **Disconnect**. Reconnect when you want to play.
But that's usually overkill.

---

## Adding or removing friends later

To add a friend:
1. Open https://login.tailscale.com/admin/users → **Invite external user**.
2. Send them the magic link.
3. Once they accept, add their email to `group:friends` in the ACL file.
4. Save the policy. Access takes effect within seconds.

To remove a friend:
1. Open the ACL file, remove their email from `group:friends`. Save.
2. (Optional, more thorough) Open https://login.tailscale.com/admin/users
   → click the friend → **Remove user**. This kicks them out of your
   tailnet entirely, not just out of the game.

---

## Bonus — Windows firewall as a belt-and-braces layer

Tailscale already filters before traffic reaches your OS, so this is
truly belt-and-braces. But if you want absolute certainty:

```powershell
# Allow inbound UDP 27015 only from the Tailscale interface (100.x.y.z range).
New-NetFirewallRule -DisplayName "SkyrimReLive (Tailscale only)" `
  -Direction Inbound -Protocol UDP -LocalPort 27015 `
  -RemoteAddress 100.64.0.0/10 -Action Allow
```

The `100.64.0.0/10` range is Tailscale's CGNAT space. Anything outside
that range targeting port 27015 gets denied by Windows even before
SkyrimReLive sees it.

You can verify the rule:

```powershell
Get-NetFirewallRule -DisplayName "SkyrimReLive (Tailscale only)"
```

Remove it later if you ever want to host on the open internet:

```powershell
Remove-NetFirewallRule -DisplayName "SkyrimReLive (Tailscale only)"
```

---

## What this doesn't protect against

Be honest about scope:

- **A friend who is your enemy and connects.** They're inside the game
  with you. They can do whatever the game protocol allows (move, attack,
  send malformed packets to fuzz the server). The Tailscale ACL doesn't
  help with in-game griefing — that's a server-protocol-hardening
  problem, addressed in Phase 2 step 2.4 (transform validation, rate
  limits, etc.).
- **A vulnerability in SkyrimReLive itself.** If someone finds a
  malformed-packet RCE in the server, the ACL doesn't help — they're
  already allowed to send packets to that port. We mitigate this by
  fuzz-testing the wire format and using safe Rust on the server side.
- **A vulnerability in Tailscale itself.** Statistically rare, and outside
  any setup you can do as a user. Keep Tailscale updated.

---

## TL;DR

1. Tag your gaming PC `tag:skyrimrelive`.
2. Group your friends as `group:friends`.
3. Replace the default ACL with: "friends can hit `tag:skyrimrelive:27015`
   on UDP, nothing else."
4. Disable Tailscale SSH unless you need it.
5. Run the SkyrimReLive server only when you're playing.

That's the whole hardening setup. Five minutes after the policy applies,
your tailnet is properly scoped down and friends can play without being
able to touch anything else.
