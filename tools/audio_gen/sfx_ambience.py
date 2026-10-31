"""SND.4 — Boucles d'ambiance synthétisées (lieux, heure, météo).

13 boucles stéréo de 45 s (sans couture : ce qui dépasse la fin est replié sur
le début) + 3 coups de tonnerre :

  amb_forest_day    feuillage au vent, chants d'oiseaux, pic-vert lointain
  amb_forest_night  souffle léger, grillons épars, chouette
  amb_plains_day    vent dans l'herbe, alouettes lointaines
  amb_plains_night  vent léger, grillons
  amb_swamp         grenouilles, insectes, bulles
  amb_mountain      vent fort qui siffle
  amb_snow          vent glacé, sifflements aigus
  amb_desert        vent sec, sable qui crépite
  amb_coast         vagues (ressac) et vent du large
  amb_river         eau qui court, bouillonnements
  amb_town          rumeur des rues, forge lointaine
  amb_rain_light    pluie fine
  amb_rain_heavy    pluie battante
  amb_thunder_1..3  tonnerre lointain (sons ponctuels)

Les niveaux sont volontairement bas (−24 à −20 LUFS) : une ambiance s'entend
sous le jeu, elle ne le couvre pas. Rien d'électronique : bruits filtrés et
modèles simples d'animaux, fortement atténués et réverbérés.

Sortie : game/data/audio/ambience/<id>.ogg
Usage :  python3 sfx_ambience.py [--out game/data/audio/ambience] [--only amb_river]
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))

from lcdlln_audio import dsp, percussion as perc, textures  # noqa: E402

LOOP = 45.0
TAIL = 6.0


def _t(dur: float) -> np.ndarray:
    return np.arange(dsp.samples(dur)) / dsp.SR


def buf() -> np.ndarray:
    return np.zeros((dsp.samples(LOOP + TAIL), 2), dtype=np.float32)


def scatter_events(r, rate_per_s: float, start: float = 0.0, end: float = LOOP):
    """Instants d'évènements aléatoires (processus de Poisson)."""
    t, out = start, []
    while True:
        t += r.exponential(1.0 / rate_per_s)
        if t >= end:
            return out
        out.append(t)


# ----------------------------------------------------------------- animaux

def bird_whistle(r) -> np.ndarray:
    """Chant sifflé : 3–6 notes glissées avec vibrato."""
    notes = int(r.integers(3, 7))
    base = r.uniform(2300, 3800)
    parts = []
    for _ in range(notes):
        d = r.uniform(0.06, 0.18)
        t = _t(d)
        f0 = base * r.uniform(0.8, 1.3)
        f = f0 * (1 + r.uniform(-0.25, 0.25) * t / d) * (1 + 0.02 * np.sin(2 * np.pi * r.uniform(25, 45) * t))
        s = np.sin(2 * np.pi * np.cumsum(f) / dsp.SR) * np.sin(np.pi * t / d) ** 0.7
        parts += [s, np.zeros(dsp.samples(r.uniform(0.02, 0.08)))]
    return np.concatenate(parts)


def bird_trill(r) -> np.ndarray:
    """Trille rapide (alouette, mésange) : chirps répétés."""
    n = int(r.integers(6, 16))
    f0 = r.uniform(4200, 6200)
    out = []
    for k in range(n):
        d = r.uniform(0.025, 0.045)
        t = _t(d)
        f = f0 * (1 + 0.3 * (t / d)) * (1 + 0.05 * np.sin(k))
        out += [np.sin(2 * np.pi * np.cumsum(f) / dsp.SR) * np.sin(np.pi * t / d), np.zeros(dsp.samples(0.02))]
    return np.concatenate(out)


def woodpecker(r) -> np.ndarray:
    n = int(r.integers(10, 18))
    hit = perc.woodblock(int(r.integers(1 << 20)), f=r.uniform(700, 900))[: dsp.samples(0.05)]
    out = np.zeros(dsp.samples(n * 0.055 + 0.1))
    for k in range(n):
        i = dsp.samples(k * 0.055)
        out[i:i + len(hit)] += hit * (1 - 0.4 * k / n)
    return out


