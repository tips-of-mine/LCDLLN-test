"""SND.5 — Sons de combat et de sorts (synthétisés, mono pour la spatialisation 3D).

  combat_hit_blade_1..3   lame qui touche (tintement + chair)          — mêlée, tank, voleur
  combat_hit_blunt_1..3   coup sourd (masse, poing, bouclier)           — créatures, défaut
  combat_hit_arrow_1..3   flèche qui se plante (sifflement + impact)    — distance, pisteur
  combat_hit_magic_1..3   décharge magique (zap + étincelles)           — lanceur, soigneur, sacré
  combat_hit_claw_1..3    griffes / crocs (déchirures)                  — attaques de créatures
  combat_crit             impact lourd superposé à un coup critique
  combat_whoosh_1..3      coup raté (fendre l'air)
  combat_player_hurt_1..3 le joueur encaisse (choc mat, cuir, souffle) — pas de voix
  combat_death            chute d'un corps
  player_death            mort du joueur (gong grave, impact)
  spell_cast              incantation qui se charge (1,4 s)
  spell_release           sort lâché
  spell_fizzle            sort interrompu
  aura_buff               bienfait reçu (scintillement montant)
  aura_debuff             malédiction reçue (descente sombre)
  combat_heartbeat        battement de cœur (boucle 1 s, santé basse)

Sortie : game/data/audio/sfx/combat/<id>.wav (16 bits, 48 kHz, mono).
Usage :  python3 sfx_combat.py [--out game/data/audio/sfx/combat]
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))

from lcdlln_audio import dsp, percussion as perc  # noqa: E402


def _t(dur: float) -> np.ndarray:
    return np.arange(dsp.samples(dur)) / dsp.SR


def _exp(t, tau):
    return np.exp(-t / tau)


def _sweep(f_start, f_end, dur, curve=1.0):
    """Phase d'un sinus glissant de f_start à f_end (Hz) en `dur` s."""
    t = _t(dur)
    f = f_start + (f_end - f_start) * (t / dur) ** curve
    return np.sin(2 * np.pi * np.cumsum(f) / dsp.SR)


def _at(dst: np.ndarray, src: np.ndarray, t: float, g: float = 1.0) -> np.ndarray:
    i = dsp.samples(t)
    j = min(len(dst), i + len(src))
    dst[i:j] += src[: j - i] * g
    return dst


