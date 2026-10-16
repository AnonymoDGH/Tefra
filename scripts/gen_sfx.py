#!/usr/bin/env python3
"""Generates Tefra's built-in sound effects (resources/sfx/*.wav) by pure synthesis.

Everything here is our own work: no samples, no recordings, no third-party code. Output is deterministic (same bytes
on every run), so `--check` can verify that the committed WAVs match the generator. Pure Python + the `wave` module,
no numpy. Effects are 16-bit mono PCM: 22050 Hz for the one-shots and 8000 Hz for the loops (size budget).

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


def hum_loop():
    # Fluorescent ballast hum: a 120 Hz buzz (twice the mains frequency) rich in odd and even harmonics, a slight
    # rattle and hiss. Every component has whole cycles in the 2 s loop, so it repeats without a click.
    dur = 2.0
    n = secs(dur, SR_WIND)
    raw = noise(n, 57)
    hiss = highpass(lowpass(raw + raw, 2600.0, SR_WIND), 900.0, SR_WIND)[n:]
    out = []
    harmonics = ((1, 1.0), (2, 0.55), (3, 0.42), (4, 0.22), (5, 0.18), (6, 0.1), (8, 0.07), (11, 0.05))
    for i in range(n):
        t = i / SR_WIND
        phase = t / dur
        buzz = 0.0
        for k, a in harmonics:
            buzz += a * math.sin(TAU * 120.0 * k * t + k * 0.7)
        # Ballast rattle: the buzz clips a little and swells twice per loop.
        buzz = math.tanh(buzz * 1.6) * (0.85 + 0.15 * math.sin(TAU * 2 * phase))
        out.append(buzz * 0.5 + hiss[i] * 0.35)
    return normalize(out, 0.4)


def rain_loop():
    # Steady rain: dense filtered noise (the hiss), random drop ticks on hard surfaces, a low rumble of water. 4 s,
    # the noise filtered over two laps so the loop point is seamless; drop ticks wrap around the loop.
    dur = 4.0
    n = secs(dur, SR_WIND)
    raw = noise(n, 23)
    hiss = lowpass(highpass(raw + raw, 300.0, SR_WIND), 3200.0, SR_WIND)[n:]
    low = lowpass(lowpass(raw + raw, 160.0, SR_WIND), 160.0, SR_WIND)[n:]
    out = [hiss[i] * 0.55 + low[i] * 1.8 for i in range(n)]
    rng = Rng(77)
    for _ in range(420):
        at = int((rng.next() * 0.5 + 0.5) * n)
        amp = 0.08 + 0.25 * (rng.next() * 0.5 + 0.5) ** 3
        f = 1800.0 + 2600.0 * (rng.next() * 0.5 + 0.5)
        for k in range(int(0.012 * SR_WIND)):
            t = k / SR_WIND
            out[(at + k) % n] += amp * math.sin(TAU * f * t) * math.exp(-t * 420.0)
    return normalize(out, 0.42)


def brown(n, seed, sr):
    """Brown noise (1/f^2): leaky integration of white noise, DC wander removed."""
    rng = Rng(seed)
    y = 0.0
    out = []
    for _ in range(n):
        y = 0.9985 * y + 0.05 * rng.next()
        out.append(y)
    return highpass(out, 22.0, sr)


def swell_curve(n, sr, seed, spacing, lo, hi):
    """Slow random curve in [lo, hi]: one random control point every `spacing` s, cosine-interpolated."""
    rng = Rng(seed)
    step = max(1, secs(spacing, sr))
    pts = [lo + (hi - lo) * (rng.next() * 0.5 + 0.5) for _ in range(n // step + 2)]
    out = []
    for i in range(n):
        k, f = divmod(i, step)
        w = 0.5 - 0.5 * math.cos(math.pi * f / step)
        out.append(pts[k] * (1.0 - w) + pts[k + 1] * w)
    return out


def smoothstep(u):
    u = max(0.0, min(1.0, u))
    return u * u * (3.0 - 2.0 * u)


def thunder():
    # Close strike: a bright broadband crack (1 ms attack, ~0.15 s), then a long low rumble (brown noise 22-200 Hz
    # with slow random swells) that starts 40 ms after the crack and decays over ~5 s.
    n = secs(6.0, SR)
    crack = normalize(highpass(noise(secs(0.15, SR), 201), 800.0, SR), 1.0)
    crack = [v * min(1.0, i / (0.001 * SR)) * math.exp(-i / (0.035 * SR)) for i, v in enumerate(crack)]
    rumble = brown(n, 203, SR)
    rumble = normalize(lowpass(lowpass(highpass(highpass(rumble, 30.0, SR), 30.0, SR), 200.0, SR), 200.0, SR), 1.0)
    swells = swell_curve(n, SR, 204, 0.9, 0.35, 1.0)
    buf = [0.0] * n
    for i in range(n):
        t = i / SR
        onset = 1.0 - math.exp(-max(0.0, t - 0.04) / 0.03)
        buf[i] = 0.6 * rumble[i] * onset * math.exp(-t / 1.5) * swells[i]
    for i, v in enumerate(crack):
        buf[i] += 1.0 * v
    return normalize(fade_edges(buf, SR), 0.85)


def thunder_far():
    # Distant thunder: no crack, a soft 0.9 s onset and a low rolling rumble (brown noise below ~120 Hz).
    n = secs(7.0, SR)
    rumble = brown(n, 301, SR)
    rumble = normalize(lowpass(lowpass(highpass(highpass(rumble, 30.0, SR), 30.0, SR), 120.0, SR), 120.0, SR), 1.0)
    swells = swell_curve(n, SR, 302, 1.2, 0.2, 1.0)
    out = []
    for i in range(n):
        t = i / SR
        env = smoothstep(t / 0.9) * math.exp(-max(0.0, t - 0.9) / 1.6) * swells[i]
        out.append(rumble[i] * env)
    return normalize(fade_edges(out, SR), 0.6)


def surf_loop():
    # Waves crashing on rocks, 12 s. Periodic by construction (the noise bands are filtered over two laps, and the
    # swells wrap around the loop), so the end joins the start without a seam. Three crashes, 4 s apart: each one is
    # preceded by a swell (low and mid bands rising) and followed by a foam hiss tail. A low sea roar runs underneath.
    dur = 12.0
    n = secs(dur, SR_WIND)

    def band(seed, stages):
        x = noise(n, seed)
        x = x + x
        for kind, fc in stages:
            x = lowpass(x, fc, SR_WIND) if kind == "lp" else highpass(x, fc, SR_WIND)
        return normalize(x[n:], 1.0)

    sea = band(401, (("lp", 220.0), ("lp", 220.0)))
    mid = band(402, (("hp", 300.0), ("lp", 1200.0)))
    hi = band(403, (("hp", 1800.0), ("lp", 3600.0)))
    events = ((1.0, 1.0), (5.0, 0.8), (9.0, 0.9))  # (crash time in s, strength)

    def wave_levels(d):
        # d: seconds from the crash, signed and wrapped to the loop. Returns (low, mid, hi) weights.
        if d < -2.0 or d > 3.0:
            return 0.0, 0.0, 0.0
        if d < 0.0:
            s = ((d + 2.0) / 2.0) ** 2
            return 0.30 * s, 0.20 * s, 0.05 * s
        taper = 1.0 - smoothstep((d - 1.5) / 1.5)
        crash = math.exp(-d / 0.04)
        low = 0.25 * math.exp(-d / 1.5)
        mid_w = 0.45 * crash + 0.25 * math.exp(-d / 0.35)
        hi_w = 0.8 * crash + 0.45 * math.exp(-d / 1.1)
        return low * taper, mid_w * taper, hi_w * taper

    out = []
    for i in range(n):
        t = i / SR_WIND
        p = i / n
        roar = 0.35 * (0.85 + 0.15 * math.sin(TAU * 2 * p + 0.5)) * sea[i]
        low_w = mid_w = hi_w = 0.0
        for te, strength in events:
            d = ((t - te + dur / 2) % dur) - dur / 2
            a, b, c = wave_levels(d)
            low_w += a * strength
            mid_w += b * strength
            hi_w += c * strength
        out.append(roar + low_w * sea[i] + mid_w * mid[i] + hi_w * hi[i])
    return normalize(out, 0.8)


def wind_storm_loop():
    # Storm wind, 10 s, periodic by construction. Noise through a state-variable bandpass whose centre frequency
    # wanders (380-860 Hz) and whose level follows slow gusts. In strong gusts a whistling tone (380 Hz +/- 40 Hz,
    # whole cycles per loop) howls through.
    dur = 10.0
    n = secs(dur, SR_WIND)
    x = noise(n, 501)
    x = x + x  # two laps: the filter forgets its start within a few ms, so the second lap is steady state
    lp = bp = 0.0
    filt = []
    q = 0.35
    for i in range(2 * n):
        p = (i % n) / n
        fc = 480.0 + 260.0 * math.sin(TAU * p + 0.4) + 120.0 * math.sin(TAU * 2 * p + 2.0)
        g = 2.0 * math.sin(math.pi * fc / SR_WIND)
        lp += g * bp
        hp = x[i] - lp - q * bp
        bp += g * hp
        filt.append(bp)
    filt = filt[n:]
    out = []
    for i in range(n):
        t = i / SR_WIND
        p = i / n
        gust = max(0.08, 0.5 + 0.3 * math.sin(TAU * 2 * p + 0.7) + 0.2 * math.sin(TAU * 3 * p + 2.1)
                   + 0.1 * math.sin(TAU * 5 * p + 0.3))
        ph = TAU * (380.0 * t + 40.0 * dur / TAU * (1.0 - math.cos(TAU * p)))
        gate = max(0.0, gust - 0.55) * 2.0
        howl = gate * (math.sin(ph) + 0.4 * math.sin(2 * ph) + 0.15 * math.sin(3 * ph))
        out.append(filt[i] * gust * 1.8 + 0.22 * howl)
    return normalize(out, 0.5)


def _partial(freq, length, tau, amp):
    n = secs(length, SR)
    return [amp * math.exp(-i / (tau * SR)) * math.sin(TAU * freq * i / SR) for i in range(n)]


def _ratchet_tick(seed, amp):
    m = secs(0.012, SR)
    crack = highpass(noise(m, seed), 1500.0, SR)
    return [amp * (crack[i] * math.exp(-i / (0.0025 * SR))
                   + 0.6 * math.exp(-i / (0.004 * SR)) * math.sin(TAU * 2600.0 * i / SR)) for i in range(m)]


def lever():
    # Heavy metal lever pulled down: a ratchet (six short clicks while it moves), then the clank of it hitting its
    # stop: inharmonic metal partials ringing for ~0.1 s over a low thud.
    total = secs(0.6, SR)
    buf = [0.0] * total
    items = []
    for k in range(6):
        items.append((0.03 + 0.032 * k, _ratchet_tick(801 + k, 0.45 + 0.05 * (k % 2))))
    for f, tau, a in ((240.0, 0.09, 1.0), (367.0, 0.07, 0.7), (611.0, 0.05, 0.55), (898.0, 0.035, 0.35),
                      (1313.0, 0.025, 0.25), (1777.0, 0.02, 0.15)):
        items.append((0.30, _partial(f, 0.28, tau, a)))
    thud = sweep(110.0, 55.0, 0.2, SR, lambda i, n_: math.exp(-i / (0.04 * SR)))
    items.append((0.30, [0.6 * v for v in thud]))
    add_notes(buf, SR, items)
    return normalize(fade_edges(buf, SR), 0.6)


def lamp_on():
    # An old electric/arc lamp switching on: a switch click, then a rising electrical buzz (60 -> 100 Hz, rich in
    # harmonics, with arc hiss). After ~0.9 s it settles into a steady 100 Hz hum at a lower level.
    n = secs(1.5, SR)
    out = [0.0] * n
    click = highpass(noise(secs(0.012, SR), 601), 2000.0, SR)
    for i, v in enumerate(click):
        out[i] += 0.9 * v * math.exp(-i / (0.002 * SR))
    hiss = highpass(noise(n, 602), 900.0, SR)
    harmonics = ((1, 1.0), (2, 0.55), (3, 0.4), (4, 0.22), (5, 0.14))
    phase = 0.0
    for i in range(n):
        t = i / SR
        rise = smoothstep((t - 0.03) / 0.6)
        phase += TAU * (60.0 + 40.0 * rise) / SR
        buzz = 0.0
        for k, a in harmonics:
            buzz += a * math.sin(k * phase)
        buzz = math.tanh(1.3 * buzz)
        settle = 1.0 - 0.45 * smoothstep((t - 0.9) / 0.5)
        amp = smoothstep((t - 0.03) / 0.15) * settle
        hiss_env = rise * (1.0 - 0.8 * smoothstep((t - 0.9) / 0.5))
        out[i] += 0.5 * amp * buzz + 0.12 * hiss_env * hiss[i]
    return normalize(fade_edges(out, SR), 0.6)


def foghorn():
    # Diaphragm foghorn: 100 Hz fundamental with strong odd harmonics and a slight rasp from soft clipping. Slow
    # 0.6 s attack, sustained to 3 s, a 1 s release during which the pitch drops about 6 %, and some breath noise.
    n = secs(4.0, SR)
    breath = lowpass(noise(n, 701), 700.0, SR)
    harmonics = ((1, 1.0), (3, 0.62), (5, 0.4), (7, 0.24), (9, 0.14), (2, 0.08))
    out = []
    phase = 0.0
    for i in range(n):
        t = i / SR
        drop = smoothstep((t - 3.0) / 1.0)
        f = 100.0 * (1.0 - 0.06 * drop) * (1.0 + 0.004 * math.sin(TAU * 4.5 * t))
        phase += TAU * f / SR
        v = 0.0
        for k, a in harmonics:
            v += a * math.sin(k * phase)
        v = math.tanh(1.1 * v)
        env = smoothstep(t / 0.6) * (1.0 - drop)
        out.append(env * v + 0.06 * env * breath[i])
    return normalize(fade_edges(out, SR), 0.6)


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
    ("hum_loop", hum_loop, SR_WIND),
    ("rain_loop", rain_loop, SR_WIND),
    ("thunder", thunder, SR),
    ("thunder_far", thunder_far, SR),
    ("lever", lever, SR),
    ("lamp_on", lamp_on, SR),
    ("foghorn", foghorn, SR),
    ("surf_loop", surf_loop, SR_WIND),
    ("wind_storm_loop", wind_storm_loop, SR_WIND),
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
