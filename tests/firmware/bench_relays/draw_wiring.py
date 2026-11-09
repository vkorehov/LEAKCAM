#!/usr/bin/env python3
"""Draws wiring.png (the wireless bench relay box): the HM-10 (CC2541) switches a 4-relay module on BLE commands and
the relays move the LEAKCAM's USB-C 1 between the bench PC and the phone; the FT4232H board flashes the HM-10 once.
    draw_wiring.py <HM-10 photo.jpg> <FT4232H photo.jpg>    (20261108_145123.jpg, 20261108_165713.jpg)
"""
import os
import sys
from PIL import Image, ImageDraw, ImageFont, ImageOps

F = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"
FB = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"
f18, f22, f26, b22, b30, b40 = (ImageFont.truetype(p, s) for p, s in
                                [(F, 18), (F, 22), (F, 26), (FB, 22), (FB, 30), (FB, 40)])
RED, GRN, BLK, GRY, BLU, ORG, PUR = "#d00000", "#008a00", "#000000", "#8a8a8a", "#0050d0", "#e07000", "#8000a0"
W = 2600
im = d = None


def canvas(h):
    global im, d
    im = Image.new("RGB", (W, h), "white")
    d = ImageDraw.Draw(im)
    return im


def box(x0, y0, x1, y1, title, fill="#f2f2f2"):
    d.rounded_rectangle([x0, y0, x1, y1], 14, fill=fill, outline=BLK, width=3)
    d.text((x0 + 14, y0 + 8), title, font=b22, fill=BLK)


def pin(x, y, label, side="r", color=BLK):
    d.ellipse([x - 7, y - 7, x + 7, y + 7], fill="white", outline=color, width=3)
    if side == "r":
        d.text((x + 14, y - 12), label, font=f22, fill=color)
    elif side == "l":
        w = d.textlength(label, font=f22)
        d.text((x - 14 - w, y - 12), label, font=f22, fill=color)
    elif side == "u":
        w = d.textlength(label, font=f18)
        d.text((x - w / 2, y - 34), label, font=f18, fill=color)
    else:
        w = d.textlength(label, font=f18)
        d.text((x - w / 2, y + 12), label, font=f18, fill=color)


def wire(pts, color, label=None, w=5):
    d.line(pts, fill=color, width=w, joint="curve")
    if label:
        (xa, ya), (xb, yb) = pts[0], pts[1]
        d.text(((xa + xb) / 2 - 20, (ya + yb) / 2 - 28), label, font=f18, fill=color)


def dot(x, y, c=BLK):
    d.ellipse([x - 8, y - 8, x + 8, y + 8], fill=c)


def heading(x, y, t):
    d.text((x, y), t, font=b30, fill=BLK)




def label(x, y, xt, yt, text, c):
    """a white-backed label at (xt, yt) with a line to the point (x, y)"""
    w = d.textlength(text, font=b22)
    d.rectangle([xt - 4, yt - 2, xt + w + 4, yt + 28], fill="white", outline=c)
    d.text((xt, yt), text, font=b22, fill=c)
    d.line([(xt + w + 6 if xt < x else xt - 6, yt + 13), (x, y)], fill=c, width=3)
    d.ellipse([x - 13, y - 13, x + 13, y + 13], outline=c, width=4)


