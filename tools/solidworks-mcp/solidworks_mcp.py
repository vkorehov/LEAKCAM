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
import asyncio
import concurrent.futures
import contextlib
import io
import os
import shutil
import socket
import tempfile
import time
import traceback

import pythoncom
import win32api
import win32con
import win32gui
import win32process
import win32com.client
import win32com.client.dynamic
from win32com.client import gencache
from mcp.server.fastmcp import FastMCP, Image
from mcp.server.transport_security import TransportSecuritySettings

PROGID = "SldWorks.Application.34"      # SolidWorks 2026
REVISION = "34."
DOC_TYPES = {".sldprt": 1, ".sldasm": 2, ".slddrw": 3}   # swDocPART, swDocASSEMBLY, swDocDRAWING
SILENT = 1                              # swOpenDocOptions_Silent / swSaveAsOptions_Silent
TEMPLATE_PART = 8                       # swUserPreferenceStringValue_e.swDefaultTemplatePart
TEMPLATE_ASSEMBLY = 9                   # swDefaultTemplateAssembly
SLDWORKS_TLB = "83A33D31-27C5-11CE-BFD4-00400513BB57"   # sldworks.tlb LIBID

# one thread owns COM and the SolidWorks object; everything goes through com()
_com = concurrent.futures.ThreadPoolExecutor(max_workers=1, initializer=pythoncom.CoInitialize)
_sw = None
_tlb = None                              # sldworks.tlb type infos, for typed()


BUSY_WAIT = 20.0                        # s a call waits for the COM thread before it gives up
_busy = None                            # (what, since) of the call on the COM thread


async def com(fn, what):
    """run fn on the COM thread without blocking the event loop: a long tool call must not hold up
    the client's other requests (tools/list after a reconnect), or it times out and drops the server.
    One call at a time: while another runs (or hangs on a SolidWorks dialog), wait BUSY_WAIT, then
    say what holds SolidWorks instead of queueing silently"""
    t0 = time.monotonic()
    while _busy is not None:
        if time.monotonic() - t0 > BUSY_WAIT:
            raise RuntimeError(busy_text() + "; this call was not started")
        await asyncio.sleep(0.2)

    def job():
        global _busy
        _busy = (what, time.monotonic())
        try:
            return fn()
        finally:
            _busy = None
    return await asyncio.wrap_future(_com.submit(job))


def busy_text():
    b = _busy
    if b is None:
        return "SolidWorks is idle"
    text = f"SolidWorks is busy with {b[0]} for {time.monotonic() - b[1]:.0f} s"
    open_ = dialogs()
    if open_:
        return text + "; waiting on a dialog: " + "; ".join(describe(d) for d in open_) + " (dismiss_dialog closes it)"
    return text + " (a long call; no SolidWorks dialog open)"


def sw_pids():
    """process ids of SLDWORKS.exe (window enumeration only: no COM, so it works while a call hangs)"""
    pids = set()
    for pid in win32process.EnumProcesses():
        try:
            h = win32api.OpenProcess(0x0410, False, pid)      # QUERY_INFORMATION | VM_READ
            name = win32process.GetModuleFileNameEx(h, 0).replace("\\", "/").rsplit("/", 1)[-1].lower()
        except Exception:
            continue
        if name == "sldworks.exe":
            pids.add(pid)
    return pids


def dialogs():
    """SolidWorks' open modal dialogs: [(hwnd, title, texts, buttons)]. A dialog (window class #32770)
    shown by an API call waits for a click that no API call can give, and holds the COM thread"""
    pids, found = sw_pids(), []

    def child(h, acc):
        cls, txt = win32gui.GetClassName(h), win32gui.GetWindowText(h).strip()
        if txt and win32gui.IsWindowVisible(h):
            acc[1 if cls == "Button" else 0].append(txt)
        return True

    def top(h, _):
        if win32gui.IsWindowVisible(h) and win32gui.GetClassName(h) == "#32770" \
                and win32process.GetWindowThreadProcessId(h)[1] in pids:
            acc = ([], [])
            with contextlib.suppress(Exception):
                win32gui.EnumChildWindows(h, child, acc)
            found.append((h, win32gui.GetWindowText(h), acc[0], acc[1]))
        return True
    if pids:
        win32gui.EnumWindows(top, None)
    return found


def describe(d):
    _, title, texts, buttons = d
    words = " ".join(texts)[:200]
    return f"'{title}'" + (f": {words}" if words else "") + (f" [buttons: {', '.join(buttons)}]" if buttons else "")


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
    if isinstance(v, win32com.client.dynamic.CDispatch):
        return v                            # a COM object (plain or typed()), not a method
    return v() if callable(v) else v


