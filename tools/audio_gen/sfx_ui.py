"""SND.3 — Sons d'interface : bois, parchemin, cuir, métal, et courtes fanfares.

Esprit médiéval sombre, pas électronique (retour d'écoute SND.1) :
  - bruitages synthétisés de matières (clic de bois, parchemin qu'on déroule,
    cuir, pièces, armure) ;
  - « fanfares » courtes jouées par des instruments ENREGISTRÉS (banque
    FluidR3_GM via FluidSynth) : harpe, cloches, tuba, timbales, taiko — les
    mêmes familles que la musique du menu.

Sortie : game/data/audio/sfx/ui/<id>.wav (mono, 48 kHz, 16 bits).
Usage :  python3 sfx_ui.py [--out game/data/audio/sfx/ui]
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))

from lcdlln_audio import dsp, percussion as perc  # noqa: E402
from lcdlln_audio.orchestra import GM, Part, note, render  # noqa: E402

HARP, BELLS, GLOCK, CELESTA = GM["harp"], 14, 9, 8


def _t(dur: float) -> np.ndarray:
    return np.arange(dsp.samples(dur)) / dsp.SR


def _env(t, attack, decay):
    return np.clip(t / max(attack, 1e-4), 0, 1) * np.exp(-np.maximum(t - attack, 0) / decay)


def mono(x: np.ndarray) -> np.ndarray:
    return x.mean(axis=1) if x.ndim == 2 else x


def norm(x: np.ndarray, peak_db: float) -> np.ndarray:
    """Coupe la queue sous -50 dB, fondus d'entrée/sortie, crête à `peak_db`."""
    x = dsp.highpass(np.asarray(x, dtype=np.float64), 35.0, 2)
    loud = np.nonzero(np.abs(x) > np.max(np.abs(x)) * dsp.from_db(-50.0))[0]
    if len(loud):
        x = x[: min(len(x), loud[-1] + dsp.samples(0.05))]
    fin, fout = dsp.samples(0.002), dsp.samples(0.03)
    x[:fin] *= np.linspace(0, 1, fin)
    x[-fout:] *= np.linspace(1, 0, fout)
    return x / (np.max(np.abs(x)) + 1e-9) * dsp.from_db(peak_db)


def instrument(program: int, notes, seconds: float, bpm: float = 120.0, bank: int = 0) -> np.ndarray:
    """Rend une courte phrase (liste (début noires, durée, note, vélocité)) en mono."""
    p = Part("ui", program, bank=bank, volume=118, humanize_ms=0.0)
    for start, dur, n, v in notes:
        p.add(start, dur, n, v)
    return mono(render(p, bpm, seconds))


def room(x: np.ndarray, wet: float = 0.18, t60: float = 0.9) -> np.ndarray:
    ir = dsp.make_ir(t60, dsp.rng(5), predelay=0.008, damping_hz=6000.0)
    return mono(dsp.reverb(x, ir, wet=wet, dry=1.0))


# ----------------------------------------------------------------- matières

def wood_knock(r, f0=180.0, dur=0.12) -> np.ndarray:
    t = _t(dur)
    body = sum(g * np.sin(2 * np.pi * f0 * k * (1 + r.uniform(-0.02, 0.02)) * t) * np.exp(-t / d)
               for k, g, d in ((1.0, 1.0, 0.03), (2.3, 0.5, 0.02), (4.1, 0.25, 0.012)))
    click = dsp.bandpass(r.standard_normal(len(t)), 1500, 7000) * _env(t, 0.0005, 0.004)
    return body + 0.5 * click / (np.max(np.abs(click)) + 1e-9)


def parchment(r, dur=0.35, lo=1200.0, hi=8000.0, crackle=60) -> np.ndarray:
    """Parchemin qu'on manipule : souffle sec + craquements."""
    t = _t(dur)
    n = dsp.bandpass(r.standard_normal(len(t)), lo, hi) * (0.3 + 0.7 * np.abs(np.sin(np.pi * t / dur)) ** 0.7)
    out = n / (np.max(np.abs(n)) + 1e-9) * 0.6
    for _ in range(crackle):
        s = int(r.uniform(0, 0.95) * len(t))
        g = dsp.samples(r.uniform(0.001, 0.004))
        out[s:s + g] += r.standard_normal(min(g, len(t) - s)) * r.uniform(0.2, 0.8)
    return dsp.bandpass(out, lo * 0.8, hi)


