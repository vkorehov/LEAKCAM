@echo off
rem Build the patched OrcaSlicer (C:\hw\build\OrcaSlicer, patch applied) with the VS 2022 Build Tools.
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64
cd /d C:\hw\build\OrcaSlicer\build
echo START %DATE% %TIME%
cmake --build . --config Release --target OrcaSlicer -- -m
echo EXIT %ERRORLEVEL% %DATE% %TIME%
