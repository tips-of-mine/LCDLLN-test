"""SND.2 / SND.2b — Bruits de pas synthétisés, par surface.

SND.2b (refonte) : le talon n'est plus un sinus grave amorti (le « tom » qu'on
entendait sur les routes, la terre, la roche et le bois) mais un IMPACT DE BRUIT
filtré, sans hauteur, comme une semelle de cuir qui frappe le sol. Chaque pas
enchaîne : impact du talon, déroulé (frottement de semelle) et appui de la
pointe, plus la TEXTURE propre au sol (brins d'herbe, grains, crissement de
neige, succion de boue, résonance creuse d'une planche, éclat du marbre…).

Surfaces refaites en SND.2b (6 variantes) : terre, terre battue (routes, rues,
places), herbe, gravier, pierre, marbre, neige, sable, boue, eau, bois.
Surfaces inchangées (4 variantes, octet pour octet) : nage, blé, maïs.

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

# Surface → fichier de base (ids du catalogue zone_audio.json : step_<nom>).
# L'ORDRE fixe la graine de chaque surface (1000 × rang + variante) : on n'ajoute
# qu'en fin de liste, pour que les surfaces inchangées restent identiques.
SURFACES = ["grass", "dirt", "gravel", "rock", "sand", "snow_crunch", "mud_squelch", "wood",
            "water", "swim_stroke", "wheat_rustle", "corn_rustle", "packed_earth", "marble"]
# Surfaces gardées telles quelles depuis SND.2 (algorithme et graines inchangés).
LEGACY = ("swim_stroke", "wheat_rustle", "corn_rustle")
# Variantes par surface : 6 pour les sols refaits, 4 pour les sols inchangés.
VARIANTS = {s: (4 if s in LEGACY else 6) for s in SURFACES}


def _t(dur: float) -> np.ndarray:
    return np.arange(dsp.samples(dur)) / dsp.SR


def _env(t: np.ndarray, attack: float, decay: float) -> np.ndarray:
    """Enveloppe attaque linéaire + décroissance exponentielle."""
    a = np.clip(t / max(attack, 1e-4), 0.0, 1.0)
    return a * np.exp(-np.maximum(t - attack, 0.0) / decay)


def _thump_tonal(r, dur=0.25, f0=95.0, decay=0.035, noise=0.5, lp=900.0) -> np.ndarray:
    """SND.2 — choc d'appui à sinus grave amorti. Gardé UNIQUEMENT pour les surfaces
    LEGACY (blé, maïs) afin qu'elles restent identiques ; les sols refaits utilisent
    impact() (bruit filtré, sans hauteur)."""
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


# ----------------------------------------------------------------- SND.2b

def impact(r, dur, lp, decay, hp=60.0, attack=0.0015) -> np.ndarray:
    """Impact de talon SANS hauteur : salve de bruit filtrée passe-bas (semelle de
    cuir qui frappe). Remplace le sinus amorti de SND.2 (effet « tambour »)."""
    t = _t(dur)
    n = dsp.bandpass(r.standard_normal(len(t)), hp, lp * r.uniform(0.85, 1.15), 2)
    return n * _env(t, attack, decay * r.uniform(0.85, 1.15))


def resonance(r, dur, bands, decay) -> np.ndarray:
    """Résonance d'un matériau excitée par du bruit (bandes larges, inharmoniques) :
    corps creux d'une planche, chant bref de la pierre. Aucune sinusoïde pure."""
    t = _t(dur)
    n = r.standard_normal(len(t))
    out = np.zeros_like(t)
    for f, width, g in bands:
        fc = f * r.uniform(0.92, 1.08)
        out += g * dsp.bandpass(n, fc / width, fc * width, 2)
    return out * _env(t, 0.001, decay * r.uniform(0.85, 1.15))


def scuff(r, dur, lo, hi, attack, decay) -> np.ndarray:
    """Frottement de semelle (déroulé du pied) : souffle filtré à attaque douce."""
    t = _t(dur)
    return dsp.bandpass(r.standard_normal(len(t)), lo, hi, 2) * _env(t, attack, decay)


