# SolidWorks MCP (HTTP)

A minimal MCP server that lets Claude Code drive **SolidWorks 2026** on a Windows 10 PC, for
designing the LEAKCAM cover around the board model (`mechanical/pcb/MIFA.step`,
`mechanical/pcb/LEDS.step`). One file, `solidworks_mcp.py`; MCP over Streamable HTTP on
`http://<PC>:8000/mcp`.

| Tool | Does |
|---|---|
| `sw_info` | SolidWorks revision, open document windows (parts loaded by an assembly only counted) |
| `close_all()` | close every document without saving |
| `open_document(path)` | open `.sldprt/.sldasm/.slddrw`, or import `.step/.stp/.stl/.igs` |
| `new_part()` | new part from the default template |
| `save_as(path)` | save the active document; the extension picks the format (`.sldprt`, `.step`, `.stl`, ...) |
| `snapshot(view)` | PNG of the active document in a named view (`*Isometric`, `*Front`, `*Top`, ...) |
| `interference()` | clashes in the active assembly: components, volume, overlap box |
| `run_python(code)` | Python against the SolidWorks API: `sw`, `model`, `get()`, `byref_int()`, `nothing()`, `typed(obj, "IInterface")`, `members("IInterface")`, `transform(r9, t3)` in scope |

Only SolidWorks 2026 (COM `SldWorks.Application.34`): it attaches to a running SolidWorks or
starts one, and refuses other versions.

## Windows PC (once)

```bat
py -3 -m pip install -r requirements.txt
```

Allow only your own machine to reach the port. `run_python` runs whatever it is sent, so the
port must not be open to the whole network (run in an administrator prompt, your machine's IP):

```bat
netsh advfirewall firewall add rule name="SolidWorks MCP" dir=in action=allow protocol=TCP localport=8000 remoteip=<your machine's IP>
```

## Run

```bat
start.bat
```

It prints the host names it accepts: requests must name this PC by its hostname or one of its IPv4
addresses (the MCP library's DNS-rebinding guard). If you reach it by another name or address
(a DNS alias, a VPN or NAT address), add it: `start.bat --allow-host myname`.

## Claude Code (your machine)

```bash
claude mcp add --transport http solidworks-mcp http://<PC IP>:8000/mcp
```

Replace an older entry first with `claude mcp remove solidworks-mcp`.

`typed()` and `interference()` bind early to SolidWorks' type library (`sldworks.tlb` next to
`SLDWORKS.exe`); the first call generates its Python wrapper into win32com's `gen_py` cache, which
takes a minute once.