def leather(r, dur=0.18) -> np.ndarray:
    t = _t(dur)
    return dsp.bandpass(r.standard_normal(len(t)), 300, 2500) * _env(t, 0.02, 0.05)


def coin(r, f0=None) -> np.ndarray:
    """Une pièce qui tinte : partiels inharmoniques aigus."""
    f0 = f0 or r.uniform(2600, 3600)
    t = _t(0.35)
    x = sum(g * np.sin(2 * np.pi * f0 * k * t + r.uniform(0, 6)) * np.exp(-t / d)
            for k, g, d in ((1.0, 1.0, 0.12), (1.47, 0.6, 0.08), (2.09, 0.4, 0.05), (2.76, 0.25, 0.04)))
    return x * (1 - np.exp(-t / 0.0008))


def coins(r, count=7, spread=0.32) -> np.ndarray:
    out = np.zeros(dsp.samples(spread + 0.4))
    for i in range(count):
        c = coin(r) * r.uniform(0.3, 1.0)
        s = dsp.samples(abs(r.normal(0.05, spread / 3)) % spread)
        out[s:s + len(c)] += c[: len(out) - s]
    return out


def metal_clank(r, dur=0.4) -> np.ndarray:
    t = _t(dur)
    x = sum(g * np.sin(2 * np.pi * f * t) * np.exp(-t / d) for f, g, d in
            ((r.uniform(500, 700), 1.0, 0.08), (r.uniform(1300, 1600), 0.7, 0.05), (r.uniform(2400, 2900), 0.4, 0.03)))
    hit = dsp.bandpass(r.standard_normal(len(t)), 800, 6000) * _env(t, 0.0005, 0.01)
    return x + 0.6 * hit / (np.max(np.abs(hit)) + 1e-9)


def place(dst: np.ndarray, src: np.ndarray, t: float, g: float = 1.0) -> np.ndarray:
    i = dsp.samples(t)
    if i + len(src) > len(dst):
        dst = np.concatenate([dst, np.zeros(i + len(src) - len(dst))])
    dst[i:i + len(src)] += g * src
    return dst


# ----------------------------------------------------------------- sons