def byref_int():
    """an [out] long argument for late-bound calls (errors, warnings)"""
    return win32com.client.VARIANT(pythoncom.VT_BYREF | pythoncom.VT_I4, 0)


def nothing():
    """a Nothing object argument (e.g. SaveAs ExportData)"""
    return win32com.client.VARIANT(pythoncom.VT_DISPATCH, None)


def swtlb():
    """sldworks.tlb: its type infos by interface name"""
    global _tlb
    if _tlb is None:
        tl = pythoncom.LoadTypeLib(os.path.join(get(sw(), "GetExecutablePath"), "sldworks.tlb"))
        _tlb = {tl.GetDocumentation(i)[0]: tl.GetTypeInfo(i) for i in range(tl.GetTypeInfoCount())}
    return _tlb


def typeinfo(interface):
    ti = swtlb().get(interface)
    if ti is None:
        raise AttributeError(f"sldworks.tlb has no interface {interface}")
    if ti.GetTypeAttr().typekind == pythoncom.TKIND_INTERFACE:     # a dual interface: its dispatch half
        ti = ti.GetRefTypeInfo(ti.GetRefTypeOfImplType(-1))
    return ti


def typed(obj, interface):
    """obj as a SolidWorks interface ("IInterference", "IMathUtility", "IBody2", ...), still late-bound
    but with the interface's type info from sldworks.tlb: plain late binding cannot reach members
    of objects that publish no type info (e.g. the body of an interference), the typed object has
    every member with its real kind (property or method)"""
    ole = getattr(obj, "_oleobj_", obj)     # the raw PyIDispatch
    return win32com.client.dynamic.Dispatch(ole, interface, None, typeinfo(interface))


_IDISPATCH = {"QueryInterface", "AddRef", "Release", "GetTypeInfoCount", "GetTypeInfo", "GetIDsOfNames", "Invoke"}


def members(interface):
    """names of an interface's properties and methods, from the type library"""
    ti = typeinfo(interface)
    props, meths = set(), set()
    for i in range(ti.GetTypeAttr().cFuncs):
        fd = ti.GetFuncDesc(i)
        name = ti.GetNames(fd.memid)[0]
        (props if fd.invkind != pythoncom.INVOKE_FUNC else meths).add(name)
    return sorted(props), sorted(meths - props - _IDISPATCH)


def drop_makepy_cache():
    """a former version of this server generated sldworks.tlb into win32com's shared gen_py cache;
    while it is there, win32com hands every SolidWorks object out early-bound ([out] arguments turn
    into return tuples, byref_int() fails): SolidWorks stays late-bound here, typed() is explicit"""
    root = gencache.GetGeneratePath()
    stale = [f for f in os.listdir(root) if f.upper().startswith(SLDWORKS_TLB)]
    for f in stale:
        path = os.path.join(root, f)
        shutil.rmtree(path) if os.path.isdir(path) else os.remove(path)
    if stale:
        gencache.Rebuild(verbose=0)
    return stale


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
        except Exception:                   # SolidWorks was closed: the dead handle throws a com_error, and
            _sw = None                      # get()'s retry as a method then an AttributeError
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
async def status() -> str:
    """Whether SolidWorks is busy (and with what, for how long) - without touching SolidWorks, so it
    answers while a call hangs."""
    return busy_text()


@mcp.tool()
async def dismiss_dialog(button: str = "Cancel") -> str:
    """Close the dialogs SolidWorks shows (they block every call until clicked) by pressing the
    named button (Cancel, No, OK, ...; '&' accelerators ignored), else closing the window. Works
    while a call hangs: no COM. status names the open dialogs and their buttons."""
    open_ = dialogs()
    if not open_:
        return "no SolidWorks dialog open"
    done = []
    for h, title, texts, buttons in open_:
        hit = []

        def find(c, _):
            if win32gui.GetClassName(c) == "Button" and \
                    win32gui.GetWindowText(c).replace("&", "").strip().lower() == button.lower():
                hit.append(c)
            return True
        with contextlib.suppress(Exception):
            win32gui.EnumChildWindows(h, find, None)
        if hit:
            win32gui.PostMessage(hit[0], win32con.BM_CLICK, 0, 0)
            done.append(f"'{title}': pressed {button}")
        else:
            win32gui.PostMessage(h, win32con.WM_CLOSE, 0, 0)
            done.append(f"'{title}': no {button} button (has {', '.join(buttons) or 'none'}), closed the window")
    return "; ".join(done)


