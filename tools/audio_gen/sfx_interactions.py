"""SND.6 — Sons de récolte et d'artisanat (synthétisés).

Boucles (jouées tant que la barre de récolte / d'artisanat avance) :
  harvest_mining    pioche sur la roche, éclats de pierre        (filon de minerai)
  harvest_wood      hache dans le bois, échardes                 (arbre)
  harvest_herb      feuillage froissé, tiges cassées             (plante)
  harvest_skinning  couteau qui tranche, cuir                    (dépouille d'animal)
  craft_forge       marteau sur l'enclume, braises               (forge)
  craft_alchemy     bulles, frémissement, fioles qui tintent     (alchimie)
  craft_leather     cuir étiré, alêne qui perce                  (travail du cuir, couture)
  craft_generic     petits coups de bois                         (autres métiers)
Sons ponctuels :
  harvest_complete  récolte terminée
  craft_complete    objet fabriqué
  craft_complete_rare  objet rare ou épique (scintillement en plus)

Sortie : game/data/audio/sfx/interactions/<id>.wav (16 bits, 48 kHz).
Usage :  python3 sfx_interactions.py [--out game/data/audio/sfx/interactions]
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))

from lcdlln_audio import dsp, percussion as perc  # noqa: E402

# Durée de chaque boucle (s) : un cycle de geste complet.
LOOPS = {
    "harvest_mining": 2.0,
    "harvest_wood": 2.4,
    "harvest_herb": 3.0,
    "harvest_skinning": 2.0,
    "craft_forge": 2.0,
    "craft_alchemy": 4.0,
    "craft_leather": 3.0,
    "craft_generic": 2.0,
}


def _t(dur: float) -> np.ndarray:
    return np.arange(dsp.samples(dur)) / dsp.SR


def _exp(t, tau):
    return np.exp(-t / tau)


def _n(x):
    return x / (np.max(np.abs(x)) + 1e-9)


def _at(dst: np.ndarray, src: np.ndarray, t: float, g: float = 1.0) -> None:
    i = dsp.samples(t)
    if i >= len(dst):
        return
    j = min(len(dst), i + len(src))
    dst[i:j] += src[: j - i] * g


def room(x: np.ndarray, wet=0.12, t60=0.5) -> np.ndarray:
    ir = dsp.make_ir(t60, dsp.rng(21), predelay=0.005, damping_hz=5000.0)
    y = dsp.reverb(x, ir, wet=wet, dry=1.0)
    return y.mean(axis=1) if y.ndim == 2 else y


# ----------------------------------------------------------------- matières

def rock_strike(r) -> np.ndarray:
    """Pointe de pioche sur la roche : tintement bref + craquement de pierre."""
    clink = perc._metal([(r.uniform(2300, 2900), 1.0), (r.uniform(4100, 5200), 0.5), (r.uniform(6500, 7800), 0.3)],
                        [0.05, 0.03, 0.02], 0.25, int(r.integers(1 << 30)), click=0.8)
    t = _t(0.25)
    crunch = dsp.bandpass(r.standard_normal(len(t)), 500, 4000) * _exp(t, 0.03)
    thud = np.sin(2 * np.pi * 110 * t) * _exp(t, 0.03)
    return 0.6 * clink + 0.7 * _n(crunch) + 0.4 * thud


def pebbles(r, dur=0.6, count=10) -> np.ndarray:
    out = np.zeros(dsp.samples(dur))
    for _ in range(count):
        d = 0.02
        t = _t(d)
        p = np.sin(2 * np.pi * r.uniform(1800, 4500) * t) * _exp(t, 0.004)
        _at(out, p, abs(r.normal(0.12, 0.12)) % (dur - d), r.uniform(0.1, 0.5))
    return out


def axe_chop(r) -> np.ndarray:
    t = _t(0.35)
    thunk = perc._drum(r.uniform(130, 170), 0.05, 0.3, int(r.integers(1 << 30)), bend=0.6, slap=0.7,
                       slap_band=(600, 4000))
    crack = dsp.bandpass(r.standard_normal(len(t)), 1500, 7000) * _exp(t, 0.015)
    out = np.zeros(len(t))
    _at(out, thunk, 0.0, 1.0)
    _at(out, _n(crack), 0.002, 0.5)
    return out


def rustle(r, dur=0.4, lo=1800.0, hi=8000.0) -> np.ndarray:
    t = _t(dur)
    grains = (r.random(len(t)) < 0.02).astype(float) * r.standard_normal(len(t))
    body = dsp.bandpass(r.standard_normal(len(t)), lo, hi) * 0.4 + dsp.bandpass(grains, lo, hi) * 3.0
    return body * np.sin(np.pi * t / dur) ** 1.5


def snap(r) -> np.ndarray:
    t = _t(0.05)
    return dsp.bandpass(r.standard_normal(len(t)), 1200, 6000) * _exp(t, 0.006)


def slice_(r, dur=0.3) -> np.ndarray:
    """Couteau qui tranche : bruit aigu glissant, attaque douce."""
    t = _t(dur)
    n = r.standard_normal(len(t))
    hi = dsp.bandpass(n, 3000, 9000)
    lo = dsp.bandpass(n, 400, 1500)
    u = t / dur
    return (hi * u + lo * (1 - u)) * np.sin(np.pi * u) ** 2


def hammer_anvil(r, strength=1.0) -> np.ndarray:
    a = perc.anvil(int(r.integers(1 << 30)), f0=r.uniform(780, 860))[: dsp.samples(1.0)]
    t = _t(1.0)
    return a * _exp(t, 0.25 if strength > 0.7 else 0.12) * strength


def crackle(r, dur, rate=40.0) -> np.ndarray:
    out = np.zeros(dsp.samples(dur))
    t = 0.0
    while True:
        t += r.exponential(1.0 / rate)
        if t >= dur - 0.01:
            break
        g = dsp.samples(r.uniform(0.001, 0.004))
        _at(out, r.standard_normal(g) * _exp(_t(g / dsp.SR), 0.001), t, r.uniform(0.05, 0.35))
    return dsp.bandpass(out, 800, 9000)


def bubble(r) -> np.ndarray:
    d = r.uniform(0.02, 0.06)
    t = _t(d)
    f = r.uniform(300, 900) * (1 + 1.5 * t / d)
    return np.sin(2 * np.pi * np.cumsum(f) / dsp.SR) * np.sin(np.pi * t / d)


def glass_clink(r) -> np.ndarray:
    return perc._metal([(r.uniform(3200, 4200), 1.0), (r.uniform(5600, 6800), 0.5)], [0.15, 0.08], 0.5,
                       int(r.integers(1 << 30)), click=0.3)


# ----------------------------------------------------------------- sons

def _loop(sid: str) -> np.ndarray:
    r = dsp.rng(sum(map(ord, sid)) * 17 + 5)
    L = LOOPS[sid]
    x = np.zeros(dsp.samples(L + 1.5))
    if sid == "harvest_mining":
        for k, tt in enumerate((0.0, 1.0)):
            _at(x, rock_strike(r), tt, 1.0 - 0.15 * k)
            _at(x, pebbles(r), tt + 0.04, 0.5)
    elif sid == "harvest_wood":
        for tt in (0.0, 1.2):
            _at(x, axe_chop(r), tt, 1.0)
            _at(x, snap(r), tt + r.uniform(0.03, 0.08), 0.3)
    elif sid == "harvest_herb":
        for tt in (0.1, 0.8, 1.6, 2.3):
            _at(x, rustle(r, r.uniform(0.3, 0.5)), tt, r.uniform(0.6, 1.0))
        for tt in (0.55, 2.0):
            _at(x, snap(r), tt, 0.7)
    elif sid == "harvest_skinning":
        for tt in (0.0, 0.7, 1.35):
            _at(x, slice_(r, r.uniform(0.25, 0.4)), tt, r.uniform(0.6, 1.0))
            t = _t(0.15)
            _at(x, dsp.bandpass(r.standard_normal(len(t)), 200, 900) * _exp(t, 0.05), tt + 0.1, 0.3)
    elif sid == "craft_forge":
        for tt, g in ((0.0, 1.0), (0.5, 0.55), (1.0, 1.0), (1.5, 0.55)):
            _at(x, hammer_anvil(r, g), tt, g)
        _at(x, crackle(r, L + 1.0, 25.0), 0.0, 0.5)
        tb = _t(L + 1.0)
        bed = dsp.lowpass(r.standard_normal(len(tb)), 500) * (0.5 + 0.5 * np.sin(2 * np.pi * tb / L) ** 2)
        _at(x, bed, 0.0, 0.08)  # braises / soufflet
    elif sid == "craft_alchemy":
        tt = 0.0
        while tt < L:
            tt += r.exponential(1 / 14.0)
            _at(x, bubble(r), tt, r.uniform(0.15, 0.6))
        tb = _t(L + 1.0)
        _at(x, dsp.bandpass(r.standard_normal(len(tb)), 300, 1500), 0.0, 0.06)  # frémissement
        for tt in (0.9, 2.9):
            _at(x, glass_clink(r), tt, 0.35)
    elif sid == "craft_leather":
        for tt in (0.0, 1.5):
            t = _t(0.6)
            stretch = dsp.bandpass(r.standard_normal(len(t)), 250, 1800) * np.sin(np.pi * t / 0.6) ** 2
            creak = np.sin(2 * np.pi * np.cumsum(140 + 40 * np.sin(2 * np.pi * 3 * t)) / dsp.SR) * np.sin(np.pi * t / 0.6) * 0.3
            _at(x, stretch + creak, tt, 0.8)
        for tt in (0.75, 1.0, 2.25, 2.5):
            t = _t(0.06)
            _at(x, dsp.bandpass(r.standard_normal(len(t)), 1500, 6000) * _exp(t, 0.008), tt, 0.6)
    elif sid == "craft_generic":
        for tt in (0.0, 0.45, 1.0, 1.45):
            t = _t(0.12)
            f0 = r.uniform(170, 240)
            knock = sum(g * np.sin(2 * np.pi * f0 * k * t) * _exp(t, d)
                        for k, g, d in ((1.0, 1.0, 0.03), (2.3, 0.5, 0.02), (4.1, 0.25, 0.012)))
            _at(x, knock, tt, r.uniform(0.6, 1.0))
    else:
        raise ValueError(sid)
    y = dsp.wrap_loop(room(x), dsp.samples(L)).mean(axis=1)
    return _n(y) * dsp.from_db(-6.0)


def make(sid: str) -> np.ndarray:
    if sid in LOOPS:
        return _loop(sid)
    r = dsp.rng(sum(map(ord, sid)) * 17 + 5)
    if sid == "harvest_complete":
        x = np.zeros(dsp.samples(0.8))
        _at(x, snap(r), 0.0, 0.6)
        _at(x, rustle(r, 0.25, 600, 4000), 0.03, 0.5)
        t = _t(0.4)
        _at(x, np.sin(2 * np.pi * 880 * t) * _exp(t, 0.08) + 0.4 * np.sin(2 * np.pi * 1320 * t) * _exp(t, 0.06), 0.08, 0.35)
        y = room(x, 0.2, 0.8)
    elif sid in ("craft_complete", "craft_complete_rare"):
        x = np.zeros(dsp.samples(2.0))
        _at(x, hammer_anvil(r, 1.0), 0.0, 0.8)
        for k, f in enumerate((587.33, 880.0)):
            t = _t(1.0)
            _at(x, np.sin(2 * np.pi * f * t) * _exp(t, 0.3), 0.12 + 0.08 * k, 0.35)
        if sid.endswith("rare"):
            for k, f in enumerate((1174.66, 1479.98, 1760.0, 2349.32)):
                t = _t(1.0)
                _at(x, (np.sin(2 * np.pi * f * t) + 0.3 * np.sin(4 * np.pi * f * t)) * _exp(t, 0.35), 0.3 + 0.07 * k, 0.3)
        y = room(x, 0.3, 1.3)
    else:
        raise ValueError(sid)
    loud = np.nonzero(np.abs(y) > np.max(np.abs(y)) * dsp.from_db(-50.0))[0]
    y = y[: loud[-1] + dsp.samples(0.04)] if len(loud) else y
    y[: dsp.samples(0.001)] *= np.linspace(0, 1, dsp.samples(0.001))
    y[-dsp.samples(0.02):] *= np.linspace(1, 0, dsp.samples(0.02))
    return _n(y) * dsp.from_db(-4.0)


SOUNDS = list(LOOPS) + ["harvest_complete", "craft_complete", "craft_complete_rare"]


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--out", default="game/data/audio/sfx/interactions")
    a = ap.parse_args()
    out = Path(a.out)
    for sid in SOUNDS:
        dsp.write_wav16(make(sid).astype(np.float32), out / f"{sid}.wav")
    print(f"écrit : {len(SOUNDS)} sons de récolte et d'artisanat dans {out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
