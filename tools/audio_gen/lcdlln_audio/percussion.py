"""Percussions synthétisées (tambours de guerre, métal, bois, peaux).

Chaque fonction renvoie un tableau mono float64 (crête ≈ 1) ; la graine
`seed` rend chaque frappe légèrement différente mais reproductible.
Modèle commun d'une peau : sinusoïde dont la hauteur chute à l'attaque
(membrane tendue par l'impact) + modes secondaires + bruit de frappe filtré.
"""

from __future__ import annotations

import numpy as np

from .dsp import SR, bandpass, highpass, lowpass, rng, samples


def _t(dur: float) -> np.ndarray:
    return np.arange(samples(dur)) / SR


def _drum(f0: float, decay: float, dur: float, seed: int, bend: float = 1.0, bend_tau: float = 0.03,
          modes=((1.0, 1.0, 1.0), (1.59, 0.35, 0.5), (2.14, 0.18, 0.35)),
          slap: float = 0.25, slap_band=(300.0, 3000.0), slap_tau: float = 0.012) -> np.ndarray:
    """Peau générique. `modes` : (rapport de fréquence, gain, facteur de durée)."""
    r = rng(seed)
    t = _t(dur)
    jitter = 1.0 + r.uniform(-0.02, 0.02)
    out = np.zeros_like(t)
    for ratio, gain, dfac in modes:
        f = f0 * jitter * ratio * (1.0 + bend * np.exp(-t / bend_tau))
        phase = 2 * np.pi * np.cumsum(f) / SR
        out += gain * np.sin(phase) * np.exp(-t / (decay * dfac))
    noise = bandpass(r.standard_normal(len(t)), *slap_band) * np.exp(-t / slap_tau)
    out += slap * noise / (np.max(np.abs(noise)) + 1e-9)
    # Attaque de 1 ms (pas de clic numérique).
    a = samples(0.001)
    out[:a] *= np.linspace(0.0, 1.0, a)
    return out / (np.max(np.abs(out)) + 1e-9)


def taiko(seed: int, f0: float = 52.0, decay: float = 1.1) -> np.ndarray:
    """Grand tambour de guerre (taiko/ō-daiko) : grave, long, la « DOOM »."""
    d = _drum(f0, decay, decay * 4.0, seed, bend=0.9, bend_tau=0.045,
              modes=((1.0, 1.0, 1.0), (1.5, 0.45, 0.55), (2.24, 0.2, 0.3), (0.5, 0.25, 1.4)),
              slap=0.35, slap_band=(120.0, 1600.0), slap_tau=0.02)
    # Sous-grave du fût (corps qui résonne).
    t = _t(decay * 4.0)
    sub = np.sin(2 * np.pi * f0 * 0.75 * t) * np.exp(-t / (decay * 1.6)) * (1 - np.exp(-t / 0.01))
    out = d + 0.35 * sub
    return out / (np.max(np.abs(out)) + 1e-9)


def battle_drum(seed: int, f0: float = 150.0, snare: float = 0.35) -> np.ndarray:
    """Tambour de bataille médium (caisse militaire / tom) : élan rythmique."""
    d = _drum(f0, 0.22, 0.9, seed, bend=0.6, bend_tau=0.02, slap=0.3, slap_band=(400.0, 4000.0))
    r = rng(seed + 7)
    t = _t(0.9)
    wires = bandpass(r.standard_normal(len(t)), 1800.0, 7000.0) * np.exp(-t / 0.09)
    wires /= np.max(np.abs(wires)) + 1e-9
    out = d + snare * wires
    return out / (np.max(np.abs(out)) + 1e-9)


def frame_drum(seed: int, f0: float = 92.0) -> np.ndarray:
    """Tambour sur cadre / bodhrán : grave mat et court."""
    return _drum(f0, 0.28, 1.0, seed, bend=0.5, bend_tau=0.025,
                 modes=((1.0, 1.0, 1.0), (1.71, 0.3, 0.4)), slap=0.45, slap_band=(250.0, 2500.0))


def djembe(seed: int, stroke: str = "tone") -> np.ndarray:
    """Djembé : `bass` (grave), `tone` (médium), `slap` (claquant)."""
    if stroke == "bass":
        return _drum(78.0, 0.25, 0.8, seed, bend=0.4, slap=0.15)
    if stroke == "slap":
        return _drum(380.0, 0.07, 0.4, seed, bend=0.2, slap=0.9, slap_band=(1500.0, 9000.0), slap_tau=0.01)
    return _drum(260.0, 0.12, 0.5, seed, bend=0.25, slap=0.35, slap_band=(600.0, 5000.0))


def finger_drum(seed: int) -> np.ndarray:
    """Petite frappe aiguë (tabla / doigts) pour la texture rapide."""
    return _drum(420.0, 0.05, 0.25, seed, bend=0.15, bend_tau=0.01,
                 modes=((1.0, 1.0, 1.0), (2.3, 0.4, 0.5)), slap=0.5, slap_band=(2000.0, 8000.0), slap_tau=0.006)


