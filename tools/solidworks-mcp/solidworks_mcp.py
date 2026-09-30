"""Minimal MCP server for SolidWorks 2026 over HTTP, for the LEAKCAM cover design.

  py solidworks_mcp.py [--host 0.0.0.0] [--port 8000]

MCP over Streamable HTTP at http://<host>:8000/mcp
  (Claude Code: claude mcp add --transport http solidworks-mcp http://<this PC>:8000/mcp)

SolidWorks 2026 only (COM ProgID SldWorks.Application.34, RevisionNumber 34.x). It attaches to a
running SolidWorks or starts one. Every COM call runs on one thread that owns the COM apartment:
SolidWorks objects must not be used from the server's other threads.

Requests must name this PC in their Host header (its hostname or one of its IPv4 addresses, or an
--allow-host): the MCP library's DNS-rebinding guard, which otherwise only accepts localhost.

run_python executes code sent by the client on this PC. Anyone who can reach the port can run
anything here: allow only your own machine through the Windows firewall (README.md).
"""
import argparse
import concurrent.futures
import contextlib
import io
import os
import socket
import tempfile
import traceback

import pythoncom
import win32com.client
from mcp.server.fastmcp import FastMCP, Image
from mcp.server.transport_security import TransportSecuritySettings

PROGID = "SldWorks.Application.34"      # SolidWorks 2026
REVISION = "34."
DOC_TYPES = {".sldprt": 1, ".sldasm": 2, ".slddrw": 3}   # swDocPART, swDocASSEMBLY, swDocDRAWING
SILENT = 1                              # swOpenDocOptions_Silent / swSaveAsOptions_Silent
TEMPLATE_PART = 8                       # swUserPreferenceStringValue_e.swDefaultTemplatePart

# one thread owns COM and the SolidWorks object; everything goes through com()
_com = concurrent.futures.ThreadPoolExecutor(max_workers=1, initializer=pythoncom.CoInitialize)
_sw = None


def com(fn):
    return _com.submit(fn).result()


def get(obj, name):
    """a zero-argument member: late binding hands some (RevisionNumber, GetTitle) over as
    properties already read, others as methods still to call"""
    v = getattr(obj, name)
    return v() if callable(v) else v


def byref_int():
    """an [out] long argument for late-bound calls (errors, warnings)"""
    return win32com.client.VARIANT(pythoncom.VT_BYREF | pythoncom.VT_I4, 0)


def nothing():
    """a Nothing object argument (e.g. SaveAs ExportData)"""
    return win32com.client.VARIANT(pythoncom.VT_DISPATCH, None)


def sw():
    """the SolidWorks 2026 application: the running one, else a new visible one"""
    global _sw
    if _sw is not None:
        try:
            get(_sw, "RevisionNumber")
            return _sw
        except pythoncom.com_error:
            _sw = None                      # SolidWorks was closed
    try:
        app = win32com.client.GetActiveObject(PROGID)
    except pythoncom.com_error:
        app = win32com.client.Dispatch(PROGID)
    rev = get(app, "RevisionNumber")
    if not rev.startswith(REVISION):
        raise RuntimeError(f"SolidWorks revision {rev} is not 2026 ({REVISION}x)")
    app.Visible = True
    _sw = app
    return app


def active():
    model = sw().ActiveDoc
    if model is None:
        raise RuntimeError("no document open in SolidWorks")
    return model


mcp = FastMCP("solidworks")


@mcp.tool()
def sw_info() -> str:
    """SolidWorks revision and the open documents (title, path)."""
    def run():
        app = sw()
        docs = get(app, "GetDocuments") or ()
        lines = [f"SolidWorks {get(app, 'RevisionNumber')}"]
        lines += [f"{get(d, 'GetTitle')}  {get(d, 'GetPathName')}" for d in docs]
        return "\n".join(lines)
    return com(run)


@mcp.tool()
def open_document(path: str) -> str:
    """Open a .sldprt/.sldasm/.slddrw, or import a .step/.stp/.stl/.igs (e.g. the LEAKCAM board STEP)."""
    def run():
        app, ext = sw(), os.path.splitext(path)[1].lower()
        err = byref_int()
        if ext in DOC_TYPES:
            model = app.OpenDoc6(path, DOC_TYPES[ext], SILENT, "", err, byref_int())
        else:
            model = app.LoadFile4(path, "r", app.GetImportFileData(path), err)
        if model is None:
            raise RuntimeError(f"SolidWorks could not open {path} (error {err.value})")
        return f"opened {get(model, 'GetTitle')}"
    return com(run)


