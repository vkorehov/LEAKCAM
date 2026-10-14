"""mate_fit.py: (re)make every mate of mechanical/enclosure/fit/fit.SLDASM. Run on the SolidWorks PC through the
solidworks-mcp run_python tool, as a background job (wait=False, timeout 1800): reading the faces takes minutes.

Rebuilt parts (build_base.py, build_cover.py) get new face ids, so their mates break: run this after every rebuild.
It deletes all mates and makes them again from the parts as they are placed (the design position, base frame):

  contact faces (gap 0)          -> coincident
  clearance fits (gap 0.2)       -> distance 0.2: the mate states the clearance
  other facing faces up to 3 mm  -> distance as built (cells, NTC film, plugs, LED strips)
  screws                         -> concentric with their pilot (rotation locked) + head on its seat

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
SCREW_HOSTS = ["base-1", "cover-1", "cradle-1", "led_clamp_1-38", "led_clamp_2-38", "led_clamp_3-38", "led_clamp_4-5"]
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
    """planar faces (outward normal, offset, box) and cylinders (origin, axis, radius), assembly mm"""
    R, t = xfm(c)
    out = []
    for b in (bodies if bodies is not None else c.GetBodies2(0) or []):
        for f in get(b, "GetFaces") or []:
            s = get(f, "GetSurface")
            _pid[id(f)] = m.Extension.GetPersistReference3(f)
            if get(s, "IsPlane"):
                p = get(s, "PlaneParams")
                n = list(p[:3])
                if get(f, "FaceInSurfaceSense"):      # late-bound, the sense reads inverted: this makes n outward
                    n = [-x for x in n]
                a = get(f, "GetArea") * 1e6
                if a < 0.2:
                    continue
                bx = [v * 1000 for v in get(f, "GetBox")]
                cs = [to_asm(R, t, [bx[3 * i], bx[1 + 3 * j], bx[2 + 3 * k]]) for i in (0, 1) for j in (0, 1) for k in (0, 1)]
                n = rot(R, n)
                pt = to_asm(R, t, [x * 1000 for x in p[3:6]])
                out.append(dict(kind="plane", f=f, n=n, d=sum(n[i] * pt[i] for i in range(3)), area=a,
                                box=[min(q[i] for q in cs) for i in range(3)] + [max(q[i] for q in cs) for i in range(3)]))
            elif get(s, "IsCylinder"):
                p = get(s, "CylinderParams")
                out.append(dict(kind="cyl", f=f, o=to_asm(R, t, [x * 1000 for x in p[:3]]), ax=rot(R, list(p[3:6])),
                                r=p[6] * 1000, area=get(f, "GetArea") * 1e6))
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
    for nm in names:
        m.ClearSelection2(True)
        if m.Extension.SelectByID2(nm, "MATE", 0, 0, 0, False, 0, nothing(), 0):
            m.EditDelete()
    m.ClearSelection2(True)


def moved(c, before):
    R, t = xfm(c)
    return max(abs(x - y) for x, y in zip(R + t, before[0] + before[1]))


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
        if made and moved(C[child], before) > 1e-6:
            delete_mates(m, made)
            say(f"  {child}: MOVED by its mates {gaps}: mates removed")
            return False
        if independent(ns):
            odd = [f"{h} {g}" for h, g in gaps if 0.005 < g < 0.195]
            say(f"  {child}: {len(made)} mates {[(h, g) for h, g in gaps]}" + (f"  BELOW 0.2: {odd}" if odd else ""))
            return True
        delete_mates(m, made)
    say(f"  {child}: not enough facing faces (found {len(ns)} directions)")
    return False


def mate_screw(m, C, screw, cache):
    F = faces(C[screw])
    shank = min((f for f in F if f["kind"] == "cyl"), key=lambda f: f["r"])
    o, ax = shank["o"], shank["ax"]
    for h in SCREW_HOSTS:
        if h not in cache:
            cache[h] = faces(C[h])
        for b in cache[h]:
            if b["kind"] != "cyl" or b["r"] > 1.0 or abs(abs(sum(ax[i] * b["ax"][i] for i in range(3))) - 1) > 1e-4:
                continue
            v = [b["o"][i] - o[i] for i in range(3)]
            tt = sum(v[i] * ax[i] for i in range(3))
            if math.sqrt(max(0.0, sum(x * x for x in v) - tt * tt)) > 0.05:
                continue
            before = xfm(C[screw])
            mt = mate_faces(m, shank["f"], b["f"], 1, 0.0, f"{screw} in {h}: concentric", lock=True)
            if mt is None:
                continue
            # the head's underside on its seat: the screw plane facing the host at gap 0 or the nearest
            seat = facing([f for f in F if f["kind"] == "plane"], [f for f in sum((cache[k] for k in SCREW_HOSTS if k in cache), []) if f["kind"] == "plane"], 0.6)
            seat = [s for s in seat if abs(abs(sum(s[1]["n"][i] * ax[i] for i in range(3))) - 1) < 1e-4]
            seat.sort(key=lambda s: (abs(s[0]), -s[1]["area"]))
            mt2 = None
            if seat:
                g = seat[0][0]
                mt2 = mate_faces(m, seat[0][1]["f"], seat[0][2]["f"], 0 if abs(g) < 0.005 else 5, 0.0 if abs(g) < 0.005 else g,
                                 f"{screw} head: " + ("seated" if abs(g) < 0.005 else f"{g:.2f}"))
            if moved(C[screw], before) > 1e-6:
                delete_mates(m, [x for x in (mt, mt2) if x is not None])
                say(f"  {screw}: MOVED: mates removed")
                return False
            say(f"  {screw}: concentric with the Ø{2 * b['r']:.2f} pilot in {h}" + (f", head {seat[0][0]}" if seat else ", NO head seat"))
            return mt2 is not None
    say(f"  {screw}: no pilot found")
    return False


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
delete_mates(m, old)
say(f"deleted {len(old)} old mates")
for n, c in C.items():
    if n != FIXED and get(c, "IsFixed"):
        m.ClearSelection2(True)
        typed(c, "IEntity").Select2(False, 0)
        m.UnfixComponent()
cache, done, missing = {}, [], []
sub = board_substrate(C["MIFA-1"])
if sub is None:
    say("  MIFA-1: substrate body not found")
    missing.append("MIFA-1")
else:
    cache["MIFA-1"] = faces(C["MIFA-1"], [sub])          # hosts that need the board see only its substrate
    (done if mate_planes(m, C, "MIFA-1", ["base-1", "cradle-1"], cache, own=cache["MIFA-1"]) else missing).append("MIFA-1")
for child, hosts in HOSTS.items():
    if child not in C:
        say(f"  {child}: not in the assembly")
        continue
    (done if mate_planes(m, C, child, hosts, cache) else missing).append(child)
    say(f"    ({time.time() - t0:.0f} s)")
for n in sorted(C):
    if n.startswith("ISO7380"):
        (done if mate_screw(m, C, n, cache) else missing).append(n)
left = [n for n in C if n != FIXED and n not in done and n not in missing]
say(f"mated {len(done)}, not fully: {missing}, not handled: {left}")
m.ForceRebuild3(False)
e, w = byref_int(), byref_int()
say(f"saved: {m.Save3(1, e, w)} ({time.time() - t0:.0f} s)")
