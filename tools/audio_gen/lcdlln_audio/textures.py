"""Textures d'ambiance synthétisées : vent, tonnerre lointain, souffle grave."""

from __future__ import annotations

import numpy as np

from .dsp import SR, bandpass, brown, lowpass, pink, rng, samples


def wind(dur: float, seed: int, lo: float = 180.0, hi: float = 1400.0, gust_hz: float = 0.07) -> np.ndarray:
    """Vent stéréo : bruit rose filtré dont la bande et le niveau ondulent (rafales).

    Le filtrage est fait par blocs de 50 ms avec une fréquence centrale qui
    glisse ; le résultat est stéréo (deux réalisations décorrélées).
    """
    r = rng(seed)
    n = samples(dur)
    out = np.zeros((n, 2), dtype=np.float32)
    t = (np.arange(n) / SR).astype(np.float32)
    for ch in range(2):
        src = pink(n, r)
        lfo = 0.5 + 0.5 * np.sin(2 * np.pi * gust_hz * t + r.uniform(0, 6.28)) \
            * (0.6 + 0.4 * np.sin(2 * np.pi * gust_hz * 2.7 * t + r.uniform(0, 6.28)))
        # Deux bandes (sourde et sifflante) mêlées selon la rafale.
        low = bandpass(src, lo, lo * 3.0)
        high = bandpass(src, hi * 0.6, hi)
        x = low * (0.6 + 0.4 * lfo) + high * (lfo ** 2) * 0.6
        out[:, ch] = x * (0.35 + 0.65 * lfo)
    return out / (np.max(np.abs(out)) + 1e-9)


def thunder(seed: int, dur: float = 7.0) -> np.ndarray:
    """Tonnerre lointain : grondement grave en plusieurs roulements."""
    r = rng(seed)
    n = samples(dur)
    t = np.arange(n) / SR
    base = lowpass(brown(n, r), 260.0, 2)
    env = np.zeros(n)
    k = 0.0
    while k < dur * 0.7:
        center = k + r.uniform(0.1, 0.6)
        width = r.uniform(0.3, 1.2)
        env += r.uniform(0.4, 1.0) * np.exp(-((t - center) / width) ** 2)
        k = center + r.uniform(0.2, 0.9)
    env *= np.exp(-t / (dur * 0.5))
    x = base * env
    return x / (np.max(np.abs(x)) + 1e-9)


def drone_noise(dur: float, seed: int, center: float = 90.0) -> np.ndarray:
    """Souffle grave stéréo (respiration du monde, sous les cordes)."""
    r = rng(seed)
    n = samples(dur)
    out = np.zeros((n, 2), dtype=np.float32)
    for ch in range(2):
        out[:, ch] = bandpass(brown(n, r), center * 0.6, center * 1.8)
    return out / (np.max(np.abs(out)) + 1e-9)