def photos():
    """1. the HM-10 pads, 2. the FT4232H pads and the flashing wires"""
    canvas(1130)
    d.text((40, 20), "LEAKCAM bench relay box (wireless): HM-10 BLE drives 4 relays", font=b40, fill=BLK)
    heading(40, 90, "1. HM-10 pads (chip side up, antenna left)")
    photo = ImageOps.exif_transpose(Image.open(sys.argv[1])).resize((1600, 1200)).crop((100, 80, 1450, 1080))
    px, py, s = 40, 140, 1080 / 1350
    im.paste(photo.resize((1080, 800)), (px, py))
    top = [(425, 440), (480, 425), (530, 405), (585, 375), (635, 355), (690, 330), (740, 310), (795, 285),
           (845, 265), (895, 240), (945, 220), (1000, 195), (1050, 165)]          # pads 34 .. 22
    bot = [(650, 915), (700, 890), (755, 870), (810, 850), (860, 825), (905, 800), (960, 775), (1010, 750),
           (1060, 725), (1110, 700), (1165, 675), (1210, 655), (1260, 630)]       # pads 1 .. 13

    def P(xy):
        return px + (xy[0] - 100) * s, py + (xy[1] - 80) * s
    for n, xy in enumerate(bot, 1):
        x, y = P(xy)
        d.ellipse([x - 8, y - 8, x + 8, y + 8], outline="#ffff00", width=3)
    for i, xy in enumerate(top):
        x, y = P(xy)
        d.ellipse([x - 8, y - 8, x + 8, y + 8], outline="#ffff00", width=3)
    for n, text, c, xt, yt in [(7, "7 DC", ORG, 300, 880), (8, "8 DD", PUR, 420, 880), (11, "11 RESET", BLU, 560, 880),
                               (12, "12 VCC 3.3V", RED, 760, 880)]:
        x, y = P(bot[n - 1])
        label(x, y, xt, yt, text, c)
    for n, yt in [(25, 150), (26, 185), (27, 220), (28, 255)]:
        x, y = P(top[34 - n])
        label(x, y, 60, yt, f"{n} PIO{n - 23}", GRN)
    for i, t in enumerate(["Pads 1-13 on the near edge (1 at the antenna), 22-34 on the far edge (34 at the antenna).",
                           "Check first: pad 12 = 3.3 V to GND with the carrier powered.",
                           "Carrier header: VCC = 5 V in, GND. TXD/RXD/STATE/BRK not used."]):
        d.text((40, 960 + i * 30), t, font=f22, fill=BLK)

    heading(1180, 90, "2. Flashing (once): FT4232H -> HM-10")
    ft = ImageOps.exif_transpose(Image.open(sys.argv[2])).resize((1200, 1600)).crop((150, 450, 650, 1050))
    fx, fy, fs = 1180, 140, 1.25
    im.paste(ft.resize((625, 750)), (fx, fy))

    def F(x, y):
        return fx + (x - 150) * fs, fy + (y - 450) * fs
    for (x, y), text, c, yt in [((315, 520), "GND -> carrier GND", BLK, 170), ((315, 565), "PWR 5 V -> carrier VCC", RED, 230),
                                ((315, 605), "VIO (3.3 V)", GRY, 290), ((315, 645), "ADBUS0 -> pad 7 DC", ORG, 350),
                                ((315, 687), "ADBUS1 -> pad 8 DD", PUR, 410), ((312, 737), "ADBUS2 -> pad 11 RESET", BLU, 470)]:
        xx, yy = F(x, y)
        label(xx, yy, 1850, yt, text, c)
    for i, t in enumerate(["ADBUS0/1 already have header pins; ADBUS2 needs one.",
                           "Wires to the HM-10 pads short (< 15 cm), soldered to the pad edges.",
                           "FT4232H: USB-C -> mini-B cable to the Mac, passed to the Linux VM.",
                           "VIO must be 3.3 V (the CC2541 is a 3.3 V chip).",
                           "Flasher (Python, FT4232H bit-bang): chip ID -> backup -> HMSoft.",
                           "Not needed if a BLE scan already shows \"HMSoft\".",
                           "Then once: AT+MODE2 (remote PIO), PIO2-5 HIGH at power-on."]):
        d.text((1850, 560 + i * 32), t, font=f18, fill=BLK)
    return im


def relays():
    """3. the HM-10 drives the relay module; power from a USB-C charger half"""
    canvas(760)
    heading(40, 10, "3. Relay control (wireless: BLE commands to the HM-10)")
    box(60, 70, 520, 430, "HM-10 + carrier")
    for i in range(4):
        pin(510, 130 + i * 50, f"pad {25 + i} PIO{2 + i}", "l", GRN)
    pin(510, 340, "carrier VCC (5 V in)", "l", RED)
    pin(510, 390, "carrier GND", "l", BLK)
    box(60, 500, 520, 640, "USB-C charger: 4th cable half")
    d.text((74, 532), "5.1 k (4.7-5.6 k) CC -> GND", font=f18, fill=BLU)
    pin(510, 560, "VBUS +5V", "l", RED)
    pin(510, 605, "GND", "l", BLK)
    box(1000, 70, 1700, 640, "4-relay module")
    for i, lab in enumerate(["IN1", "IN2", "IN3", "IN4", "VCC", "GND"]):
        pin(1010, 130 + i * 50, lab, "r", BLK)
    for i in range(4):
        wire([(517, 130 + i * 50), (1003, 130 + i * 50)], GRN, ["K1 VBUS", "K2 CC", "K3 D+", "K4 D-"][i])
    wire([(517, 560), (700, 560), (700, 330), (1003, 330)], RED)
    wire([(517, 340), (700, 340)], RED)
    dot(700, 340, RED)
    wire([(517, 605), (760, 605), (760, 380), (1003, 380)], BLK)
    wire([(517, 390), (760, 390)], BLK)
    dot(760, 390)
    for k in range(4):
        y0 = 100 + k * 130
        d.rectangle([1360, y0, 1490, y0 + 110], outline=BLK, width=2, fill="#cfe0ff")
        d.text((1390, y0 + 40), f"K{k + 1}", font=b30, fill=BLK)
        for j, t in enumerate(["NO", "COM", "NC"]):
            pin(1580, y0 + 20 + j * 35, t, "r")
            d.line([(1490, y0 + 20 + j * 35), (1573, y0 + 20 + j * 35)], fill=GRY, width=2)
    for i, t in enumerate(["Active-low module: PIO LOW = relay on = phone side.",
                           "AT+PIOn0 = on, AT+PIOn1 = off (n = 2..5).",
                           "Box unpowered / BLE lost: relays off",
                           "= the LEAKCAM on the bench PC.",
                           "",
                           "Unplug (bench): K2 on. Plug: K2 off.",
                           "To the phone: K2 on, 1 s,",
                           "  K1 + K3 + K4 on.",
                           "Back: K2 off, 1 s, K1 + K3 + K4 off.",
                           "",
                           "JD-VCC jumper board: jumper off,",
                           "JD-VCC = 5 V, VCC = 3.3 V (pad 12).",
                           "Power not from the PC/phone half."]):
        d.text((1800, 80 + i * 32), t, font=f22, fill=BLK)
    return im


