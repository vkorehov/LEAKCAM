"""serve_http.restart: a scheduled task ends only the server's own console pids and reopens start.bat."""
import importlib.util
import os
import subprocess
from pathlib import Path

import pytest
from mcp.server.mcpserver import MCPServer

spec = importlib.util.spec_from_file_location("serve_http", Path(__file__).parent.parent / "serve_http.py")
serve_http = importlib.util.module_from_spec(spec)
spec.loader.exec_module(serve_http)


@pytest.mark.asyncio
async def test_restart_ends_own_console_only(monkeypatch, tmp_path):
    me = os.getpid()
    table = f"{me} 20 python.exe\n20 30 cmd.exe\n30 1 explorer.exe\n40 20 orca-slicer.exe\n"
    calls = []

    def run(cmd, **kw):
        calls.append(cmd)
        return subprocess.CompletedProcess(cmd, 0, table if cmd[0] == "powershell" else "SUCCESS", "")
    monkeypatch.setattr(serve_http.subprocess, "run", run)
    monkeypatch.setattr(serve_http.tempfile, "gettempdir", lambda: str(tmp_path))
    mcp = MCPServer("t")
    serve_http.add_restart(mcp)
    assert "restart" in [t.name for t in await mcp.list_tools()]
    text = str(await mcp.call_tool("restart", {}))
    assert f"[{me}, 20]" in text
    bat = (tmp_path / "orcaslicer_mcp_restart.bat").read_text()
    assert f"taskkill /F /PID {me} /PID 20 " in bat and "/PID 40" not in bat and "cmd /k start.bat" in bat
    assert [c[1] for c in calls if c[0] == "schtasks"] == ["/create", "/run"]
