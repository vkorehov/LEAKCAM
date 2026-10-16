"""mate_fit.py: (re)make every mate of mechanical/enclosure/fit/fit.SLDASM. Run on the SolidWorks PC through the
solidworks-mcp run_python tool, as a background job (wait=False, timeout 1800): reading the faces takes minutes.

Rebuilt parts (build_base.py, build_cover.py) get new face ids, so their mates break: run this after every rebuild.
It deletes all mates and makes them again from the parts as they are placed (the design position, base frame):

  contact faces (gap 0)          -> coincident
  clearance fits (gap 0.2)       -> distance 0.2: the mate states the clearance
  other facing faces up to 3 mm  -> distance as built (cells, NTC film, plugs, LED strips)
  parts without a clearance fit  -> their origin planes where they are (cells, cameras, lenses, plugs, wires, legs,
                                    screws: user 2026-10-16); one turned off the axes (tilted clamp screws) fixed in place

Every part must end where it was: a part that moves, or a mate SolidWorks refuses, is reported and its
mates are removed. A clearance fit below 0.2 shows up as a gap that is neither 0 nor >= 0.2 (listed).
The board (MIFA, ~2000 bodies) is mated by its substrate body only: walking all its faces takes far too long.
"""
import math
import time

ASM = r"C:\hw\LEAKCAM\mechanical\enclosure\fit\fit.SLDASM"
FIXED = "base-1"
# child -> hosts, in order of preference (the first host that gives faces wins a direction)
HOSTS = {
    "cover-1": ["base-1"],
    "cradle-1": ["base-1", "cover-1"],
    "LiPo_503450_1000mAh-1": ["cradle-1", "cover-1"],
    "LiPo_503450_1000mAh-2": ["LiPo_503450_1000mAh-1", "cradle-1", "cover-1"],
    "MF55_NTC-1": ["LiPo_503450_1000mAh-1", "LiPo_503450_1000mAh-2", "cradle-1"],
    "Arducam_B0370_OV5647_M12-1": ["base-1", "cover-1"],
    "Arducam_B0370_OV5647_M12-2": ["cover-1", "base-1"],
    "M12_fisheye_222-1": ["Arducam_B0370_OV5647_M12-2", "cover-1"],
    "M12_fisheye_222-2": ["Arducam_B0370_OV5647_M12-1", "base-1"],
    "USB_Type-C_Plug-1": ["base-1"],
    "USB_Type-C_Plug-2": ["base-1"],
    "PHR-2-1": ["base-1", "cover-1", "cradle-1"],
    "PHR-2-2": ["base-1", "cover-1", "cradle-1"],
    "PHR-2-3": ["base-1", "cover-1", "cradle-1"],
    "SPH-002T_wires-1": ["PHR-2-1"],
    "SPH-002T_wires-2": ["PHR-2-2"],
    "SPH-002T_wires-3": ["PHR-2-3"],
    "probe_legs-1": ["base-1", "cover-1"],
    "led_clamp_1-38": ["cover-1", "base-1"],
    "led_clamp_2-38": ["cover-1", "base-1"],
    "led_clamp_3-38": ["base-1", "cover-1"],
    "led_clamp_4-5": ["base-1", "cover-1"],
    "LEDS-148": ["led_clamp_3-38", "base-1", "cover-1"],
    "LEDS-149": ["led_clamp_1-38", "cover-1", "base-1"],
    "LEDS-150": ["led_clamp_2-38", "cover-1", "base-1"],
    "LEDS-151": ["led_clamp_4-5", "base-1", "cover-1"],
}
# no clearance fit to state (user, 2026-10-14: cells glued, cameras, lenses, USB plugs; the probe legs are a tight fit):
# held by their origin planes where they are, without reading their faces (thousands each: most of the run time)
NO_FIT = ["LiPo_503450_1000mAh-1", "LiPo_503450_1000mAh-2", "MF55_NTC-1", "Arducam_B0370_OV5647_M12-1",
          "Arducam_B0370_OV5647_M12-2", "M12_fisheye_222-1", "M12_fisheye_222-2", "USB_Type-C_Plug-1", "USB_Type-C_Plug-2",
          "SPH-002T_wires-1", "SPH-002T_wires-2", "SPH-002T_wires-3", "probe_legs-1"]