@mcp.tool()
def new_part() -> str:
    """New part from the default part template; becomes the active document."""
    def run():
        app = sw()
        model = app.NewDocument(app.GetUserPreferenceStringValue(TEMPLATE_PART), 0, 0, 0)
        if model is None:
            raise RuntimeError("no default part template set in SolidWorks options")
        return f"new part {get(model, 'GetTitle')}"
    return com(run)


@mcp.tool()
def save_as(path: str) -> str:
    """Save the active document; the extension picks the format (.sldprt, .sldasm, .step, .stl, ...)."""
    def run():
        err, warn = byref_int(), byref_int()
        ok = active().Extension.SaveAs(path, 0, SILENT, nothing(), err, warn)
        if not ok:
            raise RuntimeError(f"save failed (error {err.value}, warning {warn.value})")
        return f"saved {path}"
    return com(run)


@mcp.tool()
def snapshot(view: str = "*Isometric") -> Image:
    """PNG of the active document in a named view (*Isometric, *Front, *Top, *Right, ...), zoomed to fit."""
    def run():
        model = active()
        model.ShowNamedView2(view, -1)
        get(model, "ViewZoomtofit2")
        path = os.path.join(tempfile.gettempdir(), "solidworks_mcp_snapshot.png")
        err, warn = byref_int(), byref_int()
        if not model.Extension.SaveAs(path, 0, SILENT, nothing(), err, warn):
            raise RuntimeError(f"snapshot failed (error {err.value})")
        with open(path, "rb") as f:
            return f.read()
    return Image(data=com(run), format="png")


@mcp.tool()
def run_python(code: str) -> str:
    """Run Python against the SolidWorks API (anything the tools above do not cover).

    In scope: sw (the application), model (active document or None), get(obj, "Name") for
    zero-argument members (late binding returns some as values, some as methods), byref_int() for
    [out] long arguments, nothing() for a Nothing object argument, win32com, pythoncom. print() output is
    returned; assign `result` to return a value. Constants are numbers (see the SolidWorks API help,
    swconst). Example: model.FeatureManager.FeatureExtrusion3(...).
    """
    def run():
        out = io.StringIO()
        app = sw()
        env = {"sw": app, "model": app.ActiveDoc, "get": get, "byref_int": byref_int, "nothing": nothing,
               "win32com": win32com, "pythoncom": pythoncom,
               "print": lambda *a, **k: print(*a, **k, file=out)}
        try:
            exec(code, env)
        except Exception:
            out.write(traceback.format_exc())
        if "result" in env:
            out.write(f"result: {env['result']!r}\n")
        return out.getvalue() or "(no output)"
    return com(run)


def own_names():
    """this PC's hostname and IPv4 addresses: from the hostname, and the default-route address"""
    host = socket.gethostname()
    names = {"localhost", "127.0.0.1", host}
    names |= {ai[4][0] for ai in socket.getaddrinfo(host, None, socket.AF_INET)}
    with contextlib.suppress(OSError), socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as u:
        u.connect(("192.0.2.1", 9))           # sends nothing: only picks the outgoing interface
        names.add(u.getsockname()[0])
    return names


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--host", default="0.0.0.0")
    ap.add_argument("--port", type=int, default=8000)
    ap.add_argument("--allow-host", action="append", default=[], help="another name clients use for this PC")
    a = ap.parse_args()
    names = own_names() | set(a.allow_host)
    mcp.settings.host, mcp.settings.port = a.host, a.port
    mcp.settings.transport_security = TransportSecuritySettings(
        enable_dns_rebinding_protection=True, allowed_hosts=sorted(f"{n}:{a.port}" for n in names))
    print("accepting Host:", ", ".join(sorted(names)), flush=True)
    print(f"SolidWorks MCP on http://{a.host}:{a.port}/mcp", flush=True)
    mcp.run(transport="streamable-http")


if __name__ == "__main__":
    main()
