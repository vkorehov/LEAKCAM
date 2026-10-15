# OrcaSlicer Remote API patch

Our additions to the Remote API of the MaxEllis OrcaSlicer fork, which the MCP tools need:

- `GET/PUT /api/v1/plate/custom_gcode`: layer markers (custom G-code or a pause at a height): `set_layer_gcode`, `get_layer_gcode`
- `POST/DELETE /api/v1/objects/{id}/support_volume`: support blocker/enforcer boxes: `add_support_volume`, `delete_support_volumes`
- `POST/DELETE /api/v1/objects/{id}/modifier`: modifier volumes from an STL in plate mm, with their own settings: `add_modifier`, `delete_modifiers`
- `POST /api/v1/app/quit`: close OrcaSlicer without the save question (the project is discarded); `install_api_patch.bat` uses it

`remote-api.patch` applies to `v2.4.2-mcp.13`. Build from the fork's newest release each time:

```
cd C:\hw\build\OrcaSlicer
git fetch --tags origin
git checkout -f <newest v*-mcp.N tag>
git apply C:\hw\LEAKCAM\tools\orcaslicer-mcp\orca-patch\remote-api.patch
C:\hw\LEAKCAM\tools\orcaslicer-mcp\orca-patch\build_api_patch.bat     (VS 2022 Build Tools; first build of a new tag: build_release_vs2022.bat slicer)
C:\hw\LEAKCAM\tools\orcaslicer-mcp\orca-patch\install_api_patch.bat   (-> build\OrcaSlicer\orca-slicer.exe, the only OrcaSlicer on the PC)
```

`install_api_patch.bat` closes OrcaSlicer through `POST /app/quit` and starts it again. Never kill it
(taskkill /F): it leaves its backup behind and the next start asks to restore the project.