BOARD_SIZE = (36.5, 1.6, 65.0)          # the board substrate's box (mm): picked out of MIFA's bodies by it

log = []
_pid = {}                                # face handle -> persistent id (fresh())


def say(s):
    log.append(s)
    print(s)


def comps(m):
    return {get(c, "Name2"): c for c in m.GetComponents(True)}


def xfm(c):
    a = list(typed(get(c, "Transform2"), "IMathTransform").ArrayData)
    return a[:9], [x * 1000 for x in a[9:12]]


def to_asm(R, t, p):
    return [sum(p[j] * R[3 * j + i] for j in range(3)) + t[i] for i in range(3)]


def rot(R, v):
    return [sum(v[j] * R[3 * j + i] for j in range(3)) for i in range(3)]


def faces(c, bodies=None):
    """planar faces (outward normal, offset, box) and cylinders (origin, axis, radius), assembly mm.
    Typed calls (IBody2/IFace2/ISurface type info): late binding looks every name up again and costs ~12 ms a
    call, ~200 ms a face; typed ~40 ms. IFace2.Normal is the outward normal of a planar face (zero otherwise)."""
    R, t = xfm(c)
    ext = typed(m.Extension, "IModelDocExtension")
    out = []
    for b in (bodies if bodies is not None else c.GetBodies2(0) or []):
        for f in typed(b, "IBody2").GetFaces() or []:
            tf = typed(f, "IFace2")
            a = tf.GetArea() * 1e6
            if a < 0.2:
                continue
            n = list(tf.Normal or (0, 0, 0))
            if any(abs(x) > 1e-9 for x in n):
                kind = "plane"
            else:
                s = typed(tf.GetSurface(), "ISurface")
                if not s.IsCylinder():
                    continue
                kind = "cyl"
            _pid[id(f)] = ext.GetPersistReference3(f)
            bx = [v * 1000 for v in tf.GetBox()]
            cs = [to_asm(R, t, [bx[3 * i], bx[1 + 3 * j], bx[2 + 3 * k]]) for i in (0, 1) for j in (0, 1) for k in (0, 1)]
            box = [min(q[i] for q in cs) for i in range(3)] + [max(q[i] for q in cs) for i in range(3)]
            if kind == "plane":
                n = rot(R, n)
                pt = to_asm(R, t, [x * 1000 for x in tf.GetClosestPointOn(0.0, 0.0, 0.0)[:3]])
                out.append(dict(kind="plane", f=f, n=n, d=sum(n[i] * pt[i] for i in range(3)), area=a, box=box))
            else:
                p = s.CylinderParams
                out.append(dict(kind="cyl", f=f, o=to_asm(R, t, [x * 1000 for x in p[:3]]), ax=rot(R, list(p[3:6])),
                                r=p[6] * 1000, area=a, box=box))
    return out


def overlap(b1, b2, n, tol=0.05):
    return all(min(b1[i + 3], b2[i + 3]) - max(b1[i], b2[i]) > tol for i in range(3) if abs(n[i]) < 0.9)


def facing(F1, F2, maxgap):
    res = []
    for a in F1:
        if a["kind"] != "plane":
            continue
        for b in F2:
            if b["kind"] != "plane" or sum(a["n"][i] * b["n"][i] for i in range(3)) > -0.99999:
                continue
            gap = -(a["d"] + b["d"])
            if -0.01 < gap < maxgap and overlap(a["box"], b["box"], a["n"]):
                res.append((round(gap, 3), a, b))
    return res


_n = [0]


def add_mate(m, kind, dist, name, lock=False):
    """the selected pair -> a mate; returns its (unique) name, or None when SolidWorks refuses it"""
    err = byref_int()
    mt = m.AddMate5(kind, 2, False, dist / 1000, dist / 1000, dist / 1000, 0, 0, 0, 0, 0, False, lock, 0, err)
    m.ClearSelection2(True)
    if mt is None or err.value != 1:
        return None
    _n[0] += 1
    name = f"{_n[0]:03d} {name}"
    mt.Name = name
    return name


def fresh(m, f):
    """the face again from its persistent id: a rebuild disconnects the handles read before it"""
    try:
        get(f, "GetArea")
        return f
    except Exception:
        pid = _pid.get(id(f))
        return m.Extension.GetObjectByPersistReference3(pid, byref_int()) if pid is not None else f


