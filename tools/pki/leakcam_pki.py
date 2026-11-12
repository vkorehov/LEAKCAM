#!/usr/bin/env python3
"""The LEAKCAM PKI for the check-in's mTLS (DESIGN.md 5.3.12 design decision, SECURITY.txt 4): ONE CA signs the
server's certificate and every device's; all EC P-256 / ECDSA-SHA256 (the BL616's ECC hardware), 20 years (the board
never renews by its own idea of the date). Everything under .secrets/pki (outside git, mode 600); the CA key never
leaves it.
    tools/pki/leakcam_pki.py ca                     the CA (once; refuses to replace one)
    tools/pki/leakcam_pki.py server                 the server's key + certificate (CN/SAN leakcam-server + its IP)
    tools/pki/leakcam_pki.py device <12 hex ID>     a device's key + certificate (CN = the ID, clientAuth); kept
    tools/pki/leakcam_pki.py der <12 hex ID>        prints the paths of the DER files for provisioning
"""
import os
import re
import subprocess
import sys

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", ".secrets", "pki"))
DAYS = str(20 * 365 + 5)                 # 20 years with the leap days
SERVER_NAME = "leakcam-server"           # the name the firmware checks (wifi_check.c TLS_HOST), whatever the address
SERVER_IP = "192.168.1.159"              # the bench server (the Pi)
CURVE = ["-newkey", "ec", "-pkeyopt", "ec_paramgen_curve:prime256v1"]


def run(*a):
    subprocess.run(["openssl", *a], check=True, capture_output=True)


def path(*p):
    return os.path.join(ROOT, *p)


def ext_file(name, text):
    f = path(name)
    with open(f, "w") as o:
        o.write(text)
    return f


def ca():
    if os.path.exists(path("ca.key")):
        sys.exit(f"leakcam_pki: {path('ca.key')} exists: the CA is made once (every device trusts it)")
    os.makedirs(path("devices"), exist_ok=True)
    os.chmod(ROOT, 0o700)
    run("req", "-x509", *CURVE, "-nodes", "-keyout", path("ca.key"), "-out", path("ca.crt"), "-days", DAYS,
        "-subj", "/O=LEAKCAM/CN=LEAKCAM CA", "-sha256",
        "-addext", "basicConstraints=critical,CA:TRUE,pathlen:0",
        "-addext", "keyUsage=critical,keyCertSign,cRLSign")
    run("x509", "-in", path("ca.crt"), "-outform", "DER", "-out", path("ca.der"))
    os.chmod(path("ca.key"), 0o600)
    print(f"leakcam_pki: CA in {ROOT}")


def issue(name, subj, ext, out_dir):
    key, csr, crt = (os.path.join(out_dir, name + s) for s in (".key", ".csr", ".crt"))
    run("req", "-new", *CURVE, "-nodes", "-keyout", key, "-out", csr, "-subj", subj, "-sha256")
    run("x509", "-req", "-in", csr, "-CA", path("ca.crt"), "-CAkey", path("ca.key"), "-CAcreateserial",
        "-out", crt, "-days", DAYS, "-sha256", "-extfile", ext_file("ext.cnf", ext))
    os.remove(csr)
    os.remove(path("ext.cnf"))
    os.chmod(key, 0o600)
    # DER for the board: the certificate and the key (SEC1 EC key, what mbedtls_pk_parse_key takes)
    run("x509", "-in", crt, "-outform", "DER", "-out", os.path.join(out_dir, name + ".crt.der"))
    run("ec", "-in", key, "-outform", "DER", "-out", os.path.join(out_dir, name + ".key.der"))
    os.chmod(os.path.join(out_dir, name + ".key.der"), 0o600)
    return key, crt


def server():
    k, c = issue("server", f"/O=LEAKCAM/CN={SERVER_NAME}",
                 "basicConstraints=critical,CA:FALSE\nkeyUsage=critical,digitalSignature\n"
                 f"extendedKeyUsage=serverAuth\nsubjectAltName=DNS:{SERVER_NAME},IP:{SERVER_IP}\n", ROOT)
    print(f"leakcam_pki: server {c}")


def device(dev):
    if not re.fullmatch(r"[0-9A-F]{12}", dev):
        sys.exit("leakcam_pki: a device ID is 12 upper-case hex digits")
    d = path("devices")
    if os.path.exists(os.path.join(d, dev + ".crt")):
        print(f"leakcam_pki: {dev} has its certificate already")
        return
    issue(dev, f"/O=LEAKCAM/CN={dev}",
          "basicConstraints=critical,CA:FALSE\nkeyUsage=critical,digitalSignature\nextendedKeyUsage=clientAuth\n", d)
    print(f"leakcam_pki: device {dev}")


def der(dev):
    d = path("devices")
    print(path("ca.der"), os.path.join(d, dev + ".crt.der"), os.path.join(d, dev + ".key.der"))


if __name__ == "__main__":
    a = sys.argv[1:]
    if a[:1] == ["ca"]:
        ca()
    elif a[:1] == ["server"]:
        server()
    elif a[:1] == ["device"] and len(a) == 2:
        device(a[1])
    elif a[:1] == ["der"] and len(a) == 2:
        der(a[1])
    else:
        sys.exit(__doc__)
