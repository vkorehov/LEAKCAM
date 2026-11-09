#!/usr/bin/env python3
"""TI CC254x/CC253x 2-wire debug interface (DC, DD, RESET_N) bit-banged on an FT4232H channel A in MPSSE mode, for
the bench relay box's HM-10 (CC2541): chip ID, status, flash backup, chip erase, flash write + verify.
Wiring (tests/firmware/bench_relays/WIRING.md): ADBUS0 -> DC (HM-10 pad 7), ADBUS1 -> DD (pad 8), ADBUS2 -> RESET_N
(pad 11), GND, VIO = 3.3 V.
    ccdbg.py id                    chip ID, revision, status
    ccdbg.py read <out.bin>        the whole flash (refused while debug-locked)
    ccdbg.py erase                 chip erase (clears the debug lock)
    ccdbg.py write <image.bin>     chip erase, write, verify
    ccdbg.py verify <image.bin>    compare the flash with the image
Protocol: TI SWRA124 / CC253x-CC2541 user guide (SWRU191) chapter 3; flash write by DMA (DBGDATA -> RAM -> FWDATA)
as in CCLib. Every GPIO edge is an MPSSE "set low byte" command, so DD can change direction inside one USB transfer.
"""
import sys
import time
from pyftdi.ftdi import Ftdi

URL = "ftdi://ftdi:2232/1"           # the breakout reports FT2232H once VIO (and its EEPROM) is powered
DC, DD, RST = 0x01, 0x02, 0x04
REP = 2                        # each pin state sent REP times: DC ~1-2 MHz
FLASH = 256 * 1024
BLOCK = 1024                   # bytes per DMA burst
BUF, DESC = 0x0000, 0x1F00     # XDATA: the RAM buffer, the two DMA descriptors
CHIPS = {0x41: "CC2541", 0x8D: "CC2540", 0xA5: "CC2530", 0xB5: "CC2531"}


