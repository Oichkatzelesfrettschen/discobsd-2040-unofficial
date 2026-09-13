#!/usr/bin/env python3
"""Board test for a.out admission.

usage: PYTHON board_aout_admission.py [unix.uf2,flash.uf2]
Flashes the named images in one BOOTSEL visit when given, then logs in as
operator on the USB console. Header-only images travel as uuencoded
text; a truncated, an oversized and an even-entry image must be refused
with distinct errors while the shell keeps running."""
import binascii, glob, re, struct, subprocess, sys, time
import serial
ESC = re.compile(rb'\x1b\[[0-9;?]*[A-Za-z]|\r')
s = None
def rd(t):
    buf = b""; t0 = time.time()
    while time.time() - t0 < t:
        d = s.read(4096)
        if d: buf += d
        else: time.sleep(0.05)
    return ESC.sub(b"", buf).decode("latin1")
def boot(uf2s):
    global s
    if uf2s:
        subprocess.call(["picotool", "reboot", "-u", "-f"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL); time.sleep(3)
        for u in uf2s.split(","):
            subprocess.check_call(["picotool", "load", u], stdout=subprocess.DEVNULL)
        subprocess.check_call(["picotool", "reboot"], stdout=subprocess.DEVNULL)
    else:
        subprocess.call(["picotool", "reboot", "-f"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(4); t0 = time.time()
    while time.time() - t0 < 30 and not glob.glob("/dev/serial/by-id/*DiscoBSD*rp2040-if00"): time.sleep(0.5)
    time.sleep(1.5)
    s = serial.Serial(glob.glob("/dev/serial/by-id/*DiscoBSD*rp2040-if00")[0], 115200, timeout=0)
    rd(25); s.write(b"operator\r"); rd(4)
def run(c, wait=5):
    s.write(c.encode() + b"\r"); out = rd(wait)
    return [l for l in out.split("\n")[1:] if l.strip() and not l.startswith("discobsd")]
def image(text, data, bss, entry):
    return struct.pack("<8I", 0x107, text, data, bss, 0, 0, 0, entry)
def put(name, blob):
    lines = ["begin 755 %s" % name]
    for i in range(0, len(blob), 45):
        lines.append(binascii.b2a_uu(blob[i:i+45]).decode().rstrip("\n"))
    lines += ["`", "end"]
    run("rm -f /tmp/%s /tmp/%s.uu" % (name, name))
    for l in lines:
        run("echo '%s' >> /tmp/%s.uu" % (l.replace("'", "'\\''"), name), 1.5)
    return run("cd /tmp && %s /tmp/%s.uu; ls -l /tmp/%s" % (DECODE, name, name))
boot(sys.argv[1] if len(sys.argv) > 1 else "")
DECODE = "uudecode" if run("ls /usr/bin/uudecode") == ["/usr/bin/uudecode"] else "textbox uudecode"
print("decoder:", DECODE)
for name, blob in (("trunc", image(0x70e4, 0x96c, 0x1b10, 0x200001f5)),
                   ("toobig", image(0x70e4, 0x96c, 0x100000, 0x200001f5)),
                   ("badentry", image(0x70e4, 0x96c, 0x1b10, 0x200001f4))):
    print(name, "->", put(name, blob))
    print("%-8s -> %s" % (name, run("/tmp/%s; echo rc=$?" % name)))
print("survive ->", run("echo alive; sh -c 'echo child-ok'; echo 3 4 | awk '{print $1*$2}'"))