def usb():
    """4. the USB-C switching"""
    canvas(2380)
    # --- 4. USB switching ----------------------------------------------------------------------------------------
    heading(40, 1405, "4. USB-C switching: C-to-C cables cut in half (VBUS, CC, D+, D- through relays; GND common)")
    cols = {"VBUS": RED, "D+": GRN, "D-": GRY, "GND": BLK, "CC": BLU}
    kx = 1150


    def term(k, t):                      # (x, y) of relay k terminal t in this panel
        return kx + 120, 1530 + k * 190 + {"NO": 0, "COM": 45, "NC": 90}[t]


    for k in range(4):
        y0 = 1505 + k * 190
        d.rectangle([kx - 60, y0, kx + 110, y0 + 140], outline=BLK, width=2, fill="#cfe0ff")
        d.text((kx - 40, y0 + 50), f"K{k + 1}", font=b30, fill=BLK)
        for t in ["NO", "COM", "NC"]:
            x, y = term(k, t)
            pin(x, y, t, "u" if t == "NO" else "d" if t == "NC" else "r")
    d.text((kx - 60, 2310), "K1 = VBUS  K2 = CC  K3 = D+  K4 = D-   (off = PC, on = phone)", font=f22, fill=BLK)


    def usb(x, y, title, sub):
        box(x, y, x + 380, y + 290, title, "#fff6d0")
        d.text((x + 14, y + 36), sub, font=f18, fill=GRY)
        out = {}
        for i, n in enumerate(["VBUS", "CC", "D-", "D+", "GND"]):
            yy = y + 95 + i * 40
            xx = x + 370 if x < kx else x + 10
            pin(xx, yy, f"{n}", "l" if x < kx else "r", cols[n])
            out[n] = (xx, yy)
        return out


    pc = usb(60, 1500, "PC half", "plug -> bench PC USB-C")
    ph = usb(60, 1880, "PHONE half", "plug -> phone USB-C")
    lc = usb(2150, 1620, "LEAKCAM half", "plug -> LEAKCAM USB-C 1")


    def route(a, b, color, lane):
        wire([a, (lane, a[1]), (lane, b[1]), b], color)


    route(pc["VBUS"], term(0, "NC"), RED, 620)
    route(pc["CC"], term(1, "NC"), BLU, 680)
    route(pc["D+"], term(2, "NC"), GRN, 740)
    route(pc["D-"], term(3, "NC"), GRY, 800)
    route(ph["VBUS"], term(0, "NO"), RED, 860)
    route(ph["CC"], term(1, "NO"), BLU, 920)
    route(ph["D+"], term(2, "NO"), GRN, 980)
    route(ph["D-"], term(3, "NO"), GRY, 1040)
    route(term(0, "COM"), lc["VBUS"], RED, 1440)
    route(term(1, "COM"), lc["CC"], BLU, 1520)
    route(term(2, "COM"), lc["D+"], GRN, 1600)
    route(term(3, "COM"), lc["D-"], GRY, 1680)
    wire([pc["GND"], (560, pc["GND"][1]), (560, ph["GND"][1]), ph["GND"]], BLK)
    dot(560, ph["GND"][1])
    wire([(560, ph["GND"][1]), (560, 2250), (1960, 2250), (1960, lc["GND"][1]), lc["GND"]], BLK)
    for i, t in enumerate(["Cut two C-to-C cables in half, use 3 halves.",
                           "Wires: VBUS red, GND black (+ braid),",
                           "D+ green, D- white, CC = plug pin A5",
                           "(find it with a meter; any colour).",
                           "Other wires (SBU, SuperSpeed, VCONN):",
                           "insulate, not connected.",
                           "A host gives 5 V only while it sees the",
                           "LEAKCAM's 5.1 k on CC.",
                           "Unplug: K2 alone to the other side (the",
                           "host on the VBUS side loses CC, drops 5 V).",
                           "To the phone: K2 on, 1 s, K1+K3+K4 on.",
                           "Back: K2 off, 1 s, K1+K3+K4 off."]):
        d.text((2030, 1950 + i * 30), t, font=f18, fill=BLK)
    return im.crop((0, 1380, W, 2380))


parts = [photos(), relays(), usb()]
out_im = Image.new("RGB", (W, sum(p.height for p in parts)), "white")
y = 0
for p in parts:
    out_im.paste(p, (0, y))
    y += p.height
out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "wiring.png")
out_im.save(out)
print(out)
