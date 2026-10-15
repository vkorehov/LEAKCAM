@echo off
rem Install the build into build\OrcaSlicer and start it again.
rem OrcaSlicer is closed through the API (POST /app/quit): a killed one asks to restore its project at the next start.
powershell -NoProfile -Command "$t=(Get-Content C:\hw\LEAKCAM\tools\orcaslicer-mcp\orca_api_token.txt).Trim(); try { Invoke-RestMethod -Method Post -Uri http://127.0.0.1:13130/api/v1/app/quit -Headers @{'X-Api-Token'=$t} | Out-Null } catch {}; Wait-Process orca-slicer -Timeout 60 -ErrorAction SilentlyContinue"
tasklist /fi "imagename eq orca-slicer.exe" | find /i "orca-slicer.exe" >nul && (echo OrcaSlicer is still running & exit /b 1)
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul
cd /d C:\hw\build\OrcaSlicer\build
cmake --build . --target install --config Release
echo EXIT %ERRORLEVEL%
start "" "C:\hw\build\OrcaSlicer\build\OrcaSlicer\orca-slicer.exe"