def make(sid: str) -> np.ndarray:
    r = dsp.rng(sum(map(ord, sid)))  # graine stable par son
    if sid == "ui_click":
        return norm(wood_knock(r, 210.0, 0.09), -10)
    if sid == "ui_open":
        x = place(parchment(r, 0.28) * 0.6, wood_knock(r, 150.0, 0.12), 0.2, 0.8)
        return norm(room(x, 0.12), -9)
    if sid == "ui_close":
        x = place(leather(r, 0.12), wood_knock(r, 130.0, 0.12), 0.06, 1.0)
        return norm(room(x, 0.12), -10)
    if sid == "ui_error":
        x = place(wood_knock(r, 110.0, 0.12), wood_knock(r, 95.0, 0.14), 0.11, 0.9)
        x = place(x, instrument(GM["tuba"], [(0, 0.35, note("C#2"), 90)], 0.6) * 1.5, 0.0, 0.8)
        return norm(room(x, 0.1), -9)
    if sid == "ui_notify":
        return norm(room(instrument(CELESTA, [(0, 1, note("A5"), 90), (0.25, 1, note("D6"), 80)], 1.4)), -11)
    if sid == "chat_message":
        return norm(instrument(HARP, [(0, 1, note("A5"), 70)], 0.9), -16)
    if sid == "chat_whisper":
        return norm(instrument(HARP, [(0, 1, note("D6"), 60), (0.3, 1, note("A5"), 55)], 1.2), -15)
    if sid == "quest_accept":
        x = instrument(HARP, [(k * 0.25, 2, note(n), 90) for k, n in enumerate(("D4", "F4", "A4", "D5"))], 2.2)
        x = place(x, mono(render(_one("taiko", "A1", 100), 120, 1.5)) * 0.8, 0.0, 1.0)
        return norm(room(x, 0.2, 1.4), -8)
    if sid == "quest_objective":
        return norm(room(instrument(GLOCK, [(0, 1, note("A5"), 80), (0.4, 1.5, note("D6"), 85)], 1.6)), -12)
    if sid == "quest_complete":
        x = instrument(BELLS, [(0.5, 3, note("D4"), 100), (0.5, 3, note("A4"), 90)], 3.0)
        x = place(x, instrument(GM["tuba"], [(0, 2, note("D2"), 110)], 2.4), 0.0, 1.0)
        x = place(x, mono(render(_one("taiko", "A1", 120), 120, 2.0)), 0.0, 1.2)
        x = place(x, instrument(HARP, [(k * 0.17, 2, note(n), 95) for k, n in enumerate(("D4", "F4", "A4", "D5", "F5"))], 2.4), 0.25, 0.7)
        return norm(room(x, 0.25, 1.8), -6)
    if sid == "level_up":
        roll = Part("roll", GM["timpani"], volume=120, humanize_ms=0.0)
        for k in range(12):
            roll.add(k * 0.125, 0.15, note("A1"), 60 + k * 5)
        roll.add(1.5, 2, note("D2"), 127)
        x = mono(render(roll, 120, 4.0))
        x = place(x, instrument(GM["brass"], [(1.5, 3, n, 110) for n in (note("D3"), note("F#3"), note("A3"), note("D4"))], 4.0), 0.0, 1.2)
        x = place(x, instrument(GM["tuba"], [(1.5, 3, note("D2"), 115)], 4.0), 0.0, 1.0)
        x = place(x, instrument(BELLS, [(1.5, 3, note("D5"), 100), (2.0, 3, note("A5"), 90)], 4.0), 0.0, 0.6)
        x = place(x, perc.gong(7, f0=70.0, dur=4.0) * 0.5, 0.75, 0.6)
        return norm(room(x, 0.25, 2.0), -5)
    if sid == "coins":
        return norm(room(coins(r), 0.08), -10)
    if sid == "item_pickup":
        x = place(leather(r, 0.16), coin(r, 2200.0) * 0.3, 0.05, 1.0)
        return norm(x, -11)
    if sid == "item_equip":
        x = place(leather(r, 0.2), metal_clank(r), 0.08, 0.8)
        return norm(room(x, 0.1), -9)
    if sid == "loot_open":
        x = place(wood_knock(r, 120.0, 0.18), parchment(r, 0.25, 500, 3000, 20) * 0.6, 0.05, 1.0)
        x = place(x, coins(r, 4, 0.2) * 0.5, 0.2, 1.0)
        return norm(room(x, 0.12), -9)
    if sid == "map_open":
        return norm(room(parchment(r, 0.55, 900, 7500, 120), 0.1), -10)
    if sid == "map_close":
        x = place(parchment(r, 0.3, 900, 6000, 50), leather(r, 0.1), 0.25, 0.8)
        return norm(room(x, 0.1), -11)
    if sid == "place_discovered":
        x = mono(render(_one("taiko", "D1", 120), 60, 4.0))
        x = place(x, instrument(GM["tuba"], [(0, 2.5, note("D2"), 110)], 4.0, 60), 0.0, 1.0)
        x = place(x, perc.gong(3, f0=62.0, dur=4.5) * 0.6, 0.0, 0.8)
        return norm(room(x, 0.3, 2.4), -6)
    raise ValueError(sid)


def _one(kind: str, pitch: str, vel: int) -> Part:
    p = Part(kind, GM["taiko"], volume=127, humanize_ms=0.0)
    p.add(0, 2, pitch, vel)
    return p


SOUNDS = ["ui_click", "ui_open", "ui_close", "ui_error", "ui_notify", "chat_message", "chat_whisper",
          "quest_accept", "quest_objective", "quest_complete", "level_up", "coins", "item_pickup",
          "item_equip", "loot_open", "map_open", "map_close", "place_discovered"]


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--out", default="game/data/audio/sfx/ui")
    a = ap.parse_args()
    out = Path(a.out)
    for sid in SOUNDS:
        dsp.write_wav16(make(sid).astype(np.float32), out / f"{sid}.wav")
    print(f"écrit : {len(SOUNDS)} sons d'interface dans {out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
