"""Log the Pico's USB-serial debug output to a file: python tools/read_mic.py COM31 out.log seconds"""
import sys, time, serial
port, out, secs = sys.argv[1], sys.argv[2], float(sys.argv[3])
s = serial.Serial(port, 115200, timeout=1)
t0 = time.time()
with open(out, "w") as f:
    while time.time() - t0 < secs:
        line = s.readline().decode("utf8", "replace").strip()
        if line:
            f.write(f"{time.time() - t0:7.2f} {line}\n")
            f.flush()
