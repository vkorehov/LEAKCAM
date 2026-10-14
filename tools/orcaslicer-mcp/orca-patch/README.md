# OrcaSlicer Remote API patch

Our additions to the Remote API of the MaxEllis OrcaSlicer fork, which the MCP tools need:

- `GET/PUT /api/v1/plate/custom_gcode`: layer markers (custom G-code or a pause at a height): `set_layer_gcode`, `get_layer_gcode`
- `POST/DELETE /api/v1/objects/{id}/support_volume`: support blocker/enforcer boxes: `add_support_volume`, `delete_support_volumes`

`remote-api.patch` applies to `v2.4.2-mcp.13`. Build from the fork's newest release each time:

```
cd C:\hw\build\OrcaSlicer
git fetch --tags origin
git checkout -f <newest v*-mcp.N tag>
git apply C:\hw\LEAKCAM\tools\orcaslicer-mcp\orca-patch\remote-api.patch
build_api_patch.bat      (VS 2022 Build Tools; first build of a new tag: build_release_vs2022.bat slicer)
install_api_patch.bat    (-> build\OrcaSlicer\orca-slicer.exe, the only OrcaSlicer on the PC)
```

Close OrcaSlicer before the install step; `start.bat` starts that exe.
