"""Network transport tests: TCP/WS legs of SerialLink + token auth.

Real sockets on ephemeral ports, no pyserial, no device — the link thread is
never started, so nothing here can touch the real USB port. Outbound
broadcast is exercised via _net_send (what _run calls); inbound via real
client writes feeding the on_line collector.
"""
from __future__ import annotations

import json
import socket
import sys
import threading
import time
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from plugin import serial_link
from plugin.serial_link import SerialLink

TOKEN = "test-token-123"


def wait_for(cond, timeout=3.0):
    deadline = time.time() + timeout
    while time.time() < deadline:
        if cond():
            return True
        time.sleep(0.02)
    return False


def make_link():
    seen: list[dict] = []
    link = SerialLink(lambda evt, origin=None: seen.append(evt))
    return link, seen


def tcp_connect(port: int) -> socket.socket:
    s = socket.create_connection(("127.0.0.1", port), timeout=3)
    s.settimeout(3)
    return s


def readline(sock: socket.socket) -> bytes:
    buf = b""
    while not buf.endswith(b"\n"):
        chunk = sock.recv(1)
        if not chunk:
            break
        buf += chunk
    return buf


# -- TCP ------------------------------------------------------------------

def test_tcp_rejects_bad_token():
    link, seen = make_link()
    port = link.start_tcp(0, token=TOKEN)
    s = tcp_connect(port)
    s.sendall(b'{"type":"auth","token":"wrong"}\n{"cmd":"deck","i":0}\n')
    assert readline(s) == b""          # server hung up, no auth-ok
    s.close()
    assert not wait_for(lambda: seen, timeout=0.5)
    assert link._net_clients == []


def test_tcp_rejects_non_auth_first_line():
    link, seen = make_link()
    port = link.start_tcp(0, token=TOKEN)
    s = tcp_connect(port)
    s.sendall(b'{"hello":"hermes-buddy","transport":"tcp"}\n')
    assert readline(s) == b""
    s.close()
    assert link._net_clients == []


def test_tcp_auth_roundtrip():
    link, seen = make_link()
    port = link.start_tcp(0, token=TOKEN)
    s = tcp_connect(port)
    s.sendall(json.dumps({"type": "auth", "token": TOKEN}).encode() + b"\n")
    assert json.loads(readline(s)) == {"type": "auth", "ok": True}
    assert wait_for(lambda: link._net_clients)
    # outbound broadcast reaches the authed client
    link._net_send(b'{"type":"state","face":"idle"}\n')
    assert json.loads(readline(s))["type"] == "state"
    # inbound frame lands in on_line
    s.sendall(b'{"cmd":"deck","i":0}\n')
    assert wait_for(lambda: seen)
    assert seen[0] == {"cmd": "deck", "i": 0}
    s.close()
    assert wait_for(lambda: not link._net_clients)


def test_tcp_malformed_frame_recovers_and_client_can_reconnect():
    link, seen = make_link()
    port = link.start_tcp(0, token=TOKEN)

    first = tcp_connect(port)
    first.sendall(json.dumps({"type": "auth", "token": TOKEN}).encode() + b"\n")
    assert json.loads(readline(first)) == {"type": "auth", "ok": True}
    assert wait_for(lambda: len(link._net_clients) == 1)
    first.sendall(b'{"cmd":\nnot-json\n{"cmd":"deck","i":2}\n')
    assert wait_for(lambda: seen == [{"cmd": "deck", "i": 2}])
    first.close()
    assert wait_for(lambda: not link._net_clients)

    second = tcp_connect(port)
    second.sendall(json.dumps({"type": "auth", "token": TOKEN}).encode() + b"\n")
    assert json.loads(readline(second)) == {"type": "auth", "ok": True}
    assert wait_for(lambda: len(link._net_clients) == 1)
    second.sendall(b'{"cmd":"deck","i":3}\n')
    assert wait_for(lambda: seen == [{"cmd": "deck", "i": 2}, {"cmd": "deck", "i": 3}])
    second.close()
    assert wait_for(lambda: not link._net_clients)


def test_slow_network_sender_is_isolated_from_other_clients(monkeypatch):
    monkeypatch.setattr(serial_link, "_NET_SEND_QUEUE_SIZE", 1)
    link, _seen = make_link()
    blocked = threading.Event()
    release = threading.Event()
    disconnected = threading.Event()
    received = []

    def slow_write(_data):
        blocked.set()
        release.wait(timeout=2)

    slow = serial_link._queued_sender(slow_write, disconnected.set)
    link._net_add("tcp", slow, "slow-peer")
    link._net_add("tcp", received.append, "fast-peer")

    link._net_send(b"first\n")
    assert blocked.wait(timeout=1)
    link._net_send(b"queued\n")
    link._net_send(b"overflow\n")

    assert disconnected.wait(timeout=1)
    assert received == [b"first\n", b"queued\n", b"overflow\n"]
    assert wait_for(lambda: link.peers() == [("tcp", "fast-peer")])
    release.set()


def test_failed_network_write_removes_peer_and_keeps_broadcasting():
    link, _seen = make_link()
    received = []

    def broken_write(_data):
        raise OSError("peer disconnected")

    broken = serial_link._queued_sender(broken_write, lambda: None, link._net_del)
    link._net_add("tcp", broken, "broken-peer")
    link._net_add("tcp", received.append, "healthy-peer")

    link._net_send(b"fails asynchronously\n")
    assert wait_for(lambda: link.peers() == [("tcp", "healthy-peer")])
    link._net_send(b"still delivered\n")

    assert received == [b"fails asynchronously\n", b"still delivered\n"]


