@echo off
rem OctoPrint for the CR-10 on COM3 (http://localhost:5000): OrcaSlicer sends prints here (Print button)
rem installed in C:\hw\tools\octoprint (Python 3.13 venv, data in C:\hw\tools\octoprint\data); see README.md
title OctoPrint (CR-10, COM3)
C:\hw\tools\octoprint\.venv\Scripts\octoprint.exe --basedir C:\hw\tools\octoprint\data serve --port 5000 %*