def norm(x: np.ndarray, peak_db: float) -> np.ndarray:
    """Coupe la queue sous −50 dB, fondus courts, crête à `peak_db` dBFS."""
    x = dsp.highpass(np.asarray(x, dtype=np.float64), 40.0, 2)
    loud = np.nonzero(np.abs(x) > np.max(np.abs(x)) * dsp.from_db(-50.0))[0]
    if len(loud):
        x = x[: min(len(x), loud[-1] + dsp.samples(0.04))]
    fin, fout = dsp.samples(0.001), min(len(x) // 4, dsp.samples(0.025))
    x[:fin] *= np.linspace(0, 1, fin)
    x[-fout:] *= np.linspace(1, 0, fout)
    return x / (np.max(np.abs(x)) + 1e-9) * dsp.from_db(peak_db)


def room(x: np.ndarray, wet=0.12, t60=0.6) -> np.ndarray:
    ir = dsp.make_ir(t60, dsp.rng(9), predelay=0.006, damping_hz=5000.0)
    y = dsp.reverb(x, ir, wet=wet, dry=1.0)
    return y.mean(axis=1) if y.ndim == 2 else y


# ----------------------------------------------------------------- briques

def flesh(r, dur=0.18, lo=90.0, hi=900.0, tau=0.035) -> np.ndarray:
    """Impact dans la chair / le cuir : bruit grave très amorti + petit corps."""
    t = _t(dur)
    n = dsp.bandpass(r.standard_normal(len(t)), lo, hi) * _exp(t, tau)
    body = np.sin(2 * np.pi * np.cumsum(r.uniform(70, 110) * (1 + 0.8 * _exp(t, 0.02))) / dsp.SR) * _exp(t, 0.06)
    return n / (np.max(np.abs(n)) + 1e-9) + 0.6 * body


def whoosh(r, dur=0.28, f_lo=350.0, f_hi=2600.0) -> np.ndarray:
    """Air fendu : bruit passe-bande dont le centre monte puis redescend."""
    t = _t(dur)
    n = r.standard_normal(len(t))
    env = np.sin(np.pi * t / dur) ** 2
    # Filtre « glissant » approché : fondu entre une bande grave et une bande
    # aiguë, l'aiguë dominant au milieu du geste (lame au plus vite).
    low = dsp.bandpass(n, f_lo * 0.7, f_lo * 1.6)
    high = dsp.bandpass(n, f_hi * 0.6, f_hi * 1.3)
    mid = np.sin(np.pi * t / dur)
    out = low * (1 - mid) + high * mid
    return out * env


def ring(r, f0, dur=0.5, tau=0.12) -> np.ndarray:
    """Tintement métallique bref (lame)."""
    partials = [(f0, 1.0), (f0 * 2.31, 0.5), (f0 * 3.87, 0.3), (f0 * 5.9, 0.15)]
    return perc._metal(partials, [tau, tau * 0.7, tau * 0.5, tau * 0.3], dur, int(r.integers(1 << 30)), click=0.5)


def sparkle(r, dur=0.6, count=14, lo=2500.0, hi=7000.0) -> np.ndarray:
    out = np.zeros(dsp.samples(dur))
    for _ in range(count):
        d = r.uniform(0.04, 0.12)
        t = _t(d)
        s = np.sin(2 * np.pi * r.uniform(lo, hi) * t) * _exp(t, d / 3)
        _at(out, s, r.uniform(0, dur - d), r.uniform(0.2, 1.0))
    return out


# ----------------------------------------------------------------- sons

def make(sid: str) -> np.ndarray:
    r = dsp.rng(sum(map(ord, sid)) * 13 + 1)
    v = int(sid[-1]) if sid[-1].isdigit() else 0
    if sid.startswith("combat_hit_blade"):
        x = np.zeros(dsp.samples(0.7))
        _at(x, whoosh(r, 0.12, 900, 3500), 0.0, 0.25)
        _at(x, flesh(r, 0.25, 150, 1500, 0.03), 0.1, 0.9)
        _at(x, ring(r, r.uniform(1400, 2200), 0.5, 0.09), 0.1, 0.35)
        return norm(room(x), -3.0)
    if sid.startswith("combat_hit_blunt"):
        x = np.zeros(dsp.samples(0.5))
        _at(x, perc._drum(r.uniform(65, 95), 0.08, 0.4, int(r.integers(1 << 30)), bend=1.5, slap=0.5,
                          slap_band=(200, 2500)), 0.0, 1.0)
        _at(x, flesh(r, 0.2, 80, 600, 0.04), 0.0, 0.8)
        return norm(room(x), -3.0)
    if sid.startswith("combat_hit_arrow"):
        x = np.zeros(dsp.samples(0.6))
        _at(x, whoosh(r, 0.16, 1800, 6000), 0.0, 0.35)
        t = _t(0.25)
        thwack = dsp.bandpass(r.standard_normal(len(t)), 400, 3500) * _exp(t, 0.012)
        _at(x, thwack / (np.max(np.abs(thwack)) + 1e-9), 0.15, 1.0)
        _at(x, flesh(r, 0.2, 100, 700, 0.03), 0.15, 0.7)
        # Vibration de la hampe.
        tv = _t(0.3)
        _at(x, np.sin(2 * np.pi * r.uniform(180, 260) * tv) * _exp(tv, 0.06) * (1 + np.sin(2 * np.pi * 35 * tv)) * 0.5,
            0.16, 0.25)
        return norm(room(x), -3.0)
    if sid.startswith("combat_hit_magic"):
        x = np.zeros(dsp.samples(0.9))
        t = _t(0.35)
        zap = _sweep(r.uniform(1400, 2000), r.uniform(120, 200), 0.35, 0.5) * _exp(t, 0.1)
        zap += 0.4 * dsp.bandpass(r.standard_normal(len(t)), 800, 6000) * _exp(t, 0.05)
        _at(x, zap, 0.0, 1.0)
        _at(x, sparkle(r, 0.6, 12), 0.05, 0.25)
        _at(x, flesh(r, 0.2, 80, 500, 0.04), 0.02, 0.5)
        return norm(room(x, 0.2, 0.9), -3.0)
    if sid.startswith("combat_hit_claw"):
        x = np.zeros(dsp.samples(0.5))
        for k in range(3):
            t = _t(0.07)
            rip = dsp.bandpass(r.standard_normal(len(t)), 1200, 6000) * np.sin(np.pi * t / 0.07) ** 0.5 * _exp(t, 0.03)
            _at(x, rip, k * r.uniform(0.025, 0.04), 0.7)
        _at(x, flesh(r, 0.2, 90, 800, 0.03), 0.06, 0.9)
        return norm(room(x), -3.0)
    if sid == "combat_crit":
        x = np.zeros(dsp.samples(1.2))
        _at(x, perc.cinematic_boom(7, 46.0)[: dsp.samples(1.2)], 0.0, 1.0)
        _at(x, ring(r, 950, 0.8, 0.2), 0.0, 0.3)
        t = _t(0.1)
        _at(x, dsp.bandpass(r.standard_normal(len(t)), 500, 5000) * _exp(t, 0.015), 0.0, 0.6)
        return norm(room(x, 0.2, 1.0), -2.0)
    if sid.startswith("combat_whoosh"):
        return norm(whoosh(r, r.uniform(0.25, 0.35), r.uniform(250, 450), r.uniform(1800, 3200)), -6.0)
    if sid.startswith("combat_player_hurt"):
        x = np.zeros(dsp.samples(0.6))
        _at(x, flesh(r, 0.3, 60, 450, 0.05), 0.0, 1.0)
        # Cuir de l'armure froissé.
        t = _t(0.18)
        _at(x, dsp.bandpass(r.standard_normal(len(t)), 1500, 5000) * np.sin(np.pi * t / 0.18) * 0.25, 0.02, 1.0)
        # Souffle coupé : bruit grave filtré (pas de voix).
        t = _t(0.3)
        _at(x, dsp.bandpass(r.standard_normal(len(t)), 300, 1200) * np.sin(np.pi * t / 0.3) ** 2 * 0.2, 0.05, 1.0)
        return norm(dsp.lowpass(room(x), 6000), -4.0)
    if sid == "combat_death":
        x = np.zeros(dsp.samples(1.4))
        _at(x, perc._drum(70, 0.12, 0.5, 31, bend=0.8, slap=0.5, slap_band=(150, 1500)), 0.0, 0.8)
        _at(x, perc._drum(55, 0.16, 0.6, 32, bend=0.6, slap=0.6, slap_band=(150, 1500)), 0.22, 1.0)
        t = _t(0.5)
        _at(x, dsp.bandpass(r.standard_normal(len(t)), 600, 4000) * _exp(t, 0.12) * 0.25, 0.24, 1.0)  # terre, gravier
        _at(x, perc.chains(33, 0.4, 10)[: dsp.samples(0.45)] * 0.12, 0.2, 1.0)                     # équipement
        return norm(room(x, 0.15), -3.0)
    if sid == "player_death":
        x = np.zeros(dsp.samples(4.5))
        _at(x, perc.gong(41, 52.0, 4.5), 0.0, 0.8)
        _at(x, perc.cinematic_boom(42, 34.0)[: dsp.samples(4.0)], 0.0, 0.9)
        return norm(room(x, 0.3, 2.2), -3.0)
    if sid == "spell_cast":
        d = 1.4
        t = _t(d)
        rise = np.clip(t / d, 0, 1) ** 1.5
        n = r.standard_normal(len(t))
        air = dsp.bandpass(n, 600, 5000) * rise * 0.4
        tone = sum(np.sin(2 * np.pi * f * (1 + 0.6 * t / d) * t) for f in (220.0, 330.0, 440.0 * 1.003)) / 3
        trem = 0.6 + 0.4 * np.sin(2 * np.pi * (4 + 10 * t / d) * t)
        x = air + tone * trem * rise * 0.6
        x[-dsp.samples(0.05):] *= np.linspace(1, 0, dsp.samples(0.05))
        return norm(room(x, 0.25, 1.2), -8.0)
    if sid == "spell_release":
        x = np.zeros(dsp.samples(1.0))
        t = _t(0.5)
        burst = dsp.bandpass(r.standard_normal(len(t)), 300, 4000) * _exp(t, 0.08)
        _at(x, burst / (np.max(np.abs(burst)) + 1e-9), 0.0, 0.8)
        _at(x, _sweep(160, 60, 0.5) * _exp(t, 0.15), 0.0, 0.9)
        _at(x, sparkle(r, 0.6, 10), 0.02, 0.3)
        return norm(room(x, 0.25, 1.2), -4.0)
    if sid == "spell_fizzle":
        x = np.zeros(dsp.samples(0.8))
        for k in range(18):
            tt = (k / 18) ** 1.4 * 0.6
            d = 0.012
            tc = _t(d)
            _at(x, dsp.highpass(r.standard_normal(len(tc)), 2000) * _exp(tc, 0.004), tt, (1 - k / 18) * 0.8)
        t = _t(0.5)
        _at(x, _sweep(500, 140, 0.5) * _exp(t, 0.12) * 0.4, 0.0, 1.0)
        return norm(room(x), -8.0)
    if sid == "aura_buff":
        x = np.zeros(dsp.samples(1.5))
        for k, f in enumerate((523.25, 659.25, 783.99, 1046.5)):
            t = _t(1.0)
            bell = (np.sin(2 * np.pi * f * t) + 0.3 * np.sin(2 * np.pi * f * 2.0 * t)) * _exp(t, 0.35)
            _at(x, bell, k * 0.07, 0.5)
        _at(x, sparkle(r, 1.0, 16, 3000, 8000), 0.05, 0.2)
        return norm(room(x, 0.3, 1.4), -9.0)
    if sid == "aura_debuff":
        x = np.zeros(dsp.samples(1.6))
        for k, f in enumerate((311.13, 277.18, 233.08)):
            t = _t(1.0)
            tone = (np.sin(2 * np.pi * f * t) + np.sin(2 * np.pi * f * 1.012 * t)) * _exp(t, 0.4)
            _at(x, tone, k * 0.12, 0.4)
        t = _t(1.2)
        _at(x, dsp.lowpass(r.standard_normal(len(t)), 300) * np.sin(np.pi * t / 1.2) * 0.5, 0.0, 1.0)
        return norm(room(x, 0.3, 1.4), -9.0)
    if sid == "combat_heartbeat":
        x = np.zeros(dsp.samples(1.0))
        for tt, g in ((0.0, 1.0), (0.24, 0.7)):
            t = _t(0.2)
            thump = np.sin(2 * np.pi * np.cumsum(48 * (1 + 0.5 * _exp(t, 0.02))) / dsp.SR) * _exp(t, 0.05)
            _at(x, thump, tt, g)
        x[: dsp.samples(0.001)] *= np.linspace(0, 1, dsp.samples(0.001))
        return x / (np.max(np.abs(x)) + 1e-9) * dsp.from_db(-6.0)  # boucle : longueur exacte, pas de coupe
    raise ValueError(sid)


def _variants(base: str, n: int = 3) -> list[str]:
    return [f"{base}_{k}" for k in range(1, n + 1)]


SOUNDS = (_variants("combat_hit_blade") + _variants("combat_hit_blunt") + _variants("combat_hit_arrow")
          + _variants("combat_hit_magic") + _variants("combat_hit_claw") + ["combat_crit"]
          + _variants("combat_whoosh") + _variants("combat_player_hurt")
          + ["combat_death", "player_death", "spell_cast", "spell_release", "spell_fizzle", "aura_buff",
             "aura_debuff", "combat_heartbeat"])


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--out", default="game/data/audio/sfx/combat")
    a = ap.parse_args()
    out = Path(a.out)
    for sid in SOUNDS:
        dsp.write_wav16(make(sid).astype(np.float32), out / f"{sid}.wav")
    print(f"écrit : {len(SOUNDS)} sons de combat dans {out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