def crackle(r, dur, count, lo, hi, grain_ms=(0.6, 2.5), shape=1.0, start=0.0) -> np.ndarray:
    """Nuée de micro-chocs répartis dans le temps. `shape` > 1 les concentre vers
    la fin (compression : neige), < 1 vers le début (grains chassés : gravier)."""
    out = np.zeros(dsp.samples(dur))
    for _ in range(count):
        g = dsp.samples(r.uniform(*grain_ms) / 1000.0)
        pos = start + (1.0 - start) * (r.random() ** (1.0 / shape))
        s0 = min(int(pos * len(out)), max(0, len(out) - g - 1))
        tt = np.arange(g) / dsp.SR
        out[s0:s0 + g] += r.standard_normal(g) * np.exp(-tt / (g / dsp.SR / 3.0)) * r.uniform(0.2, 1.0)
    return dsp.bandpass(out, lo, hi, 2)


def creak(r, dur) -> np.ndarray:
    """Grincement de planche : frottement stick-slip (train d'impulsions dont la
    cadence glisse) filtré dans le médium."""
    t = _t(dur)
    rate = r.uniform(180, 320) * (1 + r.uniform(-0.25, 0.35) * t / dur)
    phase = np.cumsum(rate) / dsp.SR
    pulses = (np.diff(np.floor(phase), prepend=0.0) > 0).astype(float)
    pulses *= 0.6 + 0.4 * r.random(len(t))
    x = dsp.bandpass(pulses + 0.15 * r.standard_normal(len(t)), 500, 2600, 2)
    return x * np.sin(np.pi * t / dur) ** 2


def footfall(r, heel, toe, gap=(0.05, 0.085), roll=None) -> np.ndarray:
    """Talon puis pointe (plus douce), avec un frottement de déroulé optionnel."""
    h = heel(r, 1.0)
    o = toe(r, 0.4)
    off = dsp.samples(r.uniform(*gap))
    out = np.zeros(max(len(h), off + len(o)) + dsp.samples(0.02))
    out[: len(h)] += h
    out[off: off + len(o)] += o
    if roll is not None:
        rl = roll(r)
        a = dsp.samples(0.01)
        e = min(len(out), a + len(rl))
        out[a:e] += rl[: e - a]
    return out


