# Hermes Familiar host integration

Two integration modes, one device protocol. **Plugin mode is the product**;
the bridge is the dev/remote fallback.

The current device profile is Waveshare ESP32-S3-Touch-LCD-3.49 V1. The
original 2.8-inch firmware is maintained as a separate legacy build.

### V1 page map

The V1 has eight tabs: **FACE** (avatar, live state, latest response),
**MSGS** (two recent cards, retained-message reader, and five-entry history),
**OPS** (six action slots and approval controls), **FLEET** (workers),
**CRON** (scheduled jobs), **NET** (gateway/network state), **DEV** (board
diagnostics), and **SET** (five themes, sound mute, animation, and brightness).
The MSGS full-text reader fetches bounded UTF-8 chunks; the host retains up to
40 records with bodies capped at 16 KiB each. Messages longer than that host
retention limit are shortened.

## Mode 1 — native gateway plugin (recommended)

`plugin/` installs as the Hermes plugin `familiar` (`./install.sh`, then
`hermes gateway restart`). It runs inside the gateway process and needs no
API server, no polling, and no separate daemon.

### Host → device (gateway hooks)

| Hook | Device effect |
|---|---|
| `pre_llm_call` | `running=1`, msg `thinking… (<platform>)` |
| `pre_tool_call` / `post_tool_call` | msg `searching the web…` etc. / back to thinking |
| `post_llm_call` | Page 1 ticker entry + `event:message` (portrait blink) |
| `on_session_start` / `on_session_end` | presence refresh |
| `pre_approval_request` | `type:permission` push — device jumps to ALLOW/DENY, `waiting=1` |
| `post_approval_response` | pending cleared however it resolved (device, `/approve`, timeout) |

A full state frame is also sent as a 2 s heartbeat (firmware marks the host
offline after 30 s). Daily aggregates (sessions/tokens/tools) are read from
`state.db` read-only at most once a minute.

On V1, Page 1 presents two 55 px message cards with two full preview lines.
Tapping a card opens that entry's retained text; vertical swipes browse one entry at a time through
five-entry history batches, and tapping returns to the prior card position.
Page 0's latest-response Markdown viewer remains available separately.

The plugin adds optional `entry_ids` beside state `entries` and `ids` beside
history `lines`. Previews contain up to 200 characters before complete-frame
fitting. A selected entry is requested with
`{"cmd":"msg","id":"…","offset":0}`; `offset` is a UTF-8 byte offset and
may be omitted by older clients. Replies include `body`, `body_offset`,
`body_total`, `has_more`, `retained_truncated`, and legacy `truncated`.
The reader appends ordered chunks and requests the next offset until complete.

The host retains at most 40 detail records, each capped at 16 KiB of UTF-8.
Each chunk is capped at 3200 UTF-8 bytes and a 4095-byte escaped JSON frame;
escaping can reduce the chunk further. `retained_truncated` marks clipping at
the 16 KiB source limit. Legacy `truncated` also remains true when `has_more`
so older clients can report that their first chunk is incomplete. A stale ID
returns `error:"stale"`, a preview-only record `error:"unavailable"`, and an
invalid/out-of-range/mid-codepoint offset `error:"offset"`. Closing the reader,
a late response, or a wrong offset cannot replace another selected message.
If Markdown formatting limits are exhausted, the complete retained text stays
scrollable with a simplified-format notice. Longer original messages show a
shortening notice. At gateway startup, recent user/assistant messages are
seeded read-only from `state.db` (up to 40 rows from the last 24 hours).

Historical rollout evidence: the reader V1 image was flashed and hash-verified as
`8819e97d9486ae66c44d38f3b7f49950ac5b9c506f402de520f584187508c9b4`.
The matching plugin was deployed to the existing gateway with the user's
approved graceful restart; settings were preserved. An authenticated live
check received 209-character previews, five stable IDs, and all 16,384 retained
bytes of a message in six valid chunks. That message's original body exceeded
the retention cap. The user reports the UI mainly works and confirmed a short Yandex speech
clip; broader audio validation remains pending. A later Gruvbox build is tracked separately in the handoff.

Additional host→device frames (v0.3.0):

| Frame | Device effect |
|---|---|
| `{"type":"notify","msg":"…","sound":"alert\|ack\|tap\|none","secs":8}` | banner toast on any page + chirp (`familiar_notify` agent tool) |
| `{"type":"page","slot":0\|1,"title":"…","lines":["…","…"]}` | fills Page 4 (cron, slot 0) / Page 5 (vitals, slot 1); pushed every 60 s from `plugin/feeds.py` |
| `{"type":"config","wifi":{"ssid":"…","password":"…"}}` | merges into SD `/hermes-buddy/config.json`, acks `{"ack":"config",…}`, reconnects Wi-Fi live |
| `{"type":"msgs","off":0,"total":9,"lines":["…"],"ids":["…"]}` | returns up to five history previews and their optional stable detail IDs |
| `{"type":"msg","id":"…","body":"…","role":"user|assistant|…","truncated":false}` | selected full-text detail response; may include `error:"stale"` or `error:"unavailable"` |

