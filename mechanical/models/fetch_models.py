#!/usr/bin/env python3
"""Download the EasyEDA/LCSC 3D models (STEP) of the LEAKCAM footprints that have none in Altium.

  python3 mechanical/models/fetch_models.py        (writes mechanical/models/<footprint>.step)

The part's EasyEDA footprint names its 3D model (SVGNODE shape): the model id, and where
EasyEDA puts it on its own footprint (c_origin x,y and z in EasyEDA units of 10 mil, on its canvas,
y downwards; rotation). models.txt gives that as an offset from the footprint origin (head x,y)
in Altium's sense, y upwards, in mm.

Everything is cached and kept in git: the footprint data as easyeda/<LCSC>.json, the model as
<footprint>.step. A rerun downloads only what is missing; delete a file to fetch it again.
easyeda.com answers 403 from here, the lceda.cn mirror serves the same data. models.txt lists
what was fetched with those placement values, for aligning the body in Altium.
"""
import json
import os
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
CACHE = os.path.join(HERE, "easyeda")
API = "https://lceda.cn/api/products/{}/components?version=6.4.19.5"
STEP = "https://modules.lceda.cn/qAxj6KHrDKw4blvCG8QJPs7Y/{}"

# The offsets apply only where the Altium footprint was made from the EasyEDA one (same origin);
# the TI/IPC footprints (RGT0016C_V, TPS61160ADRVT, CAPC2013...) are aligned on their pads.
FROM_EASYEDA = {"TYPE-C-31-M-12", "K230D", "AI-M62-CBS", "XL-HD3535UWC-A2", "JNJ-LTJI0112W120", "w25n02",
                "ch340x", "AHT30", "DSK24"}
# no EasyEDA model: an extruded body in Altium (Place -> 3D Body -> Extruded), sizes from the part
EXTRUDED = {
    "FLEX_CON_22P": "no EasyEDA model: extruded body over the connector outline, 2.0 mm high "
                    "(05B20L22P: 0.5 mm pitch, 2.0 mm, flip lock), standoff 0",
    "mems": "no EasyEDA model: extruded body 2.75 x 1.85 mm, 0.90 mm high (MSM381ACB026), standoff 0",
}

# parts without an EasyEDA model: the same package from another part number, tried in order
STAND_INS = {
    "C22459552": ["C6081229", "C2856804", "C54333867"],  # FPC 0.5 mm 22P 2.0 mm: SHOU HAN, XUNPU FPC-05F-22PH20, Minlenda
    "C51928208": ["C48997089", "C4747982"],      # MSM381ACBA24, MSM381A3729H9BPC: MSM381 package
}

PARTS = [  # footprint in LEAKCAM.PcbLib, designators, LCSC
    ("TYPE-C-31-M-12", "USBC1 USBC2", "C165948"),
    ("FLEX_CON_22P", "J4 J5", "C22459552"),
    ("K230D", "U3", "C42414902"),
    ("AI-M62-CBS", "U11", "C41368035"),
    ("XL-HD3535UWC-A2", "LED1 LED2 LED4 LED5 LED8 LED9 LED12 LED13", "C3646951"),
    ("JNJ-LTJI0112W120", "LED3 LED6 LED7 LED10 LED11 LED14 LED15 LED16", "C19185834"),
    ("w25n02", "U16", "C17626844"),
    ("ch340x", "U2", "C3035748"),
    ("mems", "U13", "C51928208"),
    ("AHT30", "U17", "C2757850"),
    ("DSK24", "D2 D7 D8", "C22466352"),
    ("RGT0016C_V", "U1", "C129319"),
    ("TPS61160ADRVT", "U14 U15", "C324075"),
    ("CAPC2013X145X50NL20T20", "C97 C101", "C28323"),
]


def get(url, tries=4):
    req = urllib.request.Request(url, headers={
        "User-Agent": "Mozilla/5.0", "Accept": "application/json, text/javascript, */*; q=0.01"})
    for n in range(tries):                           # the mirror times out now and then
        try:
            with urllib.request.urlopen(req, timeout=120) as r:
                return r.read()
        except OSError:
            if n == tries - 1:
                raise


def cached(lcsc):
    """the part's EasyEDA data, from easyeda/<LCSC>.json, downloaded once"""
    path = os.path.join(CACHE, lcsc + ".json")
    if not os.path.exists(path):
        data = get(API.format(lcsc))
        json.loads(data)                             # only a real answer goes into the cache
        os.makedirs(CACHE, exist_ok=True)
        with open(path, "wb") as f:
            f.write(data)
    return open(path, "rb").read()


def main():
    lines = ["footprint                 parts                     LCSC        model (EasyEDA title)"
             "                        offset x,y (mm)  z (mm)   rotation x,y,z"]
    for fp, des, lcsc in PARTS:
        node, src = None, lcsc
        try:
            for src in [lcsc] + STAND_INS.get(lcsc, []):
                d = json.loads(cached(src))
                shape = d["result"]["packageDetail"]["dataStr"]["shape"]
                node = next((s for s in shape if s.startswith("SVGNODE~")), None)
                if node:
                    break
        except Exception as e:                       # noqa: BLE001 - report and go on
            lines.append(f"{fp:25s} {des[:25]:25s} {lcsc:11s} no footprint data ({e})")
            continue
        if node is None:
            lines.append(f"{fp:25s} {des[:25]:25s} {lcsc:11s} {EXTRUDED.get(fp, 'NO 3D MODEL in EasyEDA')}")
            continue
        a = json.loads(node.split("~", 1)[1])["attrs"]
        head = d["result"]["packageDetail"]["dataStr"]["head"]
        path = os.path.join(HERE, fp + ".step")
        if os.path.exists(path):
            step = open(path, "rb").read()
        else:
            step = get(STEP.format(a["uuid"]))
            if not step.startswith(b"ISO-10303-21"):
                lines.append(f"{fp:25s} {des[:25]:25s} {lcsc:11s} model {a['uuid']} is not a STEP file")
                continue
            with open(path, "wb") as f:
                f.write(step)
        cx, cy = (float(v) for v in a.get("c_origin", "0,0").split(","))
        mm = 0.254                                   # one EasyEDA unit is 10 mil
        dx, dy = (cx - float(head["x"])) * mm, -(cy - float(head["y"])) * mm
        z = float(a.get("z", 0)) * mm
        place = (f"{dx:7.3f},{dy:7.3f}   {z:6.3f}" if fp in FROM_EASYEDA and abs(dx) < 10 and abs(dy) < 10
                 else "   align on the pads    ")
        title = a.get("title", "") if src == lcsc else f"stand-in {src}: {a.get('title', '')}"
        if src != lcsc:
            place = "   align on the pads    "        # another part's footprint: its origin need not match
        lines.append(f"{fp:25s} {des[:25]:25s} {lcsc:11s} {title[:40]:40s} "
                     f"{place}   {a.get('c_rotation', '0,0,0')}")
    open(os.path.join(HERE, "models.txt"), "w").write("\n".join(lines) + "\n")
    print("\n".join(lines))


if __name__ == "__main__":
    main()
