#!/usr/bin/env python3
"""Generates Tefra's built-in sound effects (resources/sfx/*.wav) by pure synthesis.

Everything here is our own work: no samples, no recordings, no third-party code. Output is deterministic (same bytes
on every run), so `--check` can verify that the committed WAVs match the generator. Pure Python + the `wave` module,
no numpy. Effects are 16-bit mono PCM: 22050 Hz for the short ones and 8000 Hz for the wind loop (size budget).

Usage:
  python3 scripts/gen_sfx.py                 # writes resources/sfx/*.wav
  python3 scripts/gen_sfx.py --out DIR       # writes elsewhere
  python3 scripts/gen_sfx.py --check         # exit 1 if the files in resources/sfx differ from the generator
"""
import argparse
import io
import math
import os
import struct
import sys
import wave

SR = 22050       # short effects
SR_WIND = 8000   # wind loop (keeps the file under 60 KB)
TAU = 2.0 * math.pi


class Rng:
    """Linear congruential generator: the same sequence on every platform."""

    def __init__(self, seed):
        self.s = (seed & 0x7FFFFFFF) or 1

    def next(self):  # uniform in [-1, 1)
        self.s = (self.s * 1103515245 + 12345) & 0x7FFFFFFF
        return self.s / 0x40000000 - 1.0


def secs(s, sr):
    return int(round(s * sr))


def noise(n, seed):
    rng = Rng(seed)
    return [rng.next() for _ in range(n)]


def lowpass(x, fc, sr):
    a = 1.0 - math.exp(-TAU * fc / sr)
    y = 0.0
    out = []
    for v in x:
        y += a * (v - y)
        out.append(y)
    return out


def highpass(x, fc, sr):
    low = lowpass(x, fc, sr)
    return [v - l for v, l in zip(x, low)]


def add_notes(buf, sr, items):
    """items: iterable of (start_s, samples) where samples are already rendered at sr."""
    for start, samples in items:
        j0 = secs(start, sr)
        for i, v in enumerate(samples):
            j = j0 + i
            if j < len(buf):
                buf[j] += v


def render_note(freq, length, sr, decay, attack=0.004, harmonics=((1, 1.0),)):
    n = secs(length, sr)
    a_n = max(1, secs(attack, sr))
    out = []
    for i in range(n):
        t = i / sr
        env = min(1.0, i / a_n) * math.exp(-t / decay)
        v = 0.0
        for k, g in harmonics:
            v += g * math.sin(TAU * freq * k * t)
        out.append(env * v)
    return out


def sweep(f0, f1, dur, sr, amp, wave_fn=math.sin, vibrato=None):
    """Exponential frequency sweep; amp(i, n) gives the envelope."""
    n = secs(dur, sr)
    out = []
    ph = 0.0
    for i in range(n):
        t = i / sr
        u = i / n
        f = f0 * (f1 / f0) ** u
        if vibrato:
            rate, depth = vibrato
            f *= 1.0 + depth * math.sin(TAU * rate * t)
        ph += TAU * f / sr
        out.append(amp(i, n) * wave_fn(ph))
    return out


