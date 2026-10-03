# Hermes Familiar host integration

Two integration modes, one device protocol. **Plugin mode is the product**;
the bridge is the dev/remote fallback.

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

A full state frame is also sent as a 5 s heartbeat (firmware marks the host
offline after 30 s). Daily aggregates (sessions/tokens/tools) are read from
`state.db` read-only at most once a minute.

On V1, Page 1 presents two 55 px message cards. Tapping a card opens that
entry's retained text; vertical swipes browse one entry at a time through
five-entry history batches, and tapping returns to the prior card position.
Page 0's latest-response Markdown viewer remains available separately.

The plugin adds optional `entry_ids` beside the existing state `entries` and
`ids` beside the existing history `lines`. A selected entry is requested with
`{"cmd":"msg","id":"…"}` and returned as
`{"type":"msg","id":"…","body":"…","role":"user|assistant|…","truncated":false}`.
The ID arrays preserve the existing preview fields for older firmware. The
host retains at most 40 detail records, with each stored body capped at 16 KiB
of UTF-8. A detail response is limited to 3200 UTF-8 body bytes and a 4095-byte
complete escaped JSON frame; the latter can shorten non-ASCII text further.
The `truncated` flag reports clipping. A stale or unknown ID returns an empty
body with `error:"stale"`; a preview-only legacy record returns
`error:"unavailable"` so its compact preview is not presented as full text.

Current rollout: the V1 messages/emoji image is flashed and hash-verified as
`85029efd3a33f3f547cc8c844e34224f9c463ad1182b3f8aa58f12f1e5bfe90e`. This
checkout's host plugin changes have not been deployed or live-updated in the
gateway, so the connected device still receives preview-only records and the
selected-message detail path is not yet available end to end. The existing
Page 0 Markdown viewer remains in the image, though the user reports that
Markdown formatting broke on the physical board. Treat this as an observed
regression, not a formatting pass; do not infer a cause or investigate/fix it
until asked.

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