class Dbg:
    def __init__(self):
        self.f = Ftdi()
        self.f.open_mpsse_from_url(URL, direction=DC | DD | RST, initial=RST, frequency=6e6)
        self.q, self.reads = bytearray(), []          # queued commands; per queued read: ("bit" | "ready")

    def pins(self, dc, dd, rst, out=True):
        v = (DC if dc else 0) | (DD if dd else 0) | (RST if rst else 0)
        self.q += bytes([0x80, v, DC | RST | (DD if out else 0)]) * REP

    def sample(self, kind):
        self.q.append(0x81)
        self.reads.append(kind)

    def wbyte(self, b):
        for i in range(7, -1, -1):
            bit = (b >> i) & 1
            self.pins(1, bit, 1)               # the chip samples DD on the falling edge
            self.pins(0, bit, 1)

    def rbyte(self):
        for _ in range(8):
            self.pins(1, 0, 1, out=False)      # the chip drives DD on the rising edge
            self.sample("bit")
            self.pins(0, 0, 1, out=False)

    def cmd(self, out, nread, pulses=0):
        """queue one debug command: its bytes, then (if it answers) the ready check and nread bytes. DEBUG_INSTR
        answers after one group of 8 DC pulses (measured on the HM-10's CC2541: DD high before, low after)"""
        for b in out:
            self.wbyte(b)
        if nread:
            self.pins(0, 0, 1, out=False)
            for _ in range(pulses):
                self.pins(1, 0, 1, out=False)
                self.pins(0, 0, 1, out=False)
            self.sample("ready")               # DD low = the chip is ready to answer
            for _ in range(nread):
                self.rbyte()
            self.pins(0, 0, 1)

    def flush(self):
        """send the queue; the answered bytes (the ready checks must all be low)"""
        self.q.append(0x87)
        self.f.write_data(self.q)
        raw, reads = self.f.read_data_bytes(len(self.reads), attempt=100), self.reads
        self.q, self.reads = bytearray(), []
        if len(raw) != len(reads):
            sys.exit(f"ccdbg: {len(raw)} of {len(reads)} samples came back")
        out, cur, n = [], 0, 0
        for kind, v in zip(reads, raw):
            if kind == "ready":
                if v & DD:
                    sys.exit("ccdbg: the chip did not answer (DD high): check wiring, power, debug mode")
                continue
            cur, n = (cur << 1) | (1 if v & DD else 0), n + 1
            if n == 8:
                out.append(cur)
                cur, n = 0, 0
        return out

    def run(self, out, nread):
        self.cmd(out, nread)
        return self.flush()

    def run_slow(self, out, nread, secs=5):
        """a command the chip takes long to answer (CHIP_ERASE): while DD is high, 8 more DC pulses (SWRU191 3.3)"""
        for b in out:
            self.wbyte(b)
        self.pins(0, 0, 1, out=False)
        t0 = time.time()
        while True:
            self.q.append(0x81)
            self.q.append(0x87)
            self.f.write_data(self.q)
            self.q = bytearray()
            if not self.f.read_data_bytes(1, attempt=100)[0] & DD:
                break
            if time.time() - t0 > secs:
                sys.exit("ccdbg: the chip did not get ready")
            for _ in range(8):
                self.pins(1, 0, 1, out=False)
                self.pins(0, 0, 1, out=False)
        for _ in range(nread):
            self.rbyte()
        self.pins(0, 0, 1)
        return self.flush()

    def enter(self):
        """debug mode: RESET_N low, two DC rising+falling edges, RESET_N high. RESET_N is slow on the HM-10 (its
        level lags a USB round trip: an RC on the module), so each step waits"""
        self.pins(0, 0, 0)
        self.flush()
        time.sleep(0.05)
        for _ in range(2):
            self.pins(1, 0, 0)
            self.pins(0, 0, 0)
        self.flush()
        time.sleep(0.01)
        self.pins(0, 0, 1)
        self.flush()
        time.sleep(0.05)

    # debug commands
    def chip_id(self):
        return self.run([0x68], 2)

    def status(self):
        return self.run([0x34], 1)[0]

    def instr(self, *ops):
        """one 8051 instruction run in debug mode; the accumulator after it"""
        self.q_instr(*ops)
        return self.flush()[0]

    def q_instr(self, *ops):
        self.cmd([0x54 | len(ops)] + list(ops), 1, pulses=8)

    def write_xdata(self, addr, data):
        self.q_instr(0x90, addr >> 8, addr & 0xFF)          # MOV DPTR,#addr
        for b in data:
            self.q_instr(0x74, b)                           # MOV A,#b
            self.q_instr(0xF0)                              # MOVX @DPTR,A
            self.q_instr(0xA3)                              # INC DPTR
        self.flush()

    def read_xdata(self, addr, n):
        out = []
        for a in range(addr, addr + n, 64):                 # 64 bytes per USB transfer (~40 KB of edges)
            self.q_instr(0x90, a >> 8, a & 0xFF)
            for _ in range(min(64, addr + n - a)):
                self.q_instr(0xE0)                          # MOVX A,@DPTR
                self.q_instr(0xA3)
            out += self.flush()[1::2]
        return out

    def xosc(self):
        """the 32 MHz crystal as the system clock (flash writes need it)"""
        self.instr(0x75, 0xC6, 0x00)                        # MOV CLKCONCMD,#0
        for _ in range(100):
            if self.instr(0xE5, 0x9E) == 0:                 # MOV A,CLKCONSTA
                return
            time.sleep(0.01)
        sys.exit("ccdbg: the 32 MHz crystal did not start")

    def erase(self):
        self.run_slow([0x10], 1)
        t0 = time.time()
        while self.status() & 0x80:                         # CHIP_ERASE_BUSY
            if time.time() - t0 > 5:
                sys.exit("ccdbg: chip erase did not finish in 5 s")
            time.sleep(0.05)
        return time.time() - t0

    def read_code(self, addr, n):
        bank = addr // 0x8000
        self.instr(0x75, 0xC7, bank & 0x07)                # MEMCTR.XBANK: flash bank at XDATA 0x8000
        return self.read_xdata(0x8000 + addr % 0x8000, n)

    def dma_desc(self, ch, src, dst, trig, srcinc, dstinc, prio):
        d = [src >> 8, src & 0xFF, dst >> 8, dst & 0xFF, (BLOCK >> 8) & 0x1F, BLOCK & 0xFF, trig & 0x1F,
             (srcinc << 6) | (dstinc << 4) | 0x08 | prio]   # IRQMASK set: DMAIRQ flags the end
        a = DESC + ch * 8
        self.write_xdata(a, d)
        self.write_xdata(0x70D4 if ch == 0 else 0x70D2, [a & 0xFF, a >> 8])   # DMAnCFGL/H

    def write_flash(self, image):
        self.run([0x19, 0x22], 1)                           # WR_CONFIG: DMA_PAUSE off (it was on: 0x26), DMA runs
        self.dma_desc(0, 0x6260, BUF, 31, 0, 1, 1)          # DBGDATA -> RAM on DBG_BW
        self.dma_desc(1, BUF, 0x6273, 18, 1, 0, 2)          # RAM -> FWDATA on FLASH
        for ofs in range(0, len(image), BLOCK):
            blk = image[ofs:ofs + BLOCK]
            if blk == b"\xff" * BLOCK:
                continue                                    # erased already
            self.write_xdata(0x70D1, [0x00])                # DMAIRQ clear
            self.write_xdata(0x6271, [(ofs >> 2) & 0xFF, ofs >> 10])   # FADDRL/H (word address)
            self.write_xdata(0x70D6, [0x01])                # DMAARM channel 0
            self.run_slow([0x80 | (BLOCK >> 8), BLOCK & 0xFF] + list(blk), 1)   # BURST_WRITE
            if not self.read_xdata(0x70D1, 1)[0] & 0x01:
                sys.exit(f"ccdbg: DMA 0 did not finish at 0x{ofs:05x}")
            self.write_xdata(0x70D1, [0x00])
            self.write_xdata(0x70D6, [0x02])                # DMAARM channel 1
            self.write_xdata(0x6270, [0x02])                # FCTL.WRITE
            t0 = time.time()
            while not self.read_xdata(0x70D1, 1)[0] & 0x02:
                if self.read_xdata(0x6270, 1)[0] & 0x20:
                    sys.exit(f"ccdbg: flash write aborted at 0x{ofs:05x}")
                if time.time() - t0 > 2:
                    sys.exit(f"ccdbg: flash write did not finish at 0x{ofs:05x}")
            print(f"\rwrite 0x{ofs + BLOCK:05x}/0x{len(image):05x}", end="", flush=True)
        print()


