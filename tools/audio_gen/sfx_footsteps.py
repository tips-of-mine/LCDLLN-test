"""SND.2 — Bruits de pas synthétisés (12 surfaces × 4 variantes).

Chaque pas = un appui de TALON (choc sourd, grave) suivi ~70 ms plus tard de
la POINTE du pied (plus léger), plus la TEXTURE de la surface : bruissement
d'herbe, grains de gravier, craquement de neige, clapotis d'eau…
Les variantes changent la graine (grains, timing, hauteur) pour qu'un même sol
ne sonne jamais deux fois pareil ; le moteur ajoute encore ±hauteur/volume.

Sortie : game/data/audio/sfx/footsteps/<surface>_<n>.wav (mono, 48 kHz, 16 bits).
Usage :  python3 sfx_footsteps.py [--out game/data/audio/sfx/footsteps]
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))

from lcdlln_audio import dsp  # noqa: E402

VARIANTS = 4
# Surface → fichier de base (ids de surface_table.json : step_<nom>).
SURFACES = ["grass", "dirt", "gravel", "rock", "sand", "snow_crunch", "mud_squelch", "wood",
            "water", "swim_stroke", "wheat_rustle", "corn_rustle"]


def _t(dur: float) -> np.ndarray:
    return np.arange(dsp.samples(dur)) / dsp.SR


def _env(t: np.ndarray, attack: float, decay: float) -> np.ndarray:
    """Enveloppe attaque linéaire + décroissance exponentielle."""
    a = np.clip(t / max(attack, 1e-4), 0.0, 1.0)
    return a * np.exp(-np.maximum(t - attack, 0.0) / decay)


def thump(r, dur=0.25, f0=95.0, decay=0.035, noise=0.5, lp=900.0) -> np.ndarray:
    """Choc sourd d'appui (talon) : sinus grave amorti + souffle filtré."""
    t = _t(dur)
    body = np.sin(2 * np.pi * f0 * (1 + r.uniform(-0.1, 0.1)) * t) * np.exp(-t / decay)
    n = dsp.lowpass(r.standard_normal(len(t)), lp, 2) * _env(t, 0.002, decay * 0.6)
    return body + noise * n / (np.max(np.abs(n)) + 1e-9)


def grains(r, dur, count, lo, hi, grain_ms=(1.5, 4.0), spread=1.0) -> np.ndarray:
    """Nuée de petits chocs (grains de gravier, cristaux de neige, brindilles)."""
    out = np.zeros(dsp.samples(dur))
    for _ in range(count):
        g = dsp.samples(r.uniform(*grain_ms) / 1000.0)
        start = int(abs(r.normal(0.25, 0.22 * spread)) * len(out)) % max(1, len(out) - g)
        tt = np.arange(g) / dsp.SR
        burst = r.standard_normal(g) * np.exp(-tt / (g / dsp.SR / 3.0))
        out[start:start + g] += burst * r.uniform(0.3, 1.0)
    return dsp.bandpass(out, lo, hi, 2)


def rustle(r, dur, lo, hi, density=0.6) -> np.ndarray:
    """Bruissement (herbe, feuilles sèches) : souffle filtré granuleux."""
    t = _t(dur)
    n = r.standard_normal(len(t)) * (r.random(len(t)) < density)
    return dsp.bandpass(n, lo, hi, 2) * _env(t, 0.015, dur * 0.3)


def bubbles(r, dur, count) -> np.ndarray:
    """Bulles : petits glissandos sinusoïdaux montants."""
    out = np.zeros(dsp.samples(dur))
    for _ in range(count):
        d = r.uniform(0.012, 0.04)
        tt = _t(d)
        f = r.uniform(700, 1800) * (1 + 1.5 * tt / d)
        b = np.sin(2 * np.pi * np.cumsum(f) / dsp.SR) * np.sin(np.pi * tt / d)
        s = int(r.uniform(0.0, 0.85) * len(out))
        e = min(len(out), s + len(b))
        out[s:e] += b[: e - s] * r.uniform(0.2, 0.6)
    return out


def splash(r, dur, lo=400.0, hi=6000.0) -> np.ndarray:
    t = _t(dur)
    return dsp.bandpass(r.standard_normal(len(t)), lo, hi, 2) * _env(t, 0.01, dur * 0.25)


def mix(*parts) -> np.ndarray:
    n = max(len(p) for p, _ in parts)
    out = np.zeros(n)
    for p, g in parts:
        out[: len(p)] += g * p / (np.max(np.abs(p)) + 1e-9)
    return out


def step_pair(r, make_one) -> np.ndarray:
    """Talon puis pointe (~60–90 ms), la pointe plus douce."""
    heel = make_one(r, 1.0)
    toe = make_one(r, 0.45)
    off = dsp.samples(r.uniform(0.06, 0.09))
    out = np.zeros(max(len(heel), off + len(toe)))
    out[: len(heel)] += heel
    out[off: off + len(toe)] += toe
    return out


