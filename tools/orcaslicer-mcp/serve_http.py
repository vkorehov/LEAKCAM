"""OrcaSlicer MCP over Streamable HTTP, for MCP clients on other machines (the package itself only speaks stdio).

  py serve_http.py [--host 0.0.0.0] [--port 8081] [--allow-host NAME]
  (Claude Code: claude mcp add --transport http orcaslicer-mcp http://<this PC>:8081/mcp)

OrcaSlicer (the MCP build) must run with Preferences > Remote API enabled. Its token comes from ORCA_API_TOKEN or
from orca_api_token.txt next to this file (not in git). Anyone who can reach the port can drive the slicer: keep it on
the LAN.
"""
import argparse
import asyncio
import contextlib
import os
import socket
import subprocess
import tempfile

from mcp.server.transport_security import TransportSecuritySettings

HERE = os.path.dirname(os.path.abspath(__file__))


def own_names():
    """this PC's hostname and IPv4 addresses: from the hostname, and the default-route address"""
    host = socket.gethostname()
    names = {"localhost", "127.0.0.1", host}
    names |= {ai[4][0] for ai in socket.getaddrinfo(host, None, socket.AF_INET)}
    with contextlib.suppress(OSError), socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as u:
        u.connect(("192.0.2.1", 9))           # sends nothing: only picks the outgoing interface
        names.add(u.getsockname()[0])
    return names



def own_console():
    """this server's pids up to start.bat's cmd.exe (python, cmd): only these, never OrcaSlicer (start.bat
    starts it with start, so it is a child of that cmd, not an ancestor, and it keeps running)"""
    out = subprocess.run(["powershell", "-NoProfile", "-Command",
                          "Get-CimInstance Win32_Process | % { '{0} {1} {2}' -f $_.ProcessId,$_.ParentProcessId,$_.Name }"],
                         capture_output=True, text=True, timeout=60).stdout
    procs = {int(p): (int(pp), n.lower()) for p, pp, n in (l.split(" ", 2) for l in out.splitlines() if l.strip())}
    pids, pid = [], os.getpid()
    while pid in procs and procs[pid][1] in ("python.exe", "py.exe", "cmd.exe"):
        pids.append(pid)
        pid = procs[pid][0]
    return pids


def add_restart(mcp):
    from mcp.server.mcpserver.exceptions import ToolError

    @mcp.tool()
    async def restart() -> str:
        """Restart this MCP server in a new console window in ~3 s: start.bat pulls the repo and reinstalls the
        package first (use it after pushing a server fix). Runs as a Windows scheduled task, so it is not killed
        with the server; OrcaSlicer and its plate stay open. Calls fail until the server is back (~15 s); the
        client reconnects by itself."""
        pids = await asyncio.to_thread(own_console)
        bat = os.path.join(tempfile.gettempdir(), "orcaslicer_mcp_restart.bat")
        with open(bat, "w") as f:
            f.write("@echo off\r\ntimeout /t 3 /nobreak >nul\r\n"
                    f"taskkill /F {' '.join(f'/PID {p}' for p in pids)} >nul 2>&1\r\n"
                    f'start "orcaslicer-mcp" /D "{HERE}" cmd /k start.bat\r\n')
        task = ["/tn", "orcaslicer-mcp-restart"]
        for args in (["/create", *task, "/tr", bat, "/sc", "once", "/st", "00:00", "/f"], ["/run", *task]):
            r = await asyncio.to_thread(subprocess.run, ["schtasks", *args], capture_output=True, text=True, timeout=60)
            if r.returncode:
                raise ToolError(f"schtasks {args[0]} failed: {(r.stderr or r.stdout).strip()}")
        return f"restarting in ~3 s (ending pids {pids}); start.bat pulls and reinstalls first"


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--host", default="0.0.0.0")
    ap.add_argument("--port", type=int, default=8081)
    ap.add_argument("--allow-host", action="append", default=[], help="another name clients use for this PC")
    a = ap.parse_args()
    token_file = os.path.join(HERE, "orca_api_token.txt")
    if not os.environ.get("ORCA_API_TOKEN") and os.path.exists(token_file):
        os.environ["ORCA_API_TOKEN"] = open(token_file, encoding="utf-8").read().strip()
    if not os.environ.get("ORCA_API_TOKEN"):
        raise SystemExit(f"no Remote API token: put it (OrcaSlicer Preferences > Remote API) into {token_file}")
    from orcaslicer_mcp.server import mcp     # after the token is set
    add_restart(mcp)
    names = own_names() | set(a.allow_host)
    print("accepting Host:", ", ".join(sorted(names)), flush=True)
    print(f"OrcaSlicer MCP on http://{a.host}:{a.port}/mcp", flush=True)
    mcp.run(transport="streamable-http", host=a.host, port=a.port, transport_security=TransportSecuritySettings(
        enable_dns_rebinding_protection=True, allowed_hosts=sorted(f"{n}:{a.port}" for n in names)))


if __name__ == "__main__":
    main()
