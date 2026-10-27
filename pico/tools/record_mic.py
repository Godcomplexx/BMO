"""Record audio from the Pico microphone over USB serial into a WAV file.

    python tools/record_mic.py COM31 5 out.wav      (5 seconds)

The Pico must be showing MIC TEST. The file is high-passed (rumble/DC removed)
and normalised so that it is comfortably audible.
"""
import struct, sys, time, wave
import numpy as np
import serial

port, secs, out = sys.argv[1], int(sys.argv[2]), sys.argv[3]
s = serial.Serial(port, 115200, timeout=2)
s.reset_input_buffer()
s.write(b"R" + bytes([secs]))
t0 = time.time()
buf = b""
while b"AUD0" not in buf:
    buf += s.read(4096)
    if time.time() - t0 > 10:
        sys.exit("no answer from the Pico: is it in MIC TEST?")
buf = buf[buf.index(b"AUD0") + 4:]
rate, n = struct.unpack("<II", buf[:8])
buf = buf[8:]
need = n * 4
while len(buf) < need and time.time() - t0 < secs + 15:
    buf += s.read(need - len(buf))
raw = np.frombuffer(buf[:need], dtype="<i4")
np.save(out.rsplit(".", 1)[0] + "_raw.npy", raw)      # untouched 24-bit samples
x = raw.astype(np.float64)
print(f"got {len(x)} of {n} samples, {rate} Hz")

# remove DC / rumble: subtract a ~40 Hz moving average (one pole)
a = 1 - np.exp(-2 * np.pi * 40 / rate)
lp = np.empty_like(x); acc = x[0]
for i, v in enumerate(x):
    acc += a * (v - acc); lp[i] = acc
y = x - lp
peak = np.max(np.abs(y)) or 1.0
rms = float(np.sqrt(np.mean(y ** 2)))
print(f"peak {peak:.0f}, rms {rms:.0f} (24-bit units), crest {peak / max(rms, 1):.1f}")
y16 = (y / peak * 0.9 * 32767).astype("<i2")
with wave.open(out, "wb") as w:
    w.setnchannels(1); w.setsampwidth(2); w.setframerate(rate)
    w.writeframes(y16.tobytes())
print("saved", out)
