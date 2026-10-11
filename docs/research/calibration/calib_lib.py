"""Utilidades de calibración por estadística de imagen (Tefra).

Todo es numpy + PIL. Las imágenes se tratan como sRGB de visualización (8 bit).
Las métricas de luminancia usan Y lineal (Rec.709) y L* (CIELAB).
"""
import math
import numpy as np
from PIL import Image

W_OUT, H_OUT = 1280, 720  # resolución común de análisis (los renders ya son 1280x720)


def load_rgb_u8(path):
    im = Image.open(path).convert("RGB")
    return np.asarray(im, dtype=np.uint8)


def equirect_view(src_u8, yaw_deg, pitch_deg=0.0, hfov_deg=70.0, w=W_OUT, h=H_OUT):
    """Proyección rectilínea desde un panorama equirectangular (bilineal). Devuelve uint8 (h,w,3)."""
    H, W = src_u8.shape[:2]
    f = (w / 2.0) / math.tan(math.radians(hfov_deg) / 2.0)
    xs = (np.arange(w) + 0.5 - w / 2.0) / f
    ys = -(np.arange(h) + 0.5 - h / 2.0) / f
    X, Y = np.meshgrid(xs, ys)
    Z = -np.ones_like(X)
    n = np.sqrt(X * X + Y * Y + Z * Z)
    X, Y, Z = X / n, Y / n, Z / n
    p = math.radians(pitch_deg)
    y1 = Y * math.cos(p) - Z * math.sin(p)
    z1 = Y * math.sin(p) + Z * math.cos(p)
    t = math.radians(yaw_deg)
    x2 = X * math.cos(t) + z1 * math.sin(t)
    z2 = -X * math.sin(t) + z1 * math.cos(t)
    lon = np.arctan2(x2, -z2)
    lat = np.arcsin(np.clip(y1, -1, 1))
    u = (lon + math.pi) / (2 * math.pi) * W - 0.5
    v = (math.pi / 2 - lat) / math.pi * H - 0.5
    u0 = np.floor(u).astype(np.int64)
    v0 = np.floor(v).astype(np.int64)
    fu = (u - u0)[..., None]
    fv = (v - v0)[..., None]
    u0m, u1m = u0 % W, (u0 + 1) % W
    v0c = np.clip(v0, 0, H - 1)
    v1c = np.clip(v0 + 1, 0, H - 1)
    s = src_u8.astype(np.float32)
    c00 = s[v0c, u0m]
    c01 = s[v0c, u1m]
    c10 = s[v1c, u0m]
    c11 = s[v1c, u1m]
    out = (c00 * (1 - fu) + c01 * fu) * (1 - fv) + (c10 * (1 - fu) + c11 * fu) * fv
    return np.clip(out + 0.5, 0, 255).astype(np.uint8)


def srgb_to_linear(c):
    c = np.asarray(c, dtype=np.float32)
    return np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)


def linear_to_srgb(c):
    c = np.clip(c, 0, 1)
    return np.where(c <= 0.0031308, 12.92 * c, 1.055 * np.power(c, 1 / 2.4) - 0.055)


def srgb_u8_to_lab(rgb_u8):
    lin = srgb_to_linear(rgb_u8.astype(np.float32) / 255.0)
    M = np.array([[0.4124, 0.3576, 0.1805],
                  [0.2126, 0.7152, 0.0722],
                  [0.0193, 0.1192, 0.9505]], dtype=np.float32)
    xyz = lin @ M.T
    wp = np.array([0.95047, 1.0, 1.08883], dtype=np.float32)
    r = xyz / wp
    eps, kap = 216 / 24389, 24389 / 27
    f = np.where(r > eps, np.cbrt(r), (kap * r + 16) / 116)
    L = 116 * f[..., 1] - 16
    a = 500 * (f[..., 0] - f[..., 1])
    b = 200 * (f[..., 1] - f[..., 2])
    Y = 0.2126 * lin[..., 0] + 0.7152 * lin[..., 1] + 0.0722 * lin[..., 2]
    return L, a, b, Y


def box_mean(a, k):
    """Media en ventana k x k (bordes reflejados por recorte, sin ventanas parciales)."""
    pad = k // 2
    ap = np.pad(a, ((pad + 1, pad), (pad + 1, pad)), mode="edge")
    c = np.cumsum(np.cumsum(ap, axis=0), axis=1)
    c = np.pad(c, ((1, 0), (1, 0)))
    H, W = a.shape
    s = c[k:k + H, k:k + W] - c[0:H, k:k + W] - c[k:k + H, 0:W] + c[0:H, 0:W]
    return s / (k * k)


def local_std(a, k):
    m = box_mean(a, k)
    m2 = box_mean(a * a, k)
    return np.sqrt(np.maximum(m2 - m * m, 0))


def block_std(a, b=32):
    """Desviación estándar en bloques no solapados b x b (recorta a múltiplos de b)."""
    H, W = a.shape
    h, w = (H // b) * b, (W // b) * b
    blk = a[:h, :w].reshape(h // b, b, w // b, b).swapaxes(1, 2).reshape(-1, b * b)
    return blk.std(axis=1)


def radial_bands(gray, edges=(2, 4, 8, 16, 32, 64, 128, 256, 512)):
    """Energía del espectro (ventana Hann, sin DC) en bandas de octava.
    Frecuencias en ciclos por imagen (ciclos por 1280 px). Devuelve fracciones que suman 1."""
    H, W = gray.shape
    g = gray - gray.mean()
    wy = np.hanning(H)[:, None]
    wx = np.hanning(W)[None, :]
    F = np.fft.rfft2(g * wy * wx)
    P = (np.abs(F) ** 2)
    fy = np.fft.fftfreq(H) * H
    fx = np.fft.rfftfreq(W) * W
    R = np.sqrt(fy[:, None] ** 2 + fx[None, :] ** 2)
    P[0, 0] = 0
    total = P.sum()
    out = {}
    lo_list = list(edges[:-1])
    hi_list = list(edges[1:])
    for lo, hi in zip(lo_list, hi_list):
        m = (R >= lo) & (R < hi)
        out[f"{lo}-{hi}"] = float(P[m].sum() / total)
    return out


def sobel_like_laplacian(a):
    p = np.pad(a, 1, mode="edge")
    return (p[:-2, 1:-1] + p[2:, 1:-1] + p[1:-1, :-2] + p[1:-1, 2:] - 4 * a)


def radial_spectrum_slope(gray, fmin=4, fmax=256):
    """Pendiente log-log del espectro de potencia radial (ciclos por imagen). Natural ~ -2 (amplitud 1/f)."""
    H, W = gray.shape
    g = gray - gray.mean()
    F = np.fft.rfft2(g * np.hanning(H)[:, None] * np.hanning(W)[None, :])
    P = np.abs(F) ** 2
    fy = np.fft.fftfreq(H) * H
    fx = np.fft.rfftfreq(W) * W
    R = np.rint(np.sqrt(fy[:, None] ** 2 + fx[None, :] ** 2)).astype(np.int64)
    sums = np.bincount(R.ravel(), weights=P.ravel())
    cnt = np.bincount(R.ravel())
    mean = sums / np.maximum(cnt, 1)
    f = np.arange(len(mean))
    sel = (f >= fmin) & (f <= fmax) & (mean > 0)
    slope, _ = np.polyfit(np.log10(f[sel]), np.log10(mean[sel]), 1)
    return float(slope)