def cricket_bed(r, dur, crickets=6, rate=1.4) -> np.ndarray:
    """Grillons : salves de 3–5 impulsions sur une porteuse de ~4,5 kHz."""
    out = np.zeros((dsp.samples(dur), 2))
    for c in range(crickets):
        fc = r.uniform(4000, 5200)
        p = r.uniform(-0.9, 0.9)
        g = r.uniform(0.25, 1.0)
        period = 1.0 / (rate * r.uniform(0.7, 1.4))
        t0 = r.uniform(0, period)
        while t0 < dur - 0.3:
            pulses = int(r.integers(3, 6))
            for k in range(pulses):
                d = 0.012
                t = _t(d)
                s = np.sin(2 * np.pi * fc * t) * np.sin(np.pi * t / d)
                dsp.place(out, s * g, t0 + k * 0.022, 1.0, p)
            t0 += period * r.uniform(0.9, 1.1)
    return out


def owl(r) -> np.ndarray:
    """Hululement : « hou… hou-hou », sinus grave qui glisse vers le bas."""
    parts = []
    for d, gap in ((0.45, 0.35), (0.22, 0.08), (0.5, 0.0)):
        t = _t(d)
        f = r.uniform(360, 420) * (1 - 0.08 * t / d)
        s = np.sin(2 * np.pi * np.cumsum(f) / dsp.SR) * np.sin(np.pi * t / d) ** 1.5
        s += 0.15 * np.sin(2 * np.pi * 2 * np.cumsum(f) / dsp.SR) * np.sin(np.pi * t / d) ** 2
        parts += [s, np.zeros(dsp.samples(gap))]
    return np.concatenate(parts)


def frog(r) -> np.ndarray:
    """Coassement : train d'impulsions (~25/s) filtré par un formant grave."""
    d = r.uniform(0.25, 0.6)
    t = _t(d)
    rate = r.uniform(18, 32)
    pulses = (np.sin(2 * np.pi * rate * t) > 0.6).astype(float)
    src = pulses * r.standard_normal(len(t)) * 0.3 + pulses
    x = dsp.bandpass(src, r.uniform(250, 400), r.uniform(700, 1100))
    return x * np.sin(np.pi * t / d) ** 0.5


# ----------------------------------------------------------------- eau, foule, pluie

def stream_water(r, dur) -> np.ndarray:
    out = np.zeros((dsp.samples(dur), 2), dtype=np.float32)
    t = _t(dur)
    for ch in range(2):
        n = r.standard_normal(len(t)).astype(np.float32)
        mix = np.zeros(len(t), dtype=np.float32)
        for lo, hi, g in ((150, 500, 0.6), (500, 1800, 1.0), (1800, 6000, 0.5)):
            band = dsp.bandpass(n, lo, hi)
            mod = 0.6 + 0.4 * np.sin(2 * np.pi * r.uniform(0.3, 1.5) * t + r.uniform(0, 6))
            mix += g * band * mod.astype(np.float32)
        out[:, ch] = mix
    # Bouillonnements : petites bulles.
    for tt in scatter_events(r, 25.0, 0.0, dur - 0.1):
        d = r.uniform(0.01, 0.03)
        ts = _t(d)
        f = r.uniform(500, 1500) * (1 + 1.2 * ts / d)
        b = np.sin(2 * np.pi * np.cumsum(f) / dsp.SR) * np.sin(np.pi * ts / d)
        dsp.place(out, b * r.uniform(0.05, 0.25), tt, 1.0, r.uniform(-0.7, 0.7))
    return out


def waves(r, dur) -> np.ndarray:
    out = np.zeros((dsp.samples(dur), 2), dtype=np.float32)
    t = 0.0
    while t < dur:
        period = r.uniform(7.0, 10.0)
        d = period + 2.0
        tt = _t(d)
        env = np.clip(tt / 3.0, 0, 1) ** 2 * np.exp(-np.maximum(tt - 3.0, 0) / 1.8)
        hiss_env = np.exp(-np.maximum(tt - 3.2, 0) / 2.5) * (tt > 3.0)
        for ch in range(2):
            n = r.standard_normal(len(tt))
            body = dsp.lowpass(n, 700) * env
            hiss = dsp.bandpass(n, 1800, 7000) * hiss_env * 0.35
            seg = (body + hiss).astype(np.float32)
            i = dsp.samples(t)
            j = min(len(out), i + len(seg))
            out[i:j, ch] += seg[: j - i]
        t += period
    return out


def murmur(r, dur, voices=14) -> np.ndarray:
    """Rumeur de foule lointaine : voix figurées par du bruit filtré modulé en syllabes."""
    out = np.zeros((dsp.samples(dur), 2), dtype=np.float32)
    t = _t(dur)
    for _ in range(voices):
        n = r.standard_normal(len(t)).astype(np.float32)
        lo = r.uniform(250, 400)
        v = dsp.bandpass(n, lo, lo * r.uniform(3.0, 5.0))
        syl = np.clip(np.sin(2 * np.pi * r.uniform(3.0, 5.5) * t + r.uniform(0, 6)), 0, 1) ** 2
        talk = (np.sin(2 * np.pi * r.uniform(0.05, 0.15) * t + r.uniform(0, 6)) > -0.2).astype(np.float32)
        x = v * (syl * talk).astype(np.float32)
        dsp.place(out, x, 0.0, r.uniform(0.3, 1.0), r.uniform(-0.8, 0.8))
    return dsp.lowpass(out, 2200)


