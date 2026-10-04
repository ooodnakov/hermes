#!/usr/bin/env python3
"""Capture V1 USB serial bytes and annotate JSON diagnostics without resetting the board.

The raw `.bin` file is authoritative and byte-for-byte. The `.jsonl` file records
connection boundaries, byte offsets, timestamps, decoded line observations, and
device uptime rollbacks. The passive mode never writes to the port or controls
DTR/RTS; opening a tty can still cause driver-specific modem-line behavior.
"""

import argparse
from datetime import datetime, timezone
import glob
import json
import os
from pathlib import Path
import select
import sys
import termios
import time
import tty


class LineObserver:
    """Observe newline-delimited JSON without modifying or dropping raw bytes."""

    def __init__(self, max_line_bytes=65536):
        self.pending = bytearray()
        self.last_uptime_ms = None
        self.max_line_bytes = max_line_bytes
        self.discarding = False
        self.discarded_bytes = 0

    def feed(self, data: bytes):
        events = []
        for value in data:
            if value == 0x0A:
                if self.discarding:
                    events.append({"line_omitted": "oversized", "bytes": self.discarded_bytes})
                    self.discarding = False
                    self.discarded_bytes = 0
                    self.pending.clear()
                    continue
                line = bytes(self.pending).rstrip(b"\r")
                self.pending.clear()
                events.append(self._parse_line(line))
            elif not self.discarding:
                if len(self.pending) < self.max_line_bytes:
                    self.pending.append(value)
                else:
                    self.discarding = True
                    self.discarded_bytes = len(self.pending) + 1
                    self.pending.clear()
            else:
                self.discarded_bytes += 1
        return events

    def _parse_line(self, line):
        event = {"line": line.decode("utf-8", errors="replace")}
        try:
            payload = json.loads(line)
        except (json.JSONDecodeError, UnicodeDecodeError):
            payload = None
        if isinstance(payload, dict):
            event["json"] = payload
            uptime = payload.get("uptime_ms")
            if isinstance(uptime, int) and not isinstance(uptime, bool) and uptime >= 0:
                if self.last_uptime_ms is not None and uptime < self.last_uptime_ms:
                    wrapped = self.last_uptime_ms > 0xF0000000 and uptime < 0x0FFFFFFF
                    event["uptime_reset"] = not wrapped
                    event["uptime_wrap"] = wrapped
                    event["previous_uptime_ms"] = self.last_uptime_ms
                self.last_uptime_ms = uptime
        return event

    def disconnect(self):
        """Return any incomplete observation and reset line state between boots."""
        partial = None
        if self.discarding:
            partial = {"line_omitted": "oversized_partial", "bytes": self.discarded_bytes}
        elif self.pending:
            partial = {"line": bytes(self.pending).decode("utf-8", errors="replace"), "partial": True}
        self.pending.clear()
        self.discarding = False
        self.discarded_bytes = 0
        return partial


def utc_now():
    return datetime.now(timezone.utc).isoformat(timespec="milliseconds").replace("+00:00", "Z")


def available_port(pattern):
    matches = sorted(glob.glob(pattern)) if any(char in pattern for char in "*?[") else [pattern]
    for candidate in matches:
        if os.path.exists(candidate):
            return candidate
    return None


def open_tty(path, baud, allow_writes=False):
    """Open a tty for reading and configure raw mode without modem-control ioctls."""
    access = os.O_RDWR if allow_writes else os.O_RDONLY
    fd = os.open(path, access | os.O_NOCTTY | os.O_NONBLOCK)
    try:
        attrs = termios.tcgetattr(fd)
        tty.setraw(fd, termios.TCSANOW)
        attrs = termios.tcgetattr(fd)
        speed = getattr(termios, f"B{baud}", None)
        if speed is None:
            raise ValueError(f"unsupported baud rate: {baud}")
        attrs[4] = speed
        attrs[5] = speed
        termios.tcsetattr(fd, termios.TCSANOW, attrs)
        return fd
    except BaseException:
        os.close(fd)
        raise