def main():
    if len(sys.argv) < 2 or sys.argv[1] not in ("id", "read", "erase", "write", "verify"):
        sys.exit(__doc__)
    g = Dbg()
    g.enter()
    cid, rev = g.chip_id()
    st = g.status()
    print(f"chip 0x{cid:02x} ({CHIPS.get(cid, 'unknown')}) rev 0x{rev:02x}, status 0x{st:02x}"
          f"{' DEBUG_LOCKED' if st & 0x04 else ''}")
    if cid not in CHIPS:
        sys.exit("ccdbg: no CC chip answered (0x00/0xff = check DC/DD/RESET wiring and power)")
    op = sys.argv[1]
    if op == "read":
        if st & 0x04:
            sys.exit("ccdbg: debug-locked: the flash cannot be read (only erased)")
        g.xosc()
        t0, data = time.time(), bytearray()
        for a in range(0, FLASH, 1024):
            data += bytes(g.read_code(a, 1024))
            print(f"\rread 0x{a + 1024:05x}/0x{FLASH:05x}", end="", flush=True)
        open(sys.argv[2], "wb").write(data)
        print(f"\n{sys.argv[2]}: {len(data)} bytes in {time.time() - t0:.0f} s")
    elif op == "erase":
        print(f"erased in {g.erase():.2f} s, status 0x{g.status():02x}")
    elif op in ("write", "verify"):
        image = open(sys.argv[2], "rb").read()
        if len(image) > FLASH or len(image) % 4:
            sys.exit(f"ccdbg: {len(image)} bytes: not a CC2541 flash image")
        t0 = time.time()
        if op == "write":
            print(f"erased in {g.erase():.2f} s")
            g.xosc()
            g.write_flash(image)
        else:
            g.xosc()
        bad = [a for a in range(0, len(image), 1024) if bytes(g.read_code(a, min(1024, len(image) - a)))
               != image[a:a + 1024]]
        if bad:
            sys.exit(f"ccdbg: verify failed at {len(bad)} blocks, first 0x{bad[0]:05x}")
        print(f"{'written and ' if op == 'write' else ''}verified, {len(image)} bytes in {time.time() - t0:.0f} s")
    g.f.close()


if __name__ == "__main__":
    main()
