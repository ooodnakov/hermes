import json
import os
from pathlib import Path
import pty
import select
import subprocess
import sys
import time

from scripts.capture_v1_usb_raw import LineObserver


def test_line_observer_preserves_reset_detection_across_disconnects():
    observer = LineObserver()
    assert observer.feed(b'{"type":"diagnostic","uptime_ms":9000}\r\n') == [
        {
            "line": '{"type":"diagnostic","uptime_ms":9000}',
            "json": {"type": "diagnostic", "uptime_ms": 9000},
        }
    ]

    assert observer.feed(b'{"type":"diagnostic","uptime_ms":') == []
    assert observer.disconnect() == {
        "line": '{"type":"diagnostic","uptime_ms":',
        "partial": True,
    }
    events = observer.feed(b'{"type":"diagnostic","uptime_ms":800}\n')
    assert events[0]["uptime_reset"] is True
    assert events[0]["previous_uptime_ms"] == 9000

    observer.feed(b'{"type":"diagnostic","uptime_ms":4294967280}\n')
    wrapped = observer.feed(b'{"type":"diagnostic","uptime_ms":20}\n')[0]
    assert wrapped["uptime_wrap"] is True
    assert wrapped["uptime_reset"] is False


def test_line_observer_bounds_unterminated_annotation_memory():
    observer = LineObserver(max_line_bytes=8)
    assert observer.feed(b"a" * 100) == []
    assert len(observer.pending) <= 8
    assert observer.disconnect() == {"line_omitted": "oversized_partial", "bytes": 100}


def test_capture_keeps_raw_boot_bytes_and_records_probe_and_uptime(tmp_path):
    master, slave = pty.openpty()
    port = os.ttyname(slave)
    base = tmp_path / "raw-capture"
    script = Path(__file__).parents[1] / "scripts" / "capture_v1_usb_raw.py"
    process = subprocess.Popen(
        [sys.executable, str(script), "--port", port, "--duration", "1.0",
         "--probe", "--output", str(base)],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    try:
        probe = bytearray()
        end = time.monotonic() + 2
        while b'{"cmd":"ping"}\n' not in probe or b'{"cmd":"touchdiag"}\n' not in probe:
            remaining = end - time.monotonic()
            assert remaining > 0, "capture did not send its requested harmless probes"
            readable, _, _ = select.select([master], [], [], remaining)
            assert readable, "capture did not send its requested harmless probes"
            probe.extend(os.read(master, 128))
        assert b'{"cmd":"ping"}\n' in probe
        assert b'{"cmd":"touchdiag"}\n' in probe

        payload = (
            b"ROM boot banner\r\n"
            b'{"type":"diagnostic","uptime_ms":12000}\r\n'
            b'{"type":"diagnostic","uptime_ms":700}\r\n'
        )
        os.write(master, payload)
        _, stderr = process.communicate(timeout=3)
        assert process.returncode == 0, stderr.decode(errors="replace")
        assert base.with_suffix(".bin").read_bytes() == payload

        records = [json.loads(line) for line in base.with_suffix(".jsonl").read_text().splitlines()]
        assert any(record.get("event") == "probe_sent" for record in records)
        line_events = [record for record in records if record.get("event") == "line"]
        assert line_events[0]["line"] == "ROM boot banner"
        assert any(record.get("uptime_reset") is True for record in line_events)
    finally:
        if process.poll() is None:
            process.kill()
            process.wait()
        os.close(master)
        os.close(slave)


def test_passive_capture_reconnects_without_joining_partial_lines(tmp_path):
    master1, slave1 = pty.openpty()
    master2, slave2 = pty.openpty()
    link = tmp_path / "usb-serial"
    link.symlink_to(os.ttyname(slave1))
    base = tmp_path / "reconnect"
    script = Path(__file__).parents[1] / "scripts" / "capture_v1_usb_raw.py"
    process = subprocess.Popen(
        [sys.executable, str(script), "--port", str(link), "--duration", "2.2",
         "--reconnect-delay", "0.05", "--output", str(base)],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )

    def records():
        path = base.with_suffix(".jsonl")
        try:
            return [json.loads(line) for line in path.read_text().splitlines()]
        except FileNotFoundError:
            return []

    def wait_until(predicate, timeout=2):
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            current = records()
            if predicate(current):
                return current
            time.sleep(0.02)
        raise AssertionError("capture did not reach expected serial state")

    try:
        wait_until(lambda rows: any(row.get("event") == "connected" for row in rows))
        readable, _, _ = select.select([master1], [], [], 0.1)
        assert not readable, "passive capture unexpectedly wrote to the serial port"

        first = b'{"type":"diagnostic","uptime_ms":12000}\r\n{"type":"diagnostic"'
        os.write(master1, first)
        time.sleep(0.08)
        os.close(master1)
        master1 = -1
        link.unlink()
        link.symlink_to(os.ttyname(slave2))
        connected = wait_until(lambda rows: sum(row.get("event") == "connected" for row in rows) >= 2)
        second = b'{"type":"diagnostic","uptime_ms":700}\r\n'
        os.write(master2, second)
        _, stderr = process.communicate(timeout=4)
        assert process.returncode == 0, stderr.decode(errors="replace")

        assert base.with_suffix(".bin").read_bytes() == first + second
        rows = records()
        assert sum(row.get("event") == "connected" for row in rows) >= 2
        assert any(row.get("event") == "disconnected" for row in rows)
        partials = [row for row in rows if row.get("partial")]
        assert partials and partials[0]["line"].endswith('{"type":"diagnostic"')
        assert any(row.get("uptime_reset") is True for row in rows)
    finally:
        if process.poll() is None:
            process.kill()
            process.wait()
        if master1 >= 0:
            os.close(master1)
        os.close(slave1)
        os.close(master2)
        os.close(slave2)


def test_capture_refuses_to_append_to_existing_evidence(tmp_path):
    base = tmp_path / "existing"
    raw = base.with_suffix(".bin")
    index = base.with_suffix(".jsonl")
    raw.write_bytes(b"prior raw evidence")
    index.write_text("prior index\n")
    script = Path(__file__).parents[1] / "scripts" / "capture_v1_usb_raw.py"
    result = subprocess.run(
        [sys.executable, str(script), "--duration", "0.1", "--output", str(base)],
        capture_output=True,
        text=True,
        timeout=2,
    )
    assert result.returncode == 2
    assert raw.read_bytes() == b"prior raw evidence"
    assert index.read_text() == "prior index\n"