def fade_edges(buf, sr, ms=4.0):
    n = min(len(buf) // 2, max(1, secs(ms / 1000.0, sr)))
    for i in range(n):
        g = i / n
        buf[i] *= g
        buf[-1 - i] *= g
    return buf


def normalize(buf, peak):
    m = max((abs(v) for v in buf), default=0.0) or 1.0
    return [v * peak / m for v in buf]


def pad(buf, length):
    if len(buf) < length:
        buf.extend([0.0] * (length - len(buf)))
    return buf


def jump():
    body = sweep(300.0, 700.0, 0.16, SR,
                 lambda i, n: min(1.0, i / (0.008 * SR)) * math.exp(-i / (0.11 * SR)),
                 wave_fn=lambda p: math.sin(p) + 0.3 * math.sin(2 * p))
    return normalize(fade_edges(body, SR), 0.55)


def land():
    thump = sweep(150.0, 55.0, 0.16, SR, lambda i, n: math.exp(-i / (0.05 * SR)))
    burst = lowpass(noise(secs(0.06, SR), 11), 900.0, SR)
    burst = [v * math.exp(-i / (0.02 * SR)) for i, v in enumerate(burst)]
    buf = pad(thump, len(burst))
    for i, v in enumerate(burst):
        buf[i] += 0.6 * v
    return normalize(fade_edges(buf, SR), 0.7)


def footstep_grass():
    n = secs(0.20, SR)
    band = [a - b for a, b in zip(lowpass(noise(n, 23), 3200.0, SR), lowpass(noise(n, 23), 280.0, SR))]
    buf = [v * math.exp(-i / (0.05 * SR)) * min(1.0, i / (0.002 * SR)) for i, v in enumerate(band)]
    return normalize(fade_edges(buf, SR), 0.55)


def footstep_stone():
    n = secs(0.12, SR)
    click = highpass(noise(n, 37), 1500.0, SR)
    click = [v * math.exp(-i / (0.012 * SR)) for i, v in enumerate(click)]
    thud = sweep(210.0, 120.0, 0.09, SR, lambda i, n_: math.exp(-i / (0.02 * SR)))
    buf = pad(click, len(thud))
    for i, v in enumerate(thud):
        buf[i] += 0.8 * v
    return normalize(fade_edges(buf, SR), 0.7)


def checkpoint():
    # Pleasant rising arpeggio C6-E6-G6 that resolves into a held chord.
    total = secs(1.0, SR)
    buf = [0.0] * total
    partials = ((1, 1.0), (2, 0.3), (3, 0.08))
    add_notes(buf, SR, [(0.0, render_note(1046.5, 0.6, SR, 0.22, harmonics=partials)),
                        (0.09, render_note(1318.5, 0.6, SR, 0.22, harmonics=partials)),
                        (0.18, render_note(1568.0, 0.8, SR, 0.30, harmonics=partials)),
                        (0.30, render_note(1046.5, 0.7, SR, 0.35, harmonics=partials)),
                        (0.30, render_note(1568.0, 0.7, SR, 0.35, harmonics=partials))])
    return normalize(fade_edges(buf, SR), 0.5)


def win():
    # Short fanfare: C5 E5 G5 C6, then a held C6 major chord.
    total = secs(1.2, SR)
    buf = [0.0] * total
    partials = ((1, 1.0), (2, 0.25), (3, 0.12))
    steps = [(523.25, 0.00), (659.25, 0.13), (783.99, 0.26), (1046.5, 0.39)]
    items = [(t, render_note(f, 0.30, SR, 0.16, harmonics=partials)) for f, t in steps]
    for f in (1046.5, 1318.5, 1568.0):
        items.append((0.52, render_note(f, 0.66, SR, 0.38, harmonics=partials)))
    add_notes(buf, SR, items)
    return normalize(fade_edges(buf, SR), 0.5)


def death():
    # A soft, descending bloop with a slow vibrato. Deliberately simple: a sine glide, not a copy of any sound.
    body = sweep(620.0, 170.0, 0.50, SR, lambda i, n: min(1.0, i / (0.003 * SR)) * math.exp(-i / (0.28 * SR)),
                 vibrato=(5.5, 0.015))
    return normalize(fade_edges(body, SR), 0.5)


def click():
    tone = render_note(2200.0, 0.04, SR, 0.008, attack=0.0005, harmonics=((1, 1.0), (2, 0.2)))
    return normalize(fade_edges(tone, SR), 0.5)


def coin():
    total = secs(0.32, SR)
    buf = [0.0] * total
    partials = ((1, 1.0), (2, 0.4))
    add_notes(buf, SR, [(0.0, render_note(987.77, 0.14, SR, 0.06, harmonics=partials)),
                        (0.07, render_note(1318.5, 0.26, SR, 0.10, harmonics=partials))])
    return normalize(fade_edges(buf, SR), 0.5)


def wind_loop():
    # Periodic by construction: the noise is filtered over two laps (keep the second, the filter forgets in
    # ~0.3 ms), gusts are sines with whole cycles per loop, and the rumble is an exact 90 Hz x 3.6 s = 324 cycles.
    dur = 3.6
    n = secs(dur, SR_WIND)
    raw = noise(n, 91)
    filtered = lowpass(lowpass(raw + raw, 700.0, SR_WIND), 700.0, SR_WIND)[n:]
    out = []
    for i in range(n):
        t = i / SR_WIND
        phase = t / dur
        gust = 0.55 + 0.25 * math.sin(TAU * phase) + 0.2 * math.sin(TAU * 2 * phase + 1.3)
        rumble = 0.12 * (1.0 + math.sin(TAU * phase * 2)) * math.sin(TAU * 90.0 * t)
        out.append(filtered[i] * gust * 2.2 + rumble)
    return normalize(out, 0.45)


EFFECTS = [
    ("jump", jump, SR),
    ("land", land, SR),
    ("footstep_grass", footstep_grass, SR),
    ("footstep_stone", footstep_stone, SR),
    ("checkpoint", checkpoint, SR),
    ("win", win, SR),
    ("death", death, SR),
    ("click", click, SR),
    ("coin", coin, SR),
    ("wind_loop", wind_loop, SR_WIND),
]


def encode_wav(samples, rate):
    frames = bytearray()
    for v in samples:
        q = int(round(max(-1.0, min(1.0, v)) * 32767.0))
        frames += struct.pack("<h", q)
    buf = io.BytesIO()
    with wave.open(buf, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(bytes(frames))
    return buf.getvalue()


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    default_out = os.path.join(os.path.dirname(here), "resources", "sfx")
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", default=default_out)
    ap.add_argument("--check", action="store_true")
    args = ap.parse_args()

    status = 0
    for name, fn, rate in EFFECTS:
        data = encode_wav(fn(), rate)
        path = os.path.join(args.out, name + ".wav")
        if args.check:
            try:
                with open(path, "rb") as f:
                    same = f.read() == data
            except OSError:
                same = False
            if not same:
                print("sfx: %s differs from the generator" % path, file=sys.stderr)
                status = 1
        else:
            os.makedirs(args.out, exist_ok=True)
            with open(path, "wb") as f:
                f.write(data)
            print("%-16s %6d bytes  %5d Hz" % (name, len(data), rate))
    return status


if __name__ == "__main__":
    sys.exit(main())
