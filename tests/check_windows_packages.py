"""Check unpacked Windows releases, custom client ports, and safe world saving.

Usage: python tests/check_windows_packages.py SERVER.zip CLIENT.zip
Creates a fresh temporary world, leaves diagnostics in the printed directory.
Uses local TCP 28348/28349 and test-only account credentials.
"""
from pathlib import Path
import argparse
import hashlib
import json
import os
import re
import socket
import subprocess
import tempfile
import time
import zipfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("server_zip", type=Path)
parser.add_argument("client_zip", type=Path)
args = parser.parse_args()
smoke = Path(tempfile.mkdtemp(prefix="tomenet-check-"))
print("Diagnostics:", smoke, flush=True)
for product, archive in (("server", args.server_zip), ("client", args.client_zip)):
    with zipfile.ZipFile(archive) as file:
        assert file.testzip() is None
        file.extractall(smoke)
    folder = smoke / ("TomeNET-Server" if product == "server" else "TomeNET-Client")
    for file, checksum in json.loads((folder / "SHA256SUMS.json").read_text()).items():
        assert hashlib.sha256((folder / file).read_bytes()).hexdigest() == checksum, file
    print(product + " archive and file checksums verified.", flush=True)

server = smoke / "TomeNET-Server"
client = smoke / "TomeNET-Client"
config = server / "lib/config/tomenet.cfg"
text = config.read_text()
text = re.sub(r"(?m)^GAME_PORT\s*=.*$", "GAME_PORT = 28348", text)
text = re.sub(r"(?m)^CONSOLE_PORT\s*=.*$", "CONSOLE_PORT = 28349", text)
text = re.sub(r"(?m)^GW_PORT\s*=.*$", "GW_PORT = 28400", text)
config.write_text(text)
environment = {key.upper(): value for key, value in os.environ.items()}
environment["PATH"] = environment["SYSTEMROOT"] + r"\System32;" + environment["SYSTEMROOT"]
environment["TOMENET_PATH"] = str(server / "lib")
startup = subprocess.STARTUPINFO()
startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
startup.wShowWindow = 0
output = open(smoke / "server-console.log", "w")
process = subprocess.Popen([str(server / "tomenet.server.exe")], cwd=server, env=environment,
                           stdout=output, stderr=subprocess.STDOUT, creationflags=subprocess.CREATE_NO_WINDOW)
(smoke / "server-process-id.txt").write_text(str(process.pid))
print("Isolated server started, PID", process.pid, flush=True)
test_client = None
try:
    deadline = time.monotonic() + 60
    while time.monotonic() < deadline:
        if process.poll() is not None:
            output.flush()
            raise RuntimeError("Server exited early:\n" + (smoke / "server-console.log").read_text(errors="replace"))
        try:
            with socket.create_connection(("127.0.0.1", 28349), timeout=1) as connection:
                connection.settimeout(5)
                password = re.search(r'(?m)^CONSOLE_PASSWORD\s*=\s*"([^"]+)"', text)[1]
                connection.sendall(password.encode("ascii") + b"\0\x0a")
                if connection.recv(1) == b"\x0a":
                    break
        except (OSError, TimeoutError):
            pass
        time.sleep(0.5)
    else:
        raise RuntimeError("Server did not become ready within 60 seconds.")
    print("Fresh world initialized and authenticated console status succeeded without MSYS2 on PATH.", flush=True)
    client_env = environment.copy()
    client_env.pop("TOMENET_PATH")
    # Skip interactive first-run display questions in this test copy only.
    ini = (client / "TomeNET.ini.default").read_text()
    ini = ini.replace("[Base]", "[Base]\nHintBigmap=0\nHintSound=0")
    ini = ini.replace("ForceIMEOff=0", "ForceIMEOff=1").replace("DisableNumlock=1", "DisableNumlock=0")
    ini = re.sub(r"(?m)^nick=.*$", "nick=PackSmoke", ini)
    ini = re.sub(r"(?m)^pass=.*$", "pass=SmokeTest123", ini)
    (client / "TomeNET.ini").write_text(ini)
    test_client = subprocess.Popen([str(client / "TomeNET.exe"), "-q", "-lPackSmoke", "SmokeTest123",
                                    "-p28348", "127.0.0.1"], cwd=client, env=client_env, startupinfo=startup)
    (smoke / "client-process-id.txt").write_text(str(test_client.pid))
    time.sleep(5)
    assert test_client.poll() is None, "Client exited during startup: " + str(test_client.returncode)
    log = (server / "lib/data/tomenet.log").read_text(errors="replace")
    assert "packsmoke" in log.lower(), "Client did not reach the server."
    print("Packaged client started and reached the isolated server.", flush=True)
    windows_powershell = Path(environment["SYSTEMROOT"]) / "System32/WindowsPowerShell/v1.0/powershell.exe"
    subprocess.run([str(windows_powershell), "-NoProfile", "-ExecutionPolicy", "Bypass", "-File",
                    str(server / "stop-server.ps1")], cwd=server, env=environment, check=True, timeout=70)
    process.wait(timeout=5)
    assert (server / "lib/save/server").stat().st_size > 0, "World state was not saved."
    assert (server / "lib/save/tomenet.acc").stat().st_size > 0, "Account state was not saved."
    print("Safe shutdown completed and world/account files were saved.", flush=True)
finally:
    if process.poll() is None:
        powershell = Path(environment['SYSTEMROOT']) / 'System32/WindowsPowerShell/v1.0/powershell.exe'
        subprocess.run([str(powershell), '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File',
                        str(server / 'stop-server.ps1')], cwd=server, env=environment, timeout=70)
        if process.poll() is None:
            process.terminate()
            process.wait(timeout=5)
    if test_client is not None and test_client.poll() is None:
        test_client.terminate()
        test_client.wait(timeout=5)
    output.close()
