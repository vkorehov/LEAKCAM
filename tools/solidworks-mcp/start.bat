@echo off
rem SolidWorks 2026 MCP server on port 8000 (Streamable HTTP, /mcp)
cd /d "%~dp0"
rem bring the server up to date first; never ask for a login (the repo pulls anonymously)
set GIT_TERMINAL_PROMPT=0
set GCM_INTERACTIVE=Never
git pull --ff-only
rem the libraries the pulled server needs (mcp 2.x since 2026-10-28)
py -3 -m pip install -q -r requirements.txt
py -3 solidworks_mcp.py %*