def make(surface: str, r) -> np.ndarray:
    """Un pas sur `surface` (mono, crête 1)."""
    if surface == "grass":
        x = step_pair(r, lambda r, k: mix((thump(r, 0.2, 90, 0.03, 0.3), 0.5 * k),
                                          (rustle(r, 0.22, 1800, 7000, 0.7), 0.8 * k),
                                          (grains(r, 0.2, 25, 2500, 8000), 0.3 * k)))
    elif surface == "dirt":
        x = step_pair(r, lambda r, k: mix((thump(r, 0.22, 100, 0.04, 0.6, 1200), 0.9 * k),
                                          (grains(r, 0.12, 30, 400, 2500), 0.5 * k)))
    elif surface == "gravel":
        x = step_pair(r, lambda r, k: mix((thump(r, 0.2, 105, 0.03, 0.5), 0.5 * k),
                                          (grains(r, 0.18, int(140 * k) + 40, 1000, 6500), 1.0 * k)))
    elif surface == "rock":
        def one(r, k):
            t = _t(0.18)
            ping = sum(np.sin(2 * np.pi * f * t) * np.exp(-t / d) for f, d in
                       ((r.uniform(1100, 1500), 0.02), (r.uniform(2300, 3100), 0.012)))
            scuff = dsp.bandpass(r.standard_normal(len(t)), 2000, 8000) * _env(t, 0.001, 0.015)
            return mix((thump(r, 0.18, 110, 0.025, 0.4, 2000), 0.7 * k), (ping, 0.35 * k), (scuff, 0.6 * k))
        x = step_pair(r, one)
    elif surface == "sand":
        x = step_pair(r, lambda r, k: mix((thump(r, 0.18, 80, 0.03, 0.2, 600), 0.4 * k),
                                          (rustle(r, 0.18, 900, 5000, 0.9), 0.8 * k)))
    elif surface == "snow_crunch":
        x = step_pair(r, lambda r, k: mix((thump(r, 0.22, 85, 0.03, 0.3, 700), 0.4 * k),
                                          (grains(r, 0.2, int(220 * k) + 60, 500, 3500, (1.0, 3.0), 0.7), 1.0 * k)))
    elif surface == "mud_squelch":
        def one(r, k):
            t = _t(0.3)
            n = r.standard_normal(len(t))
            # Résonance qui glisse vers l'aigu : le « succion » de la boue.
            out = np.zeros_like(t)
            for i, c in enumerate(np.linspace(260, 900, 6)):
                a, b = int(i * len(t) / 6), int((i + 1) * len(t) / 6)
                out[a:b] = dsp.bandpass(n, c * 0.8, c * 1.25)[a:b]
            out *= _env(t, 0.02, 0.09)
            return mix((thump(r, 0.2, 75, 0.04, 0.3, 500), 0.6 * k), (out, 0.9 * k))
        x = step_pair(r, one)
    elif surface == "wood":
        def one(r, k):
            t = _t(0.3)
            body = sum(g * np.sin(2 * np.pi * f * (1 + r.uniform(-0.03, 0.03)) * t) * np.exp(-t / d)
                       for f, g, d in ((140, 1.0, 0.09), (235, 0.7, 0.07), (410, 0.4, 0.05), (720, 0.2, 0.03)))
            click = dsp.bandpass(r.standard_normal(len(t)), 1500, 6000) * _env(t, 0.0005, 0.006)
            return mix((body, 1.0 * k), (click, 0.4 * k))
        x = step_pair(r, one)
    elif surface == "water":
        x = mix((splash(r, 0.32), 1.0), (bubbles(r, 0.32, 10), 0.5), (thump(r, 0.2, 70, 0.04, 0.2, 500), 0.3))
    elif surface == "swim_stroke":
        t = _t(0.7)
        wave = dsp.bandpass(r.standard_normal(len(t)), 300, 4000) * _env(t, 0.12, 0.2)
        x = mix((wave, 1.0), (bubbles(r, 0.7, 22), 0.6), (splash(r, 0.4, 800, 7000), 0.4))
    elif surface == "wheat_rustle":
        x = mix((rustle(r, 0.38, 2500, 9000, 0.5), 1.0), (grains(r, 0.38, 70, 3000, 9500), 0.5),
                (thump(r, 0.2, 90, 0.03, 0.2), 0.3))
    elif surface == "corn_rustle":
        x = mix((rustle(r, 0.42, 1200, 6000, 0.5), 1.0), (grains(r, 0.42, 50, 1500, 7000, (3.0, 7.0)), 0.6),
                (thump(r, 0.2, 90, 0.03, 0.2), 0.3))
    else:
        raise ValueError(surface)
    x = dsp.highpass(x, 40.0, 2)
    # Début (1 ms) et fin (20 ms) en fondu : aucun clic numérique.
    fin = dsp.samples(0.001)
    x[:fin] *= np.linspace(0.0, 1.0, fin)
    fade = dsp.samples(0.02)
    x[-fade:] *= np.linspace(1.0, 0.0, fade)
    return x / (np.max(np.abs(x)) + 1e-9)


# Niveau relatif par surface (dBFS de crête) : l'eau et la nage plus présentes.
LEVEL_DB = {"grass": -9, "dirt": -8, "gravel": -8, "rock": -7, "sand": -11, "snow_crunch": -8,
            "mud_squelch": -8, "wood": -7, "water": -6, "swim_stroke": -6, "wheat_rustle": -9, "corn_rustle": -9}


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--out", default="game/data/audio/sfx/footsteps")
    a = ap.parse_args()
    out = Path(a.out)
    for k, surface in enumerate(SURFACES):
        for v in range(VARIANTS):
            r = dsp.rng(1000 * k + v + 1)
            x = make(surface, r) * dsp.from_db(LEVEL_DB[surface])
            dsp.write_wav16(x.astype(np.float32), out / f"{surface}_{v + 1}.wav")
    print(f"écrit : {len(SURFACES) * VARIANTS} pas dans {out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