def mate_faces(m, fa, fb, kind, dist, name, lock=False):
    m.ClearSelection2(True)
    typed(fresh(m, fa), "IEntity").Select2(False, 1)
    typed(fresh(m, fb), "IEntity").Select2(True, 1)
    return add_mate(m, kind, dist, name, lock)


def independent(ns):
    """three of the chosen normals span space: the part can neither slide nor turn"""
    if len(ns) < 3:
        return False
    a, b, c = ns[:3]
    return abs(a[0] * (b[1] * c[2] - b[2] * c[1]) - a[1] * (b[0] * c[2] - b[2] * c[0]) + a[2] * (b[0] * c[1] - b[1] * c[0])) > 0.2


def adds_direction(ns, n):
    """n is worth a mate only if it constrains a new direction: not parallel to one chosen, and once two
    are chosen, out of their plane"""
    if any(abs(sum(n[i] * q[i] for i in range(3))) > 0.95 for q in ns):
        return False
    if len(ns) == 2:
        a, b = ns
        c = [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]]
        lc = math.sqrt(sum(x * x for x in c)) or 1.0
        return abs(sum(n[i] * c[i] for i in range(3))) / lc > 0.2
    return len(ns) < 3


def delete_mates(m, names):
    """by the mate feature itself (SelectByID2 cannot select names with ':', failed trials stayed, 2026-10-14),
    all selected, one delete (one by one took 1.2 s a mate)"""
    m.ClearSelection2(True)
    for nm in names:
        f = m.FeatureByName(nm)
        if f is not None:
            typed(f, "IFeature").Select2(True, 0)
    if names:
        m.EditDelete()
    m.ClearSelection2(True)


def delete_component_mates(m, name):
    """every mate that involves the component (walking the mate folder: the mates a component hands out cannot be selected)"""
    doomed = []
    f = get(m, "FirstFeature")
    while f:
        if get(f, "GetTypeName2") == "MateGroup":
            s = get(f, "GetFirstSubFeature")
            while s:
                try:
                    mt = typed(get(s, "GetSpecificFeature2"), "IMate2")
                    who = [get(typed(mt.MateEntity(i), "IMateEntity2").ReferenceComponent, "Name2") for i in range(mt.GetMateEntityCount())]
                    if name in who:
                        doomed.append(get(s, "Name"))
                except Exception:
                    pass
                s = get(s, "GetNextSubFeature")
        f = get(f, "GetNextFeature")
    delete_mates(m, doomed)
    return len(doomed)


def moved(c, before):
    """how far the mates moved the part: 0 when it stayed (rotation entries scaled so 1e-5 counts as 0.01 mm)"""
    R, t = xfm(c)
    return max([abs(x - y) * 1000 for x, y in zip(R, before[0])] + [abs(x - y) for x, y in zip(t, before[1])])


MOVE_TOL = 0.01                          # mm