@mcp.tool()
async def import_part(step: str, path: str) -> str:
    """Import a STEP/IGES/STL file and save it silently as SolidWorks files at path (.sldprt; a
    STEP that holds an assembly needs .sldasm, its parts are saved next to it). The coordinates are
    kept, so parts imported this way line up when new_assembly places them at the origin. The
    document stays open as the active one."""
    def run():
        app, err = sw(), byref_int()
        model = app.LoadFile4(step, "r", app.GetImportFileData(step), err)
        if model is None:
            raise RuntimeError(f"SolidWorks could not import {step} (error {err.value})")
        kind = get(model, "GetType")
        want = DOC_TYPES.get(os.path.splitext(path)[1].lower())
        if want != kind:
            raise RuntimeError(f"{step} imported as a {'part' if kind == 1 else 'assembly'}; "
                               f"save it as {'.sldprt' if kind == 1 else '.sldasm'}")
        e, w = byref_int(), byref_int()
        if not model.Extension.SaveAs(path, 0, SILENT, nothing(), e, w):
            raise RuntimeError(f"save failed (error {e.value}, warning {w.value})")
        return f"imported {step} as {path}"
    return await com(run, "import_part")


@mcp.tool()
async def new_assembly(parts: list[str], path: str) -> str:
    """New assembly from saved parts/subassemblies (.sldprt/.sldasm paths), each placed with its
    own origin at the assembly origin (so imported parts keep their positions), saved silently at
    path (.sldasm) and left open as the active document. The first part is fixed, as in the GUI."""
    def run():
        app = sw()
        tpl = app.GetUserPreferenceStringValue(TEMPLATE_ASSEMBLY)
        if not os.path.exists(tpl):
            raise RuntimeError(f"default assembly template missing: {tpl} (set it in SolidWorks options)")
        opened = []
        for p in parts:
            ext = os.path.splitext(p)[1].lower()
            if ext not in (".sldprt", ".sldasm") or not os.path.exists(p):
                raise RuntimeError(f"not a saved part or assembly: {p}")
            e = byref_int()
            d = app.OpenDoc6(p, DOC_TYPES[ext], SILENT, "", e, byref_int())
            if d is None:
                raise RuntimeError(f"could not open {p} (error {e.value})")
            opened.append(get(d, "GetTitle"))
        asm = app.NewDocument(tpl, 0, 0, 0)
        if asm is None:
            raise RuntimeError("SolidWorks did not create the assembly")
        names = []
        for p in parts:
            c = asm.AddComponent5(p, 0, "", False, "", 0, 0, 0)
            if c is None:
                raise RuntimeError(f"could not insert {p}")
            names.append(get(c, "Name2"))
        for t in opened:                      # the part windows; the assembly keeps them loaded
            app.CloseDoc(t)
        e, w = byref_int(), byref_int()
        if not asm.Extension.SaveAs(path, 0, SILENT, nothing(), e, w):
            raise RuntimeError(f"save failed (error {e.value}, warning {w.value})")
        asm.ShowNamedView2("*Isometric", -1)
        get(asm, "ViewZoomtofit2")
        return f"assembly {path}: {', '.join(names)}"
    return await com(run, "new_assembly")


@mcp.tool()
async def sw_info() -> str:
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
    return await com(run, "sw_info")


@mcp.tool()
async def close_all() -> str:
    """Close every document without saving (save_as first what should be kept): keeps SolidWorks'
    memory down, one window at a time."""
    def run():
        app = sw()
        n = len(get(app, "GetDocuments") or ())
        app.CloseAllDocuments(True)
        return f"closed {n} documents"
    return await com(run, "close_all")


@mcp.tool()
async def interference() -> str:
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
                    line += f", box {[round(v * 1000, 3) for v in get(typed(b, 'IBody2'), 'GetBodyBox')]}"
                lines.append(line)
            return "\n".join(lines) or "no interference"
        finally:
            idm.Done()
    return await com(run, "interference")


@mcp.tool()
async def open_document(path: str) -> str:
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
    return await com(run, "open_document")


@mcp.tool()
async def new_part() -> str:
    """New part from the default part template; becomes the active document."""
    def run():
        app = sw()
        model = app.NewDocument(app.GetUserPreferenceStringValue(TEMPLATE_PART), 0, 0, 0)
        if model is None:
            raise RuntimeError("no default part template set in SolidWorks options")
        return f"new part {get(model, 'GetTitle')}"
    return await com(run, "new_part")


