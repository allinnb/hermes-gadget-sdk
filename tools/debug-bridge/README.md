# StopWatch Debug Bridge

A lightweight HTTP-to-serial bridge that lets a remote server (e.g. the Hermes
gateway on a VM or another machine) send console commands to a USB-connected
M5Stack StopWatch and read back the output.

## Requirements

- Python 3.9+
- `pyserial` (`pip install pyserial`)
- Windows machine with StopWatch on a COM port (default `COM9`)
- [Tailscale](https://tailscale.com/) — the bridge binds only to the Tailscale
  IP so it is never exposed to the public internet

## Quick start

```powershell
# Open a PowerShell window and run:
.\start-bridge.ps1
# You will be prompted for a Bearer token (input is hidden).
# The bridge starts and prints: listening on <tailscale-ip>:18901, serial=COM9
```

Keep that window open while debugging.

## API

All endpoints require `Authorization: Bearer <token>`.

### `GET /health`

Returns `{"ok": true, "serial": "COM9"}` when the bridge is running and the
serial port is reachable.

### `POST /cmd`

Send a console command and receive its output.

**Request body (JSON):**

```json
{"cmd": "status", "timeout": 10}
```

| Field     | Type   | Default | Description                        |
|-----------|--------|---------|------------------------------------|
| `cmd`     | string | —       | Console command (single-line only) |
| `timeout` | int    | 15      | Max seconds to wait for output     |

**Response body (JSON):**

```json
{"ok": true, "cmd": "status", "output": "..."}
```

### Example

```bash
# From the Hermes VM (replace TOKEN and IP):
curl -X POST \
  -H "Authorization: Bearer TOKEN" \
  -H "Content-Type: application/json" \
  -d '{"cmd":"say hello","timeout":30}' \
  http://100.110.7.45:18901/cmd
```

## Security notes

- Bind address is the Tailscale IP only — never `0.0.0.0`.
- Unauthenticated requests return `401`.
- Token must be at least 32 characters; `start-bridge.ps1` prompts for it
  with hidden input and never writes it to disk.
- Request body is limited to 4 KB; only single-line commands are accepted.

## Files

| File | Description |
|------|-------------|
| `stopwatch-bridge.py` | Bridge server (Python, stdlib + pyserial) |
| `start-bridge.ps1` | Launcher — prompts for token, validates env, opens bridge |
