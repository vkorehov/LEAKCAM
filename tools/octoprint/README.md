# OctoPrint for the CR-10

OrcaSlicer cannot drive a USB-serial printer itself. OctoPrint on the Windows PC holds COM3 and streams the
G-code. The OrcaSlicer printer preset "CR-10 Marlin 2 ABL 0.4" points at it, so OrcaSlicer's **Print** button
uploads and starts the job.

- `start.bat` runs OctoPrint on http://localhost:5000. There is no firewall rule, so it is reachable from this PC only.
- Installed in `C:\hw\tools\octoprint` (not in git):
  - OctoPrint 1.11.8 in a Python 3.13 venv made with `uv`.
  - Its `netifaces` dependency is compiled, which needs the Microsoft C++ Build Tools (installed).
- Data and config are in `C:\hw\tools\octoprint\data`:
  - serial COM3, 115200, auto-connect;
  - printer profile CR-10 300 x 300 x 400 with a heated bed;
  - user `vlad`;
  - an application key "OrcaSlicer".

While OctoPrint is connected it owns COM3. Disconnect it in OctoPrint (or stop it) before flashing the firmware
(`tools/cr10-marlin`) or sending serial commands from elsewhere.

Reinstall:

```
py -3 -m pip install --user uv
uv venv C:\hw\tools\octoprint\.venv --python 3.13 --seed
uv pip install --python C:\hw\tools\octoprint\.venv\Scripts\python.exe octoprint
```