def mate_planes(m, C, child, hosts, cache, own=None):
    F1 = own if own is not None else faces(C[child])
    for maxgap in (0.25, 3.0):
        cand = []
        for h in hosts:
            if h not in cache:
                cache[h] = faces(C[h])
            cand += [(g, a, b, h) for g, a, b in facing(F1, cache[h], maxgap)]

        def rank(x):
            g = x[0]
            return (0 if abs(g) < 0.005 else 1 if abs(g - 0.2) < 0.005 else 2, hosts.index(x[3]), -min(x[1]["area"], x[2]["area"]))
        cand.sort(key=rank)
        before = xfm(C[child])
        made, ns, gaps = [], [], []
        for g, a, b, h in cand:
            n = a["n"]
            if not adds_direction(ns, n):
                continue
            kind = 0 if abs(g) < 0.005 else 5
            nm = f"{child} - {h}: " + ("contact" if kind == 0 else f"{g:.2f}")
            mt = mate_faces(m, a["f"], b["f"], kind, 0.0 if kind == 0 else g, nm)
            if mt is None:
                continue
            made.append(mt)
            ns.append(n)
            gaps.append((h, g))
            if independent(ns):
                break
        if made and moved(C[child], before) > MOVE_TOL:
            delete_mates(m, made)
            say(f"  {child}: MOVED by its mates {gaps}: mates removed")
            return False
        if independent(ns):
            odd = [f"{h} {g}" for h, g in gaps if 0.005 < g < 0.195]
            say(f"  {child}: {len(made)} mates {[(h, g) for h, g in gaps]}" + (f"  BELOW 0.2: {odd}" if odd else ""))
            return True
        if ns and maxgap == 3.0:
            # the rest from round fits: a concentric pair fixes the two directions across its axis
            cyl = [(a, b, h) for h in hosts for a in F1 if a["kind"] == "cyl" for b in cache[h] if b["kind"] == "cyl"
                   and abs(abs(sum(a["ax"][i] * b["ax"][i] for i in range(3))) - 1) < 1e-4]
            for a, b, h in sorted(cyl, key=lambda x: -min(x[0]["area"], x[1]["area"])):
                v = [b["o"][i] - a["o"][i] for i in range(3)]
                tt = sum(v[i] * a["ax"][i] for i in range(3))
                if math.sqrt(max(0.0, sum(x * x for x in v) - tt * tt)) > 0.02:
                    continue
                gain = [q for q in ([1, 0, 0], [0, 1, 0], [0, 0, 1]) if abs(sum(q[i] * a["ax"][i] for i in range(3))) < 0.1]
                trial = list(ns)
                for q in gain:
                    if adds_direction(trial, q):
                        trial.append(q)
                if len(trial) == len(ns):
                    continue
                mt = mate_faces(m, a["f"], b["f"], 1, 0.0, f"{child} - {h}: concentric r{a['r']:.2f}/{b['r']:.2f}", lock=True)
                if mt is None:
                    continue
                made.append(mt)
                ns = trial
                gaps.append((h, f"concentric r{b['r']:.2f}"))
                if independent(ns):
                    break
            if made and moved(C[child], before) > MOVE_TOL:
                delete_mates(m, made)
                say(f"  {child}: MOVED by its mates {gaps}: mates removed")
                return False
            if independent(ns):
                say(f"  {child}: {len(made)} mates {gaps}")
                return True
        delete_mates(m, made)
    say(f"  {child}: not enough facing faces (found {len(ns)} directions)")
    return False


def mate_origin_planes(m, C, child):
    """a part with no fit faces (glued cells, cameras, lenses, plugs, wires): its origin planes at their
    actual offsets from the base's. Only for parts turned by multiples of 90 degrees."""
    R, t = xfm(C[child])
    before = (R, t)
    asm = get(m, "GetTitle").rsplit(".", 1)[0]
    base_plane = {0: "Right Plane", 1: "Top Plane", 2: "Front Plane"}
    made = []
    refs = []                                # the part's own first three planes: Front, Top, Right (any names)
    f = get(C[child].GetModelDoc2, "FirstFeature")
    while f and len(refs) < 3:
        if get(f, "GetTypeName2") == "RefPlane":
            refs.append(get(f, "Name"))
        f = get(f, "GetNextFeature")
    for k, own in ((0, refs[2]), (1, refs[1]), (2, refs[0])):
        row = R[3 * k:3 * k + 3]
        j = max(range(3), key=lambda i: abs(row[i]))
        if abs(abs(row[j]) - 1) > 1e-6:
            delete_mates(m, made)
            say(f"  {child}: turned off the axes, no origin-plane mates")
            return False
        d = abs(t[j])
        for flip in (False, True):
            m.ClearSelection2(True)
            m.Extension.SelectByID2(f"{own}@{child}@{asm}", "PLANE", 0, 0, 0, False, 1, nothing(), 0)
            m.Extension.SelectByID2(f"{base_plane[j]}@{FIXED}@{asm}", "PLANE", 0, 0, 0, True, 1, nothing(), 0)
            err = byref_int()
            kind = 0 if d < 1e-6 else 5
            mt = m.AddMate5(kind, 2, flip, d / 1000, d / 1000, d / 1000, 0, 0, 0, 0, 0, False, False, 0, err)
            m.ClearSelection2(True)
            if mt is None or err.value != 1:
                continue
            _n[0] += 1
            nm = f"{_n[0]:03d} {child} - {FIXED}: {own} at {d:.3f} (no fit faces)"
            mt.Name = nm
            if moved(C[child], before) > MOVE_TOL:
                delete_mates(m, [nm])
                continue
            made.append(nm)
            break
    ok = len(made) == 3
    say(f"  {child}: {len(made)} origin-plane mates" + ("" if ok else " (incomplete)"))
    return ok