`event:message` frames also raise a 4 s banner toast; toasts never cover a
pending approval and are dismissed by page navigation.

The plugin registers the **`familiar_notify` tool** (toolset `familiar`,
gated on a connected device) so the agent — including cron-job turns — can
deliberately ping the desk with a banner + chirp.

Unlike `embody` (voice-gated face), the familiar reflects **all** platforms —
telegram, slack, cron, kanban workers — it's a desk companion for the whole
gateway.

### Device → host

| Device line | Plugin action |
|---|---|
| `{"cmd":"action","action":"start"}` | run first enabled action from `~/.hermes/familiar_actions.json` as a `hermes chat -q` subprocess |
| `{"cmd":"action","action":"pause"}` / `"cancel"` | SIGSTOP/SIGCONT / SIGTERM the job's process group |
| `{"cmd":"permission","decision":"once"\|"deny","id":<session_key>}` | `tools.approval.resolve_gateway_approval(session_key, decision)` — the exact call `/approve` and `/deny` make |
| `{"cmd":"msgs","off":0}` | return the selected five-entry history batch; response keeps `lines` and adds parallel `ids` |
| `{"cmd":"msg","id":"…"}` | return full retained text for the selected entry, when available |
| `{"cmd":"gesture","gesture":"shake"}` | ack event |

Permission commands must include an explicit `decision` of `once` or `deny`;
missing or malformed decisions are acknowledged without resolving the approval.

### Process safety

- The serial link starts **only in the gateway process** (`"gateway"` in
  argv). `hermes chat` subprocesses load plugins too; their hooks no-op on the
  link so they can never fight over the port.
- The port is opened with `exclusive=True` (flock) as a second fence.
- Port autodetect probes each `/dev/cu.usbmodem*` / `/dev/ttyACM*` with a
  `{"cmd":"ping"}` and accepts whatever answers JSON within 2 s (the familiar
  replies `{"ack":"ping","ok":true}` instantly). Unplug/replug is handled by
  a 3 s rescan loop.
- Hook callbacks never block on the device: frames go through a bounded
  queue that drops oldest on overflow. pyserial missing / no device is a
  quiet no-op — the plugin can never hurt the gateway.

### Config

`~/.hermes/familiar_actions.json` (shared with the bridge):

```json
{
  "serial": {"port": "", "baud": 115200},
  "actions": [
    {"id": "status_brief", "label": "Status brief", "enabled": true,
     "command": ["hermes", "chat", "-q", "Give me a concise status brief…"]}
  ]
}
```

Leave `serial.port` empty for autodetect. Only subprocess `command` actions
run in plugin mode (`# ponytail:` cron/API action types stay bridge-only until
someone needs them on-device).

### Status / debugging

- `/familiar` in any Hermes session → link status, pending approvals, job state.
- Gateway log lines are tagged `familiar` / `familiar.serial` / `familiar.actions`.
- `tests/test_familiar_plugin.py` covers hook wiring, approval flow, and job
  control with a fake link (no device needed): `python3 -m pytest tests/ -q`.

### Network and WSL audio reachability

The gateway's device TCP listener uses port 8767 and its PCM HTTP listener
uses port 8765. Under WSL or NAT, set `HERMES_ADVERTISED_HOST` in the gateway
process environment to an IPv4 address the ESP32 can route to. Route-derived
addresses can be private to WSL; configure the actual LAN route and firewall
for the host in use. Keep both listeners limited to the trusted LAN. A local
HTTP test does not prove that the board can reach the advertised address.
Store Wi-Fi credentials and any transport token only in private gateway/SD
configuration, never in the checked-in example or release metadata.

The V1 device accepts audio playback but has no microphone recording or
speech-to-text input path. Microphone capture is not part of the current
firmware contract.

## Mode 2 — standalone bridge (dev / remote API)

`scripts/hermes_serial_bridge.py` keeps the original read-only SQLite
snapshot path for status and recent messages, plus optional native API mode
(`--api-url` + `--api-key`/`API_SERVER_KEY`) where actions are real
`/v1/runs` or `/api/jobs/*` calls and run events stream in over SSE.
Use it when the gateway isn't running (firmware dev) or when the device is
attached to a different machine than Hermes.

The launchd template `scripts/com.nous.hermes-familiar-bridge.plist.template`
applies to bridge mode only — plugin mode needs no launchd (it lives and dies
with the gateway).
