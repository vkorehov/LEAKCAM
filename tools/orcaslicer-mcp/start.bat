@echo off
rem OrcaSlicer MCP on port 8081 (Streamable HTTP, /mcp); starts OrcaSlicer (the MCP build) if it is not running
cd /d "%~dp0"
rem bring the server up to date first; never ask for a login (the repo pulls anonymously)
set GIT_TERMINAL_PROMPT=0
set GCM_INTERACTIVE=Never
rem the user allows these protected keys (2026-10-11): the start/end G-code, e.g. the prime line
set ORCA_MCP_ALLOW_KEYS=machine_start_gcode,machine_end_gcode
git pull --ff-only
if not exist .venv\Scripts\python.exe py -3 -m venv .venv
.venv\Scripts\python -m pip install -q -e .
tasklist /fi "imagename eq orca-slicer.exe" | find /i "orca-slicer.exe" >nul || start "" "C:\Program Files\OrcaSlicer\orca-slicer.exe"
.venv\Scripts\python serve_http.py %*
