@echo off
rem SolidWorks 2026 MCP server on port 8000 (Streamable HTTP, /mcp)
cd /d "%~dp0"
py -3 solidworks_mcp.py %*