@mcp.tool()
async def save_as(path: str) -> str:
    """Save the active document; the extension picks the format (.sldprt, .sldasm, .step, .stl, ...)."""
    def run():
        err, warn = byref_int(), byref_int()
        ok = active().Extension.SaveAs(path, 0, SILENT, nothing(), err, warn)
        if not ok:
            raise RuntimeError(f"save failed (error {err.value}, warning {warn.value})")
        return f"saved {path}"
    return await com(run, "save_as")


@mcp.tool()
async def snapshot(view: str = "*Isometric") -> Image:
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
    return Image(data=await com(run, "snapshot"), format="png")


@mcp.tool()
async def run_python(code: str, wait: bool = True) -> str:
    """Run Python against the SolidWorks API (anything the tools above do not cover).

    wait=False: start it as a background job and return its id at once; job(id) gives its output
    so far and whether it finished. For anything that may take more than a minute (loops over the
    board's ~2000 bodies, big assemblies): the output survives a client that stops waiting.

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
    not; FeatureCut4's first direction goes against the sketch normal. Open and save with the
    silent option (OpenDoc6 options 1, Extension.SaveAs options 1; SaveAs3's 2 is 'copy', not silent):
    otherwise SolidWorks may show a dialog that blocks every call (status names it, dismiss_dialog
    closes it); prefer import_part / new_assembly / save_as. A start offset (T0 = 3)
    goes along the normal. Sketch with SketchManager.AddToDB = True so points do not snap.
    """
    out = io.StringIO()

    def run():
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
    what = f"run_python: {code.strip().splitlines()[0][:60] if code.strip() else ''}"
    if wait:
        return await com(run, what)
    global _next_job
    _next_job += 1
    _jobs[_next_job] = (asyncio.create_task(com(run, what)), out, what, time.monotonic())
    while len(_jobs) > 20:
        _jobs.pop(min(_jobs))
    return f"job {_next_job} started: {what}"


_jobs = {}                              # id -> (task, output buffer, what, started)
_next_job = 0


@mcp.tool()
async def job(id: int) -> str:
    """A background run_python job (run_python(..., wait=False)): running or finished, and its
    output so far."""
    if id not in _jobs:
        return f"no job {id} (kept: {sorted(_jobs) or 'none'})"
    task, out, what, t0 = _jobs[id]
    if not task.done():
        return f"job {id} running for {time.monotonic() - t0:.0f} s: {what}\n{out.getvalue()}"
    if task.exception() is not None:
        return f"job {id} failed: {task.exception()}\n{out.getvalue()}"
    return f"job {id} finished:\n{task.result()}"


def own_names():
    """this PC's hostname and IPv4 addresses: from the hostname, and the default-route address"""
    host = socket.gethostname()
    names = {"localhost", "127.0.0.1", host}
    names |= {ai[4][0] for ai in socket.getaddrinfo(host, None, socket.AF_INET)}
    with contextlib.suppress(OSError), socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as u:
        u.connect(("192.0.2.1", 9))           # sends nothing: only picks the outgoing interface
        names.add(u.getsockname()[0])
    return names


def no_prompts():
    """git (run through run_python) must never ask for a login: a GitHub popup or console prompt
    waits forever and holds the COM thread. Without stored credentials it fails at once instead"""
    os.environ["GIT_TERMINAL_PROMPT"] = "0"
    os.environ["GCM_INTERACTIVE"] = "Never"
    os.environ["GIT_ASKPASS"] = ""
    os.environ["SSH_ASKPASS"] = ""


def main():
    global BUSY_WAIT
    no_prompts()
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--host", default="0.0.0.0")
    ap.add_argument("--port", type=int, default=8000)
    ap.add_argument("--allow-host", action="append", default=[], help="another name clients use for this PC")
    ap.add_argument("--busy-wait", type=float, default=BUSY_WAIT, help="s a call waits while SolidWorks is busy")
    a = ap.parse_args()
    BUSY_WAIT = a.busy_wait
    for f in drop_makepy_cache():
        print("removed the early-binding cache", f, flush=True)
    names = own_names() | set(a.allow_host)
    mcp.settings.host, mcp.settings.port = a.host, a.port
    mcp.settings.transport_security = TransportSecuritySettings(
        enable_dns_rebinding_protection=True, allowed_hosts=sorted(f"{n}:{a.port}" for n in names))
    print("accepting Host:", ", ".join(sorted(names)), flush=True)
    print(f"SolidWorks MCP on http://{a.host}:{a.port}/mcp", flush=True)
    mcp.run(transport="streamable-http")


if __name__ == "__main__":
    main()