def shaker(seed: int, length: float = 0.09) -> np.ndarray:
    """Hochet : souffle aigu court."""
    r = rng(seed)
    t = _t(length * 2.0)
    env = (1 - np.exp(-t / (length * 0.25))) * np.exp(-t / (length * 0.35))
    x = bandpass(r.standard_normal(len(t)), 4000.0, 11000.0) * env
    return x / (np.max(np.abs(x)) + 1e-9)


def woodblock(seed: int, f: float = 1050.0) -> np.ndarray:
    """Bloc de bois : clac sec à hauteur définie."""
    return _drum(f, 0.045, 0.25, seed, bend=0.05, bend_tau=0.005,
                 modes=((1.0, 1.0, 1.0), (2.6, 0.35, 0.5), (4.1, 0.15, 0.3)), slap=0.2,
                 slap_band=(2000.0, 9000.0), slap_tau=0.003)


def _metal(partials, decays, dur: float, seed: int, attack: float = 0.0015, click: float = 0.3) -> np.ndarray:
    r = rng(seed)
    t = _t(dur)
    out = np.zeros_like(t)
    for (f, g), d in zip(partials, decays):
        f *= 1.0 + r.uniform(-0.01, 0.01)
        out += g * np.sin(2 * np.pi * f * t + r.uniform(0, 6.28)) * np.exp(-t / d)
    ck = highpass(r.standard_normal(len(t)), 3000.0) * np.exp(-t / 0.004)
    out += click * ck / (np.max(np.abs(ck)) + 1e-9)
    a = samples(attack)
    out[:a] *= np.linspace(0.0, 1.0, a)
    return out / (np.max(np.abs(out)) + 1e-9)


def anvil(seed: int, f0: float = 820.0) -> np.ndarray:
    """Coup d'enclume : partiels inharmoniques brillants, longue résonance."""
    ratios = [(1.0, 1.0), (2.76, 0.6), (5.40, 0.35), (8.93, 0.2), (1.47, 0.25)]
    partials = [(f0 * k, g) for k, g in ratios]
    return _metal(partials, [1.8, 1.1, 0.7, 0.4, 0.9], 3.0, seed, click=0.5)


def gong(seed: int, f0: float = 68.0, dur: float = 9.0) -> np.ndarray:
    """Gong : montée lente (« swell »), battements, très longue décroissance."""
    r = rng(seed)
    t = _t(dur)
    ratios = [1.0, 1.52, 2.03, 2.61, 3.27, 4.08, 5.11, 6.34, 7.9]
    out = np.zeros_like(t)
    for i, k in enumerate(ratios):
        f = f0 * k * (1.0 + r.uniform(-0.004, 0.004))
        beat = 1.0 + 0.15 * np.sin(2 * np.pi * r.uniform(0.3, 1.2) * t)
        swell = 1 - np.exp(-t / (0.05 + 0.08 * i))
        out += (0.9 ** i) * np.sin(2 * np.pi * f * t) * beat * swell * np.exp(-t / (dur * 0.45 / (1 + 0.25 * i)))
    hit = lowpass(r.standard_normal(len(t)), 900.0) * np.exp(-t / 0.05)
    out += 0.4 * hit / (np.max(np.abs(hit)) + 1e-9)
    return out / (np.max(np.abs(out)) + 1e-9)


def chains(seed: int, dur: float = 0.7, grains: int = 28) -> np.ndarray:
    """Chaînes secouées : nuée de petits chocs métalliques aigus."""
    r = rng(seed)
    out = np.zeros(samples(dur + 0.1))
    for _ in range(grains):
        start = samples(abs(r.normal(0.18, 0.15)) % dur)
        g = _metal([(r.uniform(2200, 7500), 1.0), (r.uniform(3000, 9000), 0.5)], [0.03, 0.02], 0.08,
                   int(r.integers(1 << 30)), click=0.6) * r.uniform(0.2, 1.0)
        out[start:start + len(g)] += g[: len(out) - start]
    return out / (np.max(np.abs(out)) + 1e-9)


def cinematic_boom(seed: int, f0: float = 38.0) -> np.ndarray:
    """Impact sub-grave (temps forts, fins de sections)."""
    t = _t(4.0)
    f = f0 * (1 + 1.5 * np.exp(-t / 0.06))
    x = np.sin(2 * np.pi * np.cumsum(f) / SR) * np.exp(-t / 1.2)
    r = rng(seed)
    n = lowpass(r.standard_normal(len(t)), 400.0) * np.exp(-t / 0.15)
    x += 0.3 * n / (np.max(np.abs(n)) + 1e-9)
    return x / (np.max(np.abs(x)) + 1e-9)
