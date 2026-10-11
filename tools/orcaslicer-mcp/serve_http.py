"""OrcaSlicer MCP over Streamable HTTP, for MCP clients on other machines (the package itself only speaks stdio).

  py serve_http.py [--host 0.0.0.0] [--port 8081] [--allow-host NAME]
  (Claude Code: claude mcp add --transport http orcaslicer-mcp http://<this PC>:8081/mcp)

OrcaSlicer (the MCP build) must run with Preferences > Remote API enabled. Its token comes from ORCA_API_TOKEN or
from orca_api_token.txt next to this file (not in git). Anyone who can reach the port can drive the slicer: keep it on
the LAN.
"""
import argparse
import contextlib
import os
import socket

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
    names = own_names() | set(a.allow_host)
    print("accepting Host:", ", ".join(sorted(names)), flush=True)
    print(f"OrcaSlicer MCP on http://{a.host}:{a.port}/mcp", flush=True)
    mcp.run(transport="streamable-http", host=a.host, port=a.port, transport_security=TransportSecuritySettings(
        enable_dns_rebinding_protection=True, allowed_hosts=sorted(f"{n}:{a.port}" for n in names)))


if __name__ == "__main__":
    main()