def make_v2(surface: str, r) -> np.ndarray:
    """Un pas SND.2b sur `surface` (avant normalisation)."""
    if surface == "packed_earth":
        # Terre battue (routes, rues, places) : appui sec et compact, un peu de grit.
        x = footfall(r,
                     lambda r, k: mix((impact(r, 0.14, 1100, 0.018), 0.8 * k),
                                      (crackle(r, 0.14, 14, 1500, 6000), 0.35 * k)),
                     lambda r, k: mix((impact(r, 0.10, 1400, 0.012), 0.8 * k),
                                      (crackle(r, 0.10, 8, 2000, 7000), 0.3 * k)),
                     roll=lambda r: 0.18 * scuff(r, 0.12, 900, 4500, 0.03, 0.03))
    elif surface == "dirt":
        # Terre meuble : appui plus étouffé, mottes qui s'effritent.
        x = footfall(r,
                     lambda r, k: mix((impact(r, 0.18, 650, 0.03), 0.8 * k),
                                      (crackle(r, 0.18, 26, 350, 2600, (1.5, 4.0), 0.8), 0.45 * k)),
                     lambda r, k: mix((impact(r, 0.12, 800, 0.02), 0.7 * k),
                                      (crackle(r, 0.12, 12, 500, 3000, (1.5, 4.0)), 0.4 * k)))
    elif surface == "grass":
        # Herbe : presque pas d'impact, surtout le froissement des brins.
        def one(r, k):
            sw = scuff(r, 0.22, 1100, 5000, 0.012, 0.05)
            sw *= 0.6 + 0.4 * dsp.lowpass(r.random(len(sw)), 60.0, 1) / 0.5
            return mix((impact(r, 0.12, 380, 0.025), 0.35 * k), (sw, 0.9 * k),
                       (crackle(r, 0.2, 10, 2500, 7000, (0.4, 1.2)), 0.18 * k))
        x = footfall(r, one, one, gap=(0.06, 0.1))
    elif surface == "gravel":
        # Gravier : crissement de cailloux chassés sous la semelle.
        x = footfall(r,
                     lambda r, k: mix((impact(r, 0.16, 900, 0.02), 0.45 * k),
                                      (crackle(r, 0.2, int(170 * k) + 40, 900, 7000, (1.0, 3.5), 0.6), 1.0 * k)),
                     lambda r, k: mix((impact(r, 0.1, 1000, 0.012), 0.4 * k),
                                      (crackle(r, 0.14, int(110 * k) + 30, 1100, 7500, (1.0, 3.0), 0.7), 1.0 * k)))
    elif surface == "rock":
        # Pierre : claquement sec et dur, court chant de la roche, grit de surface.
        def one(r, k):
            click = impact(r, 0.04, 7000, 0.004, hp=1500, attack=0.0003)
            return mix((impact(r, 0.1, 1800, 0.01, hp=120), 0.55 * k),
                       (resonance(r, 0.12, ((900, 1.25, 1.0), (2200, 1.2, 0.6)), 0.02), 0.35 * k),
                       (click, 0.7 * k), (crackle(r, 0.08, 6, 2500, 8000), 0.2 * k))
        x = footfall(r, one, one, gap=(0.06, 0.09))
    elif surface == "marble":
        # Marbre poli : éclat net et brillant, très peu de grit, petite queue claire.
        def one(r, k):
            click = impact(r, 0.05, 10000, 0.003, hp=2500, attack=0.0002)
            return mix((impact(r, 0.08, 2400, 0.008, hp=200), 0.45 * k),
                       (resonance(r, 0.25, ((1600, 1.12, 0.8), (3300, 1.1, 0.6), (5200, 1.08, 0.35)), 0.06), 0.4 * k),
                       (click, 0.85 * k))
        x = footfall(r, one, one, gap=(0.06, 0.09))
    elif surface == "snow_crunch":
        # Neige : crissement de compression, de plus en plus dense, grave étouffé.
        x = footfall(r,
                     lambda r, k: mix((impact(r, 0.14, 450, 0.03), 0.35 * k),
                                      (crackle(r, 0.26, int(320 * k) + 80, 450, 4200, (0.8, 2.5), 1.8), 1.0 * k)),
                     lambda r, k: mix((crackle(r, 0.16, int(200 * k) + 50, 500, 4500, (0.8, 2.5), 1.5), 1.0 * k),),
                     gap=(0.08, 0.12))
    elif surface == "sand":
        # Sable : appui mou et feutré, glissement granuleux de grains fins.
        def one(r, k):
            sh = scuff(r, 0.24, 600, 4000, 0.02, 0.06)
            return mix((impact(r, 0.12, 320, 0.03), 0.3 * k), (sh, 0.85 * k),
                       (crackle(r, 0.22, 60, 2000, 7000, (0.3, 0.8), 1.2), 0.25 * k))
        x = footfall(r, one, one, gap=(0.07, 0.11))
    elif surface == "mud_squelch":
        # Boue : claque mouillée à l'appui, succion au décollement, petites bulles.
        def heel(r, k):
            t = _t(0.3)
            n = r.standard_normal(len(t))
            suck = np.zeros_like(t)
            for i, c in enumerate(np.linspace(240, 950, 7)):
                a, b = int(i * len(t) / 7), int((i + 1) * len(t) / 7)
                suck[a:b] = dsp.bandpass(n, c * 0.8, c * 1.25)[a:b]
            suck *= _env(t, 0.03, 0.08)
            return mix((impact(r, 0.12, 700, 0.02), 0.6 * k), (splash(r, 0.1, 500, 3500), 0.35 * k),
                       (suck, 0.9 * k), (bubbles(r, 0.3, 4), 0.2 * k))
        x = footfall(r, heel, lambda r, k: mix((splash(r, 0.12, 400, 3000), 0.6 * k), (bubbles(r, 0.15, 3), 0.3 * k)),
                     gap=(0.1, 0.15))
    elif surface == "water":
        # Eau peu profonde : claque du pied dans l'eau puis remous et gouttes.
        slap = impact(r, 0.08, 3000, 0.01, hp=300)
        x = mix((slap, 0.5), (splash(r, 0.3, 350, 6500), 1.0), (bubbles(r, 0.36, 12), 0.45),
                (crackle(r, 0.36, 18, 2000, 8000, (2.0, 6.0), 1.0, 0.3), 0.25))
    elif surface == "wood":
        # Plancher : impact creux de planche, résonance de caisse (bruit filtré),
        # grincement sur une variante sur trois.
        def one(r, k):
            return mix((impact(r, 0.1, 2500, 0.006, hp=300, attack=0.0004), 0.55 * k),
                       (resonance(r, 0.3, ((150, 1.3, 1.0), (330, 1.25, 0.6), (620, 1.2, 0.3)), 0.055), 0.9 * k),
                       (impact(r, 0.14, 500, 0.03, hp=80), 0.35 * k))
        x = footfall(r, one, one, gap=(0.06, 0.09))
        if r.random() < 0.34:
            c = creak(r, r.uniform(0.15, 0.25))
            s0 = dsp.samples(0.05)
            y = np.zeros(max(len(x), s0 + len(c)))
            y[: len(x)] += x
            y[s0:s0 + len(c)] += 0.25 * np.max(np.abs(x)) * c / (np.max(np.abs(c)) + 1e-9)
            x = y
    else:
        raise ValueError(surface)
    return x


