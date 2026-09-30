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
from win32com.client import gencache
from mcp.server.fastmcp import FastMCP, Image
from mcp.server.transport_security import TransportSecuritySettings

PROGID = "SldWorks.Application.34"      # SolidWorks 2026
REVISION = "34."
DOC_TYPES = {".sldprt": 1, ".sldasm": 2, ".slddrw": 3}   # swDocPART, swDocASSEMBLY, swDocDRAWING
SILENT = 1                              # swOpenDocOptions_Silent / swSaveAsOptions_Silent
TEMPLATE_PART = 8                       # swUserPreferenceStringValue_e.swDefaultTemplatePart
TEMPLATE_ASSEMBLY = 9                   # swDefaultTemplateAssembly

# one thread owns COM and the SolidWorks object; everything goes through com()
_com = concurrent.futures.ThreadPoolExecutor(max_workers=1, initializer=pythoncom.CoInitialize)
_sw = None
_tlb = None                              # makepy module of sldworks.tlb (early binding)


def com(fn):
    return _com.submit(fn).result()


def get(obj, name):
    """a zero-argument member: late binding hands some (RevisionNumber, GetMathUtility, GetFaces)
    over as properties already read, others as methods still to call. A COM object that came back
    is callable too (its default member) and must not be called."""
    try:
        v = getattr(obj, name)
    except pythoncom.com_error:
        # a true method (face.GetSurface().IsCylinder): SolidWorks throws when it is read as a
        # property, which is how late binding tries a name first
        obj._FlagAsMethod(name)
        return getattr(obj, name)()
    if isinstance(v, (win32com.client.CDispatch, win32com.client.DispatchBaseClass)):
        return v                            # a COM object (late- or early-bound), not a method
    return v() if callable(v) else v


def byref_int():
    """an [out] long argument for late-bound calls (errors, warnings)"""
    return win32com.client.VARIANT(pythoncom.VT_BYREF | pythoncom.VT_I4, 0)


def nothing():
    """a Nothing object argument (e.g. SaveAs ExportData)"""
    return win32com.client.VARIANT(pythoncom.VT_DISPATCH, None)


def swtlb():
    """the SolidWorks type library as a makepy module (generated once into win32com's gen_py cache)"""
    global _tlb
    if _tlb is None:
        path = os.path.join(get(sw(), "GetExecutablePath"), "sldworks.tlb")
        attr = pythoncom.LoadTypeLib(path).GetLibAttr()          # (guid, lcid, syskind, major, minor, flags)
        _tlb = gencache.EnsureModule(str(attr[0]), attr[1], attr[3], attr[4])
        if _tlb is None:
            raise RuntimeError(f"could not generate the type library wrapper for {path}")
    return _tlb


def typed(obj, interface):
    """obj early-bound as a SolidWorks interface ("IInterference", "IMathUtility", "IBody2", ...):
    late binding cannot reach members of objects that publish no type info (e.g. the body of an
    interference); the typed object has every member of the interface with its real kind"""
    cls = getattr(swtlb(), interface, None)
    if cls is None:
        raise AttributeError(f"sldworks.tlb has no interface {interface}")
    return cls(obj)


def members(interface):
    """names of an interface's properties and methods, from the type library"""
    cls = getattr(swtlb(), interface)
    props = set(getattr(cls, "_prop_map_get_", {}))
    meths = {n for n in dir(cls) if not n.startswith("_") and callable(getattr(cls, n)) and n not in props}
    return sorted(props), sorted(meths)


def transform(r, t):
    """a MathTransform from rotation rows r (9 values: images of the x, y, z axes) and translation t
    in metres, e.g. for component.Transform2 = transform(...)"""
    mu = typed(get(sw(), "GetMathUtility"), "IMathUtility")
    data = [float(v) for v in list(r) + list(t)] + [1.0, 0.0, 0.0, 0.0]
    return mu.CreateTransform(win32com.client.VARIANT(pythoncom.VT_ARRAY | pythoncom.VT_R8, data))


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
    """SolidWorks revision and the open document windows (title, path); documents loaded only as
    parts of an open assembly are counted, not listed."""
    def run():
        app = sw()
        docs = get(app, "GetDocuments") or ()
        shown = [d for d in docs if get(d, "Visible")]
        lines = [f"SolidWorks {get(app, 'RevisionNumber')}"]
        lines += [f"{get(d, 'GetTitle')}  {get(d, 'GetPathName')}" for d in shown]
        if len(docs) > len(shown):
            lines.append(f"(+{len(docs) - len(shown)} documents loaded without a window)")
        return "\n".join(lines)
    return com(run)


@mcp.tool()
def close_all() -> str:
    """Close every document without saving (save_as first what should be kept): keeps SolidWorks'
    memory down, one window at a time."""
    def run():
        app = sw()
        n = len(get(app, "GetDocuments") or ())
        app.CloseAllDocuments(True)
        return f"closed {n} documents"
    return com(run)


@mcp.tool()
def interference() -> str:
    """Interference detection in the active assembly: each clash as its components, volume (mm3)
    and the bounding box of the overlap (mm, assembly coordinates). Touching faces do not count."""
    def run():
        model = active()
        if get(model, "GetType") != DOC_TYPES[".sldasm"]:
            raise RuntimeError("the active document is not an assembly")
        idm = get(model, "InterferenceDetectionManager")
        idm.TreatCoincidenceAsInterference = False
        idm.IncludeMultibodyPartInterferences = False
        props, meths = members("IInterference")
        body = next((n for n in props + meths if "Body" in n and not n.startswith("I")), None)
        try:
            lines = []
            for i in get(idm, "GetInterferences") or ():
                ti = typed(i, "IInterference")
                names = " / ".join(get(c, "Name2") for c in (ti.Components or ()))
                line = f"{names}: {ti.Volume * 1e9:.3f} mm3"
                if body:
                    b = get(ti, body)
                    line += f", box {[round(v * 1000, 3) for v in typed(b, 'IBody2').GetBodyBox()]}"
                lines.append(line)
            return "\n".join(lines) or "no interference"
        finally:
            idm.Done()
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
            # a STEP/IGES import builds on the default templates: a stale one fails with error 1
            stale = [t for t in (app.GetUserPreferenceStringValue(i) for i in (TEMPLATE_PART, TEMPLATE_ASSEMBLY))
                     if not os.path.exists(t)]
            hint = f"; default template missing: {', '.join(stale)}" if stale else ""
            raise RuntimeError(f"SolidWorks could not open {path} (error {err.value}){hint}")
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
    [out] long arguments, nothing() for a Nothing object argument, typed(obj, "IInterface") for an
    early-bound object (members late binding cannot reach), members("IInterface") -> (properties,
    methods), transform(r9, t3) for a MathTransform, win32com, pythoncom. print() output is
    returned; assign `result` to return a value. Constants are numbers (see the SolidWorks API help,
    swconst). Units are metres.

    Found on SolidWorks 2026: zero-argument members may be properties (sw.GetMathUtility,
    face.GetBox, feature.GetFaces, feature.Name) - read them with get(). Top Plane sketch (x, y) is
    model (X, -Z). FeatureExtrusion3's third argument (Dir) reverses a boss, the second (Flip) does
    not; FeatureCut4's first direction goes against the sketch normal. A start offset (T0 = 3)
    goes along the normal. Sketch with SketchManager.AddToDB = True so points do not snap.
    """
    def run():
        out = io.StringIO()
        app = sw()
        env = {"sw": app, "model": app.ActiveDoc, "get": get, "byref_int": byref_int, "nothing": nothing,
               "typed": typed, "members": members, "transform": transform,
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
