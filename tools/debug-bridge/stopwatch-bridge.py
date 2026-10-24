#!/usr/bin/env python3
"""StopWatch 调试桥：HTTP -> USB 串口，仅监听 Tailscale 地址。"""

from __future__ import annotations

import hmac
import json
import os
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from typing import Any

import serial

LISTEN_HOST = os.environ.get("BRIDGE_HOST", "100.110.7.45")
LISTEN_PORT = int(os.environ.get("BRIDGE_PORT", "18901"))
SERIAL_PORT = os.environ.get("WATCH_SERIAL", "COM9")
BAUD = 115200
TOKEN = os.environ.get("BRIDGE_TOKEN", "")
CMD_TIMEOUT = 30
MAX_TIMEOUT = 120
MAX_BODY_BYTES = 4096

serial_lock = threading.Lock()


def send_serial(cmd: str, timeout: int = CMD_TIMEOUT) -> str:
    """向 StopWatch 发送一条控制台命令并返回串口输出。"""
    command = cmd.strip()
    if not command:
        raise ValueError("cmd 不能为空")
    if "\r" in command or "\n" in command:
        raise ValueError("cmd 必须是单行命令")

    timeout = max(1, min(int(timeout), MAX_TIMEOUT))
    with serial_lock:
        with serial.Serial(SERIAL_PORT, BAUD, timeout=1) as ser:
            time.sleep(0.3)
            ser.reset_input_buffer()
            ser.write((command + "\n").encode("utf-8"))
            ser.flush()

            deadline = time.monotonic() + timeout
            quiet_since: float | None = None
            output: list[str] = []
            while time.monotonic() < deadline:
                raw = ser.readline()
                if raw:
                    output.append(raw.decode("utf-8", errors="replace").rstrip("\r\n"))
                    quiet_since = None
                    continue

                now = time.monotonic()
                if quiet_since is None:
                    quiet_since = now
                elif now - quiet_since > 2.0:
                    break

            return "\n".join(output)


class Handler(BaseHTTPRequestHandler):
    server_version = "StopWatchBridge/1.0"

    def log_message(self, _format: str, *args: Any) -> None:
        return

    def _authorized(self) -> bool:
        supplied = self.headers.get("Authorization", "")
        expected = f"Bearer {TOKEN}"
        return bool(TOKEN) and hmac.compare_digest(supplied, expected)

    def _send_json(self, status: int, payload: dict[str, Any]) -> None:
        body = json.dumps(payload, ensure_ascii=False).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def _require_auth(self) -> bool:
        if self._authorized():
            return True
        self._send_json(401, {"ok": False, "error": "unauthorized"})
        return False

    def do_GET(self) -> None:
        if self.path != "/health":
            self._send_json(404, {"ok": False, "error": "not found"})
            return
        if not self._require_auth():
            return
        self._send_json(200, {"ok": True, "serial": SERIAL_PORT})

    def do_POST(self) -> None:
        if self.path != "/cmd":
            self._send_json(404, {"ok": False, "error": "not found"})
            return
        if not self._require_auth():
            return

        try:
            content_length = int(self.headers.get("Content-Length", "0"))
            if content_length <= 0 or content_length > MAX_BODY_BYTES:
                self._send_json(413, {"ok": False, "error": "invalid body size"})
                return

            request = json.loads(self.rfile.read(content_length))
            if not isinstance(request, dict):
                raise ValueError("请求体必须是 JSON 对象")

            cmd = request.get("cmd", "")
            if not isinstance(cmd, str):
                raise ValueError("cmd 必须是字符串")
            timeout = max(1, min(int(request.get("timeout", CMD_TIMEOUT)), MAX_TIMEOUT))
            output = send_serial(cmd, timeout)
            self._send_json(200, {"ok": True, "cmd": cmd, "output": output})
        except (ValueError, TypeError, json.JSONDecodeError) as exc:
            self._send_json(400, {"ok": False, "error": str(exc)})
        except serial.SerialException as exc:
            self._send_json(503, {"ok": False, "error": f"serial unavailable: {exc}"})
        except Exception as exc:  # 保持桥可用，同时不给调用方返回 traceback。
            self._send_json(500, {"ok": False, "error": str(exc)})


def main() -> None:
    if len(TOKEN) < 32:
        raise SystemExit("BRIDGE_TOKEN 必须设置为至少 32 个字符")

    server = ThreadingHTTPServer((LISTEN_HOST, LISTEN_PORT), Handler)
    print(
        f"listening on {LISTEN_HOST}:{LISTEN_PORT}, serial={SERIAL_PORT}",
        flush=True,
    )
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()


if __name__ == "__main__":
    main()