def test_slow_disconnect_does_not_block_broadcast(monkeypatch):
    monkeypatch.setattr(serial_link, "_NET_SEND_QUEUE_SIZE", 1)
    link, _seen = make_link()
    blocked = threading.Event()
    release_write = threading.Event()
    disconnect_entered = threading.Event()
    release_disconnect = threading.Event()
    received = []

    def slow_write(_data):
        blocked.set()
        release_write.wait(timeout=2)

    def slow_disconnect():
        disconnect_entered.set()
        release_disconnect.wait(timeout=2)

    slow = serial_link._queued_sender(slow_write, slow_disconnect, link._net_del)
    link._net_add("tcp", slow, "slow-peer")
    link._net_add("tcp", received.append, "fast-peer")
    link._net_send(b"first\n")
    assert blocked.wait(timeout=1)
    link._net_send(b"queued\n")

    started = time.monotonic()
    link._net_send(b"overflow\n")
    elapsed = time.monotonic() - started

    assert elapsed < 0.25
    assert disconnect_entered.wait(timeout=1)
    assert received == [b"first\n", b"queued\n", b"overflow\n"]
    assert wait_for(lambda: link.peers() == [("tcp", "fast-peer")])
    release_write.set()
    release_disconnect.set()


def test_welcome_snapshot_on_connect():
    # fresh clients get on_client() frames right after auth — the broadcast
    # only carries what happens after they join
    link, seen = make_link()
    link.on_client = lambda: [{"type": "deck", "buttons": []}, {"type": "state"}]
    port = link.start_tcp(0, token=TOKEN)
    s = tcp_connect(port)
    s.sendall(json.dumps({"type": "auth", "token": TOKEN}).encode() + b"\n")
    assert json.loads(readline(s))["type"] == "auth"
    assert json.loads(readline(s))["type"] == "deck"
    assert json.loads(readline(s))["type"] == "state"
    s.close()


def test_tcp_only_link_keeps_sending_state_heartbeats():
    """A device on TCP with no USB serial must stay live past the UI timeout."""
    link, _seen = make_link()
    link._heartbeat = 0.05
    link._make_heartbeat = lambda: {"type": "state", "running": 0, "waiting": 0}
    # Force the actual _run no-USB branch without probing workstation ports.
    link._connect = lambda: None
    port = link.start_tcp(0)
    s = tcp_connect(port)
    s.settimeout(0.1)
    assert wait_for(lambda: len(link._net_clients) == 1)
    link.start()
    try:
        states = []
        buf = b""
        deadline = time.time() + 3.3
        while time.time() < deadline:
            try:
                chunk = s.recv(4096)
            except socket.timeout:
                continue
            if not chunk:
                break
            buf += chunk
            while b"\n" in buf:
                line, buf = buf.split(b"\n", 1)
                frame = json.loads(line)
                if frame.get("type") == "state":
                    states.append(frame)
        assert len(states) >= 3
        assert link._ser is None
        assert len(link._net_clients) == 1
    finally:
        link.stop()
        s.close()


def test_tcp_no_token_is_open_backcompat():
    # device firmware pre-auth: no token configured -> old behavior
    link, seen = make_link()
    port = link.start_tcp(0)
    s = tcp_connect(port)
    s.sendall(b'{"hello":"hermes-buddy","transport":"tcp"}\n')
    assert wait_for(lambda: seen)
    assert seen[0]["hello"] == "hermes-buddy"
    s.close()


# -- WS -------------------------------------------------------------------

def _ws_client():
    # per-test so the TCP tests above still run where websockets is absent
    return pytest.importorskip(
        "websockets.sync.client", reason="websockets not installed (run via gateway venv)")


def test_ws_rejects_bad_token():
    ws_client = _ws_client()
    link, seen = make_link()
    port = link.start_ws(0, token=TOKEN)
    assert port
    with ws_client.connect(f"ws://127.0.0.1:{port}") as conn:
        conn.send('{"type":"auth","token":"wrong"}')
        with pytest.raises(Exception):
            conn.recv(timeout=3)
    assert link._net_clients == []


def test_ws_auth_roundtrip():
    ws_client = _ws_client()
    link, seen = make_link()
    port = link.start_ws(0, token=TOKEN)
    assert port
    with ws_client.connect(f"ws://127.0.0.1:{port}") as conn:
        conn.send(json.dumps({"type": "auth", "token": TOKEN}))
        assert json.loads(conn.recv(timeout=3)) == {"type": "auth", "ok": True}
        assert wait_for(lambda: link._net_clients)
        link._net_send(b'{"type":"state","face":"idle"}\n')
        assert json.loads(conn.recv(timeout=3))["type"] == "state"
        conn.send('{"cmd":"deck","i":0}')
        assert wait_for(lambda: seen)
        assert seen[0] == {"cmd": "deck", "i": 0}
    assert wait_for(lambda: not link._net_clients)


def test_ws_and_tcp_share_broadcast():
    ws_client = _ws_client()
    link, seen = make_link()
    tcp_port = link.start_tcp(0, token=TOKEN)
    ws_port = link.start_ws(0, token=TOKEN)
    s = tcp_connect(tcp_port)
    s.sendall(json.dumps({"type": "auth", "token": TOKEN}).encode() + b"\n")
    readline(s)  # auth ok
    with ws_client.connect(f"ws://127.0.0.1:{ws_port}") as conn:
        conn.send(json.dumps({"type": "auth", "token": TOKEN}))
        conn.recv(timeout=3)  # auth ok
        assert wait_for(lambda: len(link._net_clients) == 2)
        assert link.transport == "tcp+ws"
        link._net_send(b'{"type":"state"}\n')
        assert json.loads(readline(s))["type"] == "state"
        assert json.loads(conn.recv(timeout=3))["type"] == "state"
    s.close()