def board_substrate(c):
    """MIFA's substrate: the body whose box is the board's (reading boxes is quick, faces are not)"""
    for b in c.GetBodies2(0) or []:
        bx = [v * 1000 for v in get(b, "GetBodyBox")]
        size = sorted(bx[i + 3] - bx[i] for i in range(3))
        if all(abs(x - y) < 0.3 for x, y in zip(size, sorted(BOARD_SIZE))):
            return b
    return None


t0 = time.time()
sw.CloseAllDocuments(True)
e, w = byref_int(), byref_int()
m = sw.OpenDoc6(ASM, 2, 1, "", e, w)
C = comps(m)
say(f"opened in {time.time() - t0:.0f} s, {len(C)} components")
start = {n: xfm(c) for n, c in C.items()}       # the design positions: checked again after the final rebuild
# fresh start: no mates, everything at its design position (base frame), only the base fixed
old = []
f = get(m, "FirstFeature")
while f:
    if get(f, "GetTypeName2") == "MateGroup":
        s = get(f, "GetFirstSubFeature")
        while s:
            old.append(get(s, "Name"))
            s = get(s, "GetNextSubFeature")
    f = get(f, "GetNextFeature")
ONLY = globals().get("ONLY")             # run with ONLY = [...]: redo just those parts, keep the other mates
if ONLY:
    old = [nm for nm in old if any(f" {o} " in f" {nm} " or nm.split(" ", 1)[-1].startswith(o) for o in ONLY)]
delete_mates(m, old)
say(f"deleted {len(old)} old mates")
for n, c in C.items():
    if n != FIXED and get(c, "IsFixed"):
        m.ClearSelection2(True)
        typed(c, "IEntity").Select2(False, 0)
        m.UnfixComponent()
cache, done, missing = {}, [], []
sub = board_substrate(C["MIFA-1"]) if not ONLY or "MIFA-1" in ONLY else True
if sub is True:
    done.append("MIFA-1")
elif sub is None:
    say("  MIFA-1: substrate body not found")
    missing.append("MIFA-1")
else:
    cache["MIFA-1"] = faces(C["MIFA-1"], [sub])          # hosts that need the board see only its substrate
    (done if mate_planes(m, C, "MIFA-1", ["base-1", "cradle-1"], cache, own=cache["MIFA-1"]) else missing).append("MIFA-1")
for child, hosts in HOSTS.items():
    if ONLY and child not in ONLY:
        continue
    if child not in C:
        say(f"  {child}: not in the assembly")
        continue
    if child in NO_FIT:
        missing.append(child)                    # origin planes below
        continue
    (done if mate_planes(m, C, child, hosts, cache) else missing).append(child)
    say(f"    ({time.time() - t0:.0f} s)")
missing += [n for n in sorted(C) if n.startswith("ISO7380") and (not ONLY or n in ONLY)]   # screws: no fit to state
for n in list(missing):                  # no fit faces: hold them by their origin planes where they are
    if mate_origin_planes(m, C, n):
        missing.remove(n)
        done.append(n)
asm_name = get(m, "GetTitle").rsplit(".", 1)[0]
for n in list(missing):                  # turned off the axes: fixed where it is
    m.ClearSelection2(True)
    if m.Extension.SelectByID2(f"{n}@{asm_name}", "COMPONENT", 0, 0, 0, False, 0, nothing(), 0):
        m.FixComponent()
        say(f"  {n}: fixed in place")
        missing.remove(n)
        done.append(n)
    m.ClearSelection2(True)
left = [n for n in C if n != FIXED and n not in done and n not in missing and (not ONLY or n in ONLY)]
say(f"mated {len(done)}, not fully: {missing}, not handled: {left}")
m.ForceRebuild3(False)
# a distance mate has two solutions and SolidWorks may pick the other one only at the rebuild (the cameras
# flipped across the base's top plane, 2026-10-14): every part must still be where it started
off = [n for n, c in C.items() if moved(c, start[n]) > MOVE_TOL]
say(f"after the rebuild, moved: {off or 'none'}")
e, w = byref_int(), byref_int()
say(f"saved: {m.Save3(1, e, w)} ({time.time() - t0:.0f} s)")