def rain(r, dur, drops_per_s, bed_db) -> np.ndarray:
    out = np.zeros((dsp.samples(dur), 2), dtype=np.float32)
    n = r.standard_normal((len(out), 2)).astype(np.float32)
    out += dsp.highpass(dsp.lowpass(n, 9000), 900) * dsp.from_db(bed_db)
    drop_len = dsp.samples(0.006)
    td = np.arange(drop_len) / dsp.SR
    for tt in scatter_events(r, drops_per_s, 0.0, dur - 0.01):
        d = r.standard_normal(drop_len) * np.exp(-td / 0.0015)
        i = dsp.samples(tt)
        p = r.uniform(-1, 1)
        g = r.uniform(0.05, 0.4)
        out[i:i + drop_len, 0] += (d * g * (1 - p) / 2).astype(np.float32)
        out[i:i + drop_len, 1] += (d * g * (1 + p) / 2).astype(np.float32)
    return dsp.bandpass(out, 400, 11000)


# ----------------------------------------------------------------- recettes

def far(x: np.ndarray, amount=0.5) -> np.ndarray:
    return dsp.distance(x, amount)


def add_birds(r, out, rate, gain_db, kinds=("whistle", "trill")):
    for tt in scatter_events(r, rate, 0.0, LOOP + TAIL - 2.0):
        k = kinds[int(r.integers(len(kinds)))]
        s = bird_whistle(r) if k == "whistle" else bird_trill(r)
        dsp.place(out, far(s, r.uniform(0.2, 0.6)), tt, dsp.from_db(gain_db + r.uniform(-8, 0)), r.uniform(-0.9, 0.9))