def log_event(log, started, kind, **fields):
    event = {
        "event": kind,
        "utc": utc_now(),
        "elapsed_s": round(time.monotonic() - started, 6),
        **fields,
    }
    log.write(json.dumps(event, ensure_ascii=False) + "\n")
    log.flush()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="/dev/ttyACM0", help="tty path or glob (default: /dev/ttyACM0)")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--duration", type=float, default=0, help="capture duration in seconds; 0 means until Ctrl-C")
    parser.add_argument("--reconnect-delay", type=float, default=0.5)
    parser.add_argument("--output", type=Path, help="output base path; creates BASE.bin and BASE.jsonl")
    parser.add_argument("--probe", action="store_true", help="send harmless ping and touchdiag requests once per connection")
    args = parser.parse_args()
    if args.duration < 0 or args.reconnect_delay < 0 or args.baud <= 0:
        parser.error("duration/reconnect-delay must be nonnegative and baud must be positive")

    stamp = datetime.now().strftime("%Y%m%d-%H%M%S")
    default_dir = Path.home() / ".local/state/espherm" / datetime.now().date().isoformat()
    base = args.output or (default_dir / f"v1-usb-raw-{stamp}")
    base.parent.mkdir(parents=True, exist_ok=True)
    raw_path = base.with_suffix(".bin")
    log_path = base.with_suffix(".jsonl")
    started = time.monotonic()
    deadline = started + args.duration if args.duration else None
    observer = LineObserver()
    offset = 0
    connection_number = 0
    fd = None
    current_port = None

    try:
        if raw_path.exists() or log_path.exists():
            raise FileExistsError(f"output already exists; choose a new --output base: {base}")
        with raw_path.open("xb") as raw, log_path.open("x", encoding="utf-8") as log:
            log_event(log, started, "capture_start", port=args.port, baud=args.baud,
                      raw_file=str(raw_path), offset=offset, active_probe=args.probe)
            print(f"Capturing {args.port}; raw bytes: {raw_path}; index: {log_path}", file=sys.stderr)
            try:
                while deadline is None or time.monotonic() < deadline:
                    if fd is None:
                        candidate = available_port(args.port)
                        if candidate is None:
                            time.sleep(min(args.reconnect_delay, max(0, deadline - time.monotonic()))
                                       if deadline is not None else args.reconnect_delay)
                            continue
                        try:
                            fd = open_tty(candidate, args.baud, allow_writes=args.probe)
                            current_port = candidate
                            connection_number += 1
                            log_event(log, started, "connected", port=current_port,
                                      connection=connection_number, offset=offset)
                            if args.probe:
                                for request in (b'{"cmd":"ping"}\n', b'{"cmd":"touchdiag"}\n'):
                                    os.write(fd, request)
                                log_event(log, started, "probe_sent", connection=connection_number,
                                          commands=["ping", "touchdiag"])
                        except (OSError, ValueError, termios.error) as exc:
                            log_event(log, started, "open_failed", port=candidate, error=str(exc), offset=offset)
                            if fd is not None:
                                os.close(fd)
                                fd = None
                            time.sleep(args.reconnect_delay)
                            continue

                    try:
                        readable, _, _ = select.select([fd], [], [], 0.25)
                        if not readable:
                            continue
                        chunk = os.read(fd, 4096)
                        if not chunk:
                            raise OSError("serial device reached EOF")
                    except (OSError, ValueError) as exc:
                        log_event(log, started, "disconnected", port=current_port,
                                  connection=connection_number, error=str(exc), offset=offset)
                        partial = observer.disconnect()
                        if partial is not None:
                            log_event(log, started, "line", port=current_port,
                                      connection=connection_number, **partial)
                        os.close(fd)
                        fd = None
                        current_port = None
                        time.sleep(args.reconnect_delay)
                        continue

                    chunk_offset = offset
                    raw.write(chunk)
                    raw.flush()
                    log_event(log, started, "bytes", port=current_port, connection=connection_number,
                              offset=chunk_offset, length=len(chunk))
                    offset += len(chunk)
                    for observed in observer.feed(chunk):
                        log_event(log, started, "line", port=current_port,
                                  connection=connection_number, **observed)
            finally:
                partial = observer.disconnect()
                if partial is not None:
                    log_event(log, started, "line", port=current_port,
                              connection=connection_number, **partial)
    except KeyboardInterrupt:
        pass
    except OSError as exc:
        print(f"Capture failed: {exc}", file=sys.stderr)
        return 2
    finally:
        if fd is not None:
            os.close(fd)
    print(f"Saved {offset} bytes to {raw_path}; annotations in {log_path}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