def make_legacy(surface: str, r) -> np.ndarray:
    """SND.2 — surfaces inchangées (nage, blé, maïs) : même code, mêmes graines."""
    if surface == "swim_stroke":
        t = _t(0.7)
        wave = dsp.bandpass(r.standard_normal(len(t)), 300, 4000) * _env(t, 0.12, 0.2)
        x = mix((wave, 1.0), (bubbles(r, 0.7, 22), 0.6), (splash(r, 0.4, 800, 7000), 0.4))
    elif surface == "wheat_rustle":
        x = mix((rustle(r, 0.38, 2500, 9000, 0.5), 1.0), (grains(r, 0.38, 70, 3000, 9500), 0.5),
                (_thump_tonal(r, 0.2, 90, 0.03, 0.2), 0.3))
    elif surface == "corn_rustle":
        x = mix((rustle(r, 0.42, 1200, 6000, 0.5), 1.0), (grains(r, 0.42, 50, 1500, 7000, (3.0, 7.0)), 0.6),
                (_thump_tonal(r, 0.2, 90, 0.03, 0.2), 0.3))
    else:
        raise ValueError(surface)
    return x


def make(surface: str, r) -> np.ndarray:
    """Un pas sur `surface` (mono, crête 1)."""
    x = make_legacy(surface, r) if surface in LEGACY else make_v2(surface, r)
    x = dsp.highpass(x, 40.0, 2)
    # Début (1 ms) et fin (20 ms) en fondu : aucun clic numérique.
    fin = dsp.samples(0.001)
    x[:fin] *= np.linspace(0.0, 1.0, fin)
    fade = dsp.samples(0.02)
    x[-fade:] *= np.linspace(1.0, 0.0, fade)
    return x / (np.max(np.abs(x)) + 1e-9)


# Niveau relatif par surface (dBFS de crête) : l'eau et la nage plus présentes ;
# sols mous (herbe, sable) plus discrets, sols durs (pierre, marbre) plus nets.
LEVEL_DB = {"grass": -10, "dirt": -9, "gravel": -8, "rock": -8, "sand": -11, "snow_crunch": -8,
            "mud_squelch": -8, "wood": -7, "water": -6, "swim_stroke": -6, "wheat_rustle": -9, "corn_rustle": -9,
            "packed_earth": -8, "marble": -8}


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--out", default="game/data/audio/sfx/footsteps")
    a = ap.parse_args()
    out = Path(a.out)
    for k, surface in enumerate(SURFACES):
        for v in range(VARIANTS[surface]):
            r = dsp.rng(1000 * k + v + 1)
            x = make(surface, r) * dsp.from_db(LEVEL_DB[surface])
            dsp.write_wav16(x.astype(np.float32), out / f"{surface}_{v + 1}.wav")
    print(f"écrit : {sum(VARIANTS.values())} pas dans {out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