def make(sid: str) -> np.ndarray:
    r = dsp.rng(sum(map(ord, sid)) * 7 + 3)
    total = LOOP + TAIL
    out = buf()
    hall = dsp.make_ir(2.5, dsp.rng(11), predelay=0.03, damping_hz=5000.0)
    if sid == "amb_forest_day":
        out += textures.wind(total, 101, lo=700, hi=4500, gust_hz=0.09) * 0.35
        add_birds(r, out, 0.9, -14)
        for tt in scatter_events(r, 0.05, 2.0, LOOP - 2.0):
            dsp.place(out, far(woodpecker(r), 0.6), tt, dsp.from_db(-20), r.uniform(-0.8, 0.8))
        target = -24.0
    elif sid == "amb_forest_night":
        out += textures.wind(total, 102, lo=300, hi=2500, gust_hz=0.05) * 0.25
        out += cricket_bed(r, total, crickets=4, rate=1.1).astype(np.float32) * dsp.from_db(-18)
        for tt in scatter_events(r, 0.06, 1.0, LOOP - 3.0):
            dsp.place(out, far(owl(r), 0.5), tt, dsp.from_db(-12), r.uniform(-0.7, 0.7))
        target = -27.0
    elif sid == "amb_plains_day":
        out += textures.wind(total, 103, lo=400, hi=3500, gust_hz=0.07) * 0.6
        add_birds(r, out, 0.35, -18, kinds=("trill", "trill", "whistle"))
        target = -25.0
    elif sid == "amb_plains_night":
        out += textures.wind(total, 104, lo=250, hi=2000, gust_hz=0.05) * 0.3
        out += cricket_bed(r, total, crickets=9, rate=1.6).astype(np.float32) * dsp.from_db(-14)
        target = -26.0
    elif sid == "amb_swamp":
        out += textures.wind(total, 105, lo=200, hi=1200, gust_hz=0.04) * 0.2
        for tt in scatter_events(r, 2.2, 0.0, total - 1.0):
            dsp.place(out, far(frog(r), r.uniform(0.1, 0.6)), tt, dsp.from_db(-10 + r.uniform(-10, 0)), r.uniform(-0.9, 0.9))
        t = _t(total)
        buzz = np.sin(2 * np.pi * 620 * t) * (0.5 + 0.5 * np.sin(2 * np.pi * 118 * t)) * (0.6 + 0.4 * np.sin(2 * np.pi * 0.13 * t))
        out += dsp.pan(buzz.astype(np.float32), 0.3) * dsp.from_db(-34)
        out += stream_water(r, total) * dsp.from_db(-24)
        target = -24.0
    elif sid == "amb_mountain":
        w = textures.wind(total, 106, lo=150, hi=1800, gust_hz=0.11)
        t = _t(total)
        whistle = np.zeros(len(t), dtype=np.float32)
        n = r.standard_normal(len(t)).astype(np.float32)
        for c0 in (520.0, 780.0):
            centre = c0 * (1 + 0.25 * np.sin(2 * np.pi * 0.05 * t + r.uniform(0, 6)))
            seg = dsp.bandpass(n, c0 * 0.95, c0 * 1.05, 2)
            whistle += seg * (0.5 + 0.5 * np.sin(2 * np.pi * 0.08 * t + r.uniform(0, 6))).astype(np.float32) * (centre / c0).astype(np.float32)
        out += w + dsp.pan(whistle * 3.0, 0.2)
        target = -22.0
    elif sid == "amb_snow":
        w = textures.wind(total, 107, lo=600, hi=5500, gust_hz=0.08)
        out += w * 0.8 + textures.wind(total, 108, lo=150, hi=600, gust_hz=0.05) * 0.3
        target = -23.0
    elif sid == "amb_desert":
        out += textures.wind(total, 109, lo=300, hi=6000, gust_hz=0.06) * 0.8
        for tt in scatter_events(r, 60.0, 0.0, total - 0.01):
            g = dsp.samples(0.002)
            dsp.place(out, r.standard_normal(g) * 0.15, tt, 1.0, r.uniform(-1, 1))
        target = -25.0
    elif sid == "amb_coast":
        out += waves(r, total) + textures.wind(total, 110, lo=300, hi=2500, gust_hz=0.06) * 0.25
        target = -22.0
    elif sid == "amb_river":
        out += stream_water(r, total)
        target = -23.0
    elif sid == "amb_town":
        out += murmur(r, total)
        for tt in scatter_events(r, 0.25, 0.5, total - 2.0):
            # Forge : deux ou trois coups d'enclume, loin.
            for k in range(int(r.integers(2, 4))):
                dsp.place(out, far(perc.anvil(int(r.integers(1 << 20))), 0.55), tt + k * r.uniform(0.45, 0.6),
                          dsp.from_db(-22), 0.5)
        out += textures.wind(total, 111, lo=200, hi=1500, gust_hz=0.04) * 0.1
        target = -26.0
    elif sid == "amb_rain_light":
        out += rain(r, total, 180.0, -26)
        target = -24.0
    elif sid == "amb_rain_heavy":
        out += rain(r, total, 900.0, -16) + textures.wind(total, 112, lo=150, hi=1200, gust_hz=0.1) * 0.25
        target = -20.0
    elif sid.startswith("amb_thunder_"):
        k = int(sid[-1])
        th = textures.thunder(200 + k, 7.0)
        crack = dsp.bandpass(r.standard_normal(dsp.samples(0.4)), 300, 4000) * np.exp(-_t(0.4) / 0.08)
        x = np.zeros(len(th) + dsp.samples(0.5))
        x[: len(crack)] += crack * 0.3
        x[dsp.samples(0.1):dsp.samples(0.1) + len(th)] += th
        y = dsp.reverb(dsp.distance(x.astype(np.float32), 0.4), hall, wet=0.5, dry=0.8)
        y = dsp.normalize_lufs(y, -21.0)
        return dsp.limiter(y, -2.0)
    else:
        raise ValueError(sid)
    out = dsp.reverb(out, hall, wet=0.25, dry=1.0)
    loop = dsp.wrap_loop(out, dsp.samples(LOOP))
    loop = dsp.normalize_lufs(loop, target)
    return dsp.limiter(loop, -3.0)


SOUNDS = ["amb_forest_day", "amb_forest_night", "amb_plains_day", "amb_plains_night", "amb_swamp", "amb_mountain",
          "amb_snow", "amb_desert", "amb_coast", "amb_river", "amb_town", "amb_rain_light", "amb_rain_heavy",
          "amb_thunder_1", "amb_thunder_2", "amb_thunder_3"]


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--out", default="game/data/audio/ambience")
    ap.add_argument("--only", default=None)
    a = ap.parse_args()
    out = Path(a.out)
    for sid in SOUNDS:
        if a.only and sid != a.only:
            continue
        dsp.write_ogg(make(sid), out / f"{sid}.ogg", quality=3)
        print(f"  {sid}")
    print(f"écrit : ambiances dans {out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
