from pathlib import Path
import subprocess


def test_network_transport_selection(tmp_path):
    root = Path(__file__).resolve().parents[1]
    source = root / "tests/native/test_network_policy.cpp"
    binary = tmp_path / "network-policy-test"
    subprocess.run(
        ["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror", str(source), "-o", str(binary)],
        check=True,
    )
    subprocess.run([str(binary)], check=True)
