"""SND.1 — « Aux portes de la Lune Noire » : musique de l'écran de connexion.

Piste instrumentale de 8 min 50 s, bouclée sans couture, écrite d'après le
cahier des charges du projet (docs/audio/SND.1_musique_menu.md) :

  A  0:00–1:30  L'approche            pulsation lointaine des tambours de guerre, bourdon grave
  B  1:30–3:30  Le monde se dévoile   percussions complètes, cordes et cuivres, héroïsme mélancolique
  C  3:30–5:30  L'appel de l'aventure rythmes imbriqués, thème principal (cor puis tutti)
  D  5:30–7:30  Au cœur des ténèbres  battement de cœur, vent, cors lointains, flûte ; reconstruction
  E  7:30–8:50  Le retour inéluctable reprise du thème en force, cadence, retour à la pulsation

72 noires/min, mesure à 4 temps (3,33 s), ré mineur. La pulsation des taikos
(« DOOM — DOOM ») ne s'arrête jamais. Les 8 dernières mesures reproduisent
les 8 premières (pulsation lointaine) et les queues de réverbération sont
repliées sur le début (dsp.wrap_loop) : la boucle est inaudible.

Usage :  python3 menu_theme.py [--out game/data/audio/music/menu_lune_noire.ogg] [--preview 60]
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))

from lcdlln_audio import dsp, percussion as perc, textures  # noqa: E402
from lcdlln_audio.orchestra import GM, Part, note, render  # noqa: E402

BPM = 72.0
BEAT = 60.0 / BPM          # 0,833 s
BAR = 4 * BEAT             # 3,333 s

# Sections : (nom, première mesure, nombre de mesures).
SECTIONS = [("A", 0, 27), ("B", 27, 36), ("C", 63, 36), ("D", 99, 36), ("E", 135, 24)]
TOTAL_BARS = 159
LOOP_SECONDS = TOTAL_BARS * BAR  # 530 s
TAIL_SECONDS = 12.0

# ----------------------------------------------------------------- harmonie

CHORDS = {
    "Dm": ("D", ["D", "F", "A"]), "Bb": ("Bb", ["Bb", "D", "F"]), "Gm": ("G", ["G", "Bb", "D"]),
    "A": ("A", ["A", "C#", "E"]), "C": ("C", ["C", "E", "G"]), "F": ("F", ["F", "A", "C"]),
    "Eb": ("Eb", ["Eb", "G", "Bb"]), "Dm/Eb": ("D", ["Eb", "G", "Bb"]),
}
P1 = ["Dm", "Bb", "Gm", "A"]
P2 = ["Dm", "C", "Bb", "A"]
P3 = ["Bb", "F", "Gm", "A"]
DREAD = ["Dm", "Dm/Eb", "Dm", "Dm/Eb"]


def build_harmony() -> list[str]:
    """Accord de chaque mesure (159 entrées)."""
    h: list[str] = []
    # A — 27 mesures : pédale, puis menace (mi bémol sur ré), puis dominante.
    h += ["Dm"] * 8 + DREAD * 4 + ["Bb", "A", "A"]
    # B — 36 mesures.
    h += P1 * 2 + P1 * 2 + P3 * 2 + P1 * 2 + ["Bb", "Gm", "A", "A"]
    # C — 36 mesures.
    h += ["Dm"] * 4 + P1 * 2 + P3 * 2 + P1 * 2 + P2 + ["Bb", "Gm", "A", "A"]
    # D — 36 mesures.
    h += DREAD * 4 + P2 * 2 + P1 * 2 + ["Bb", "Gm", "A", "A"]
    # E — 24 mesures : reprise, cadence, retour à la pulsation.
    h += P1 * 3 + ["Bb", "Gm", "A", "Dm"] + ["Dm"] * 8
    assert len(h) == TOTAL_BARS, len(h)
    return h


PC = {"C": 0, "C#": 1, "Db": 1, "D": 2, "Eb": 3, "E": 4, "F": 5, "F#": 6, "G": 7, "Ab": 8, "A": 9, "Bb": 10, "B": 11}


def voicing(chord: str, low: int, high: int) -> list[int]:
    """Toutes les notes de l'accord entre deux hauteurs MIDI (incluses)."""
    pcs = {PC[n] for n in CHORDS[chord][1]}
    return [p for p in range(low, high + 1) if p % 12 in pcs]


def root(chord: str, octave: int) -> int:
    return note(CHORDS[chord][0] + str(octave))


def bass_root(chord: str) -> int:
    """Fondamentale de basse entre G1 et F#2 (registre grave confortable)."""
    r = root(chord, 2)
    return r - 12 if r > note("F#2") else r


# ----------------------------------------------------------------- thèmes

# Thème principal (8 mesures sur P1×2) : (hauteur, durée en noires).
THEME = [
    [("D4", 2), ("A4", 1.5), ("G4", 0.5)],
    [("F4", 1), ("E4", 1), ("D4", 2)],
    [("G4", 2), ("Bb4", 1), ("A4", 1)],
    [("A4", 3), ("E4", 1)],
    [("A4", 3), ("D5", 1)],
    [("D5", 1), ("C5", 1), ("Bb4", 1), ("A4", 1)],
    [("Bb4", 2), ("G4", 1), ("A4", 1)],
    [("A4", 4)],
]
# Variante de fin (cadence en ré) pour la reprise E.
THEME_END = [("D5", 2), ("C#5", 1), ("E5", 1)]
# Thème secondaire (4 mesures sur P3), joué deux fois (2e fin variée).
SECOND = [
    [("D5", 3), ("C5", 1)],
    [("A4", 2), ("C5", 2)],
    [("Bb4", 1), ("A4", 1), ("G4", 1), ("F4", 1)],
    [("E4", 4)],
]
SECOND_END = [("C#5", 2), ("A4", 2)]
# Motif de la flûte (cœur des ténèbres) : lent, fragile, sur la pédale de ré.
# Aligné sur DREAD (ré mineur / mi bémol sur pédale de ré) : couleur phrygienne.
FLUTE_MOTIF = [
    [("A4", 3), ("Bb4", 1)],
    [("Bb4", 2), ("G4", 2)],
    [("F4", 1.5), ("E4", 0.5), ("D4", 1), ("F4", 1)],
    [("Eb4", 2), ("D4", 2)],
]


def put_melody(part: Part, bar: int, phrase, transpose: int = 0, vel: int = 80, legato: float = 1.03) -> None:
    """Écrit une phrase (liste de mesures) à partir de la mesure `bar`."""
    for i, measure in enumerate(phrase):
        t = (bar + i) * 4.0
        for (p, d) in measure:
            part.add(t, d * legato, note(p) + transpose, vel)
            t += d


# ----------------------------------------------------------------- partition

def bars_of(name: str) -> range:
    for n, start, count in SECTIONS:
        if n == name:
            return range(start, start + count)
    raise KeyError(name)


def compose(h: list[str]) -> dict[str, Part]:
    """Construit tous les pupitres orchestraux."""
    P = {
        "contrabass": Part("contrabass", GM["contrabass"], volume=105),
        "cello": Part("cello", GM["cello"], volume=100),
        "ostinato": Part("ostinato", GM["strings"], volume=92),
        "pad": Part("pad", GM["strings_slow"], volume=96),
        "violins": Part("violins", GM["strings"], volume=100),
        "horns": Part("horns", GM["horn"], volume=104),
        "horn_solo": Part("horn_solo", GM["horn"], volume=108),
        "low_brass": Part("low_brass", GM["trombone"], volume=100),
        "tuba": Part("tuba", GM["tuba"], volume=100),
        "choir": Part("choir", GM["voice_oohs"], volume=92),
        "flute": Part("flute", GM["flute"], volume=96),
        "oboe": Part("oboe", GM["oboe"], volume=92),
        "tremolo": Part("tremolo", GM["tremolo"], volume=94),
        "timpani": Part("timpani", GM["timpani"], volume=110),
        "far_horns": Part("far_horns", GM["horn"], volume=100),
    }
    A, B, C, D, E = (bars_of(x) for x in "ABCDE")

    # --- Bourdon / basse --------------------------------------------------
    cb, ce = P["contrabass"], P["cello"]
    # A : bourdon de ré grave qui naît à la mesure 5, enfle jusqu'à B.
    cb.add(4 * 4, 23 * 4, "D1", 70)
    ce.add(6 * 4, 18 * 4, "D2", 64)
    cb.ramp(4 * 4, 26 * 4, 30, 100)
    ce.ramp(6 * 4, 24 * 4, 25, 90)
    ce.add(24 * 4, 4, "Bb1", 70)
    ce.add(25 * 4, 8, "A1", 76)
    for b in list(B) + list(C) + list(E):
        if b >= 151:
            break
        r = bass_root(h[b])
        cb.add(b * 4, 4.05, r, 82)
        ce.add(b * 4, 4.05, r + 12, 74)
        ce.add(b * 4, 4.05, r + 19 if h[b] not in ("Dm/Eb",) else r + 12, 60)
    cb.ramp(27 * 4, 30 * 4, 90, 110)
    ce.ramp(26 * 4, 30 * 4, 90, 112)
    # D : pédale de ré, très douce, puis retour des basses à la reconstruction.
    cb.add(99 * 4, 16 * 4, "D1", 66)
    ce.add(99 * 4, 16 * 4, "D2", 56)
    cb.ramp(99 * 4, 101 * 4, 100, 70)
    ce.ramp(99 * 4, 101 * 4, 100, 60)
    for b in range(115, 135):
        r = bass_root(h[b])
        cb.add(b * 4, 4.05, r, 80)
        ce.add(b * 4, 4.05, r + 12, 70)
    cb.ramp(115 * 4, 134 * 4, 70, 115)
    ce.ramp(115 * 4, 134 * 4, 60, 115)
    # E final : ré tenu qui s'éteint dans la pulsation (miroir de l'ouverture).
    cb.add(151 * 4, 8 * 4, "D1", 70)
    ce.add(151 * 4, 6 * 4, "D2", 60)
    cb.ramp(135 * 4, 150 * 4, 115, 115)
    cb.ramp(151 * 4, 158 * 4, 105, 25)
    ce.ramp(151 * 4, 157 * 4, 100, 15)

    # --- Nappe de cordes ---------------------------------------------------
    pad = P["pad"]
    def pad_bars(bars, lo, hi, vel):
        for b in bars:
            for p in voicing(h[b], lo, hi):
                pad.add(b * 4, 4.1, p, vel)
    pad_bars(range(12, 27), note("F3"), note("A4"), 62)
    pad.ramp(12 * 4, 26 * 4, 20, 88)
    pad_bars(B, note("D3"), note("D5"), 76)
    pad.ramp(27 * 4, 34 * 4, 70, 100)
    pad.ramp(59 * 4, 62 * 4, 105, 80)
    pad_bars(range(67, 99), note("F3"), note("F5"), 74)
    pad.ramp(66 * 4, 98 * 4, 85, 112)
    pad_bars(range(99, 135), note("F3"), note("A4"), 58)
    pad.ramp(99 * 4, 114 * 4, 45, 45)
    pad.ramp(115 * 4, 134 * 4, 50, 110)
    pad_bars(range(135, 151), note("D3"), note("D5"), 82)
    pad.ramp(135 * 4, 150 * 4, 115, 120)
    pad.ramp(151 * 4, 156 * 4, 105, 10)
    for p in voicing("Dm", note("D3"), note("A4")):
        pad.add(151 * 4, 20, p, 70)

    # --- Ostinato des cordes graves (croches) : C et reconstruction de D/E ---
    ost = P["ostinato"]
    def ostinato(bars, vel):
        for b in bars:
            r = bass_root(h[b]) + 12
            fifth = r + 7
            pat = [r, r, fifth, r, r + 12, r, fifth, r]
            for k, p in enumerate(pat):
                accent = 14 if k in (0, 3, 6) else 0
                ost.add(b * 4 + k * 0.5, 0.42, p, vel + accent)
    ostinato(range(67, 99), 72)
    ostinato(range(123, 135), 70)
    ostinato(range(135, 151), 76)
    ost.ramp(66 * 4, 70 * 4, 70, 105)
    ost.ramp(123 * 4, 134 * 4, 60, 105)
    ost.ramp(135 * 4, 150 * 4, 108, 112)

    # --- Violons : thème en tutti ---------------------------------------
    vn = P["violins"]
    put_melody(vn, 51, THEME, 12, 84)           # B : thème aux cordes, à l'octave
    vn.ramp(51 * 4, 58 * 4, 85, 110)
    put_melody(vn, 83, THEME, 12, 92)           # C : tutti
    vn.ramp(83 * 4, 90 * 4, 100, 118)
    put_melody(vn, 75, SECOND, 0, 70)           # C : thème secondaire (doublure flûte)
    put_melody(vn, 79, SECOND[:3] + [SECOND_END], 0, 72)
    put_melody(vn, 135, THEME, 12, 96)          # E : reprise en force
    put_melody(vn, 143, THEME[:3] + [THEME_END], 12, 100)
    vn.ramp(135 * 4, 147 * 4, 115, 122)
    vn.add(147 * 4, 4, "D5", 96)
    vn.add(148 * 4, 4, "D5", 100)
    vn.add(149 * 4, 4, "C#5", 104)
    vn.add(150 * 4, 6, "D5", 108)
    vn.ramp(150 * 4, 152 * 4, 122, 40)

    # --- Cors : accords, puis thème -----------------------------------------
    hn = P["horns"]
    for b in list(range(27, 35)) + list(range(43, 63)):
        for p in voicing(h[b], note("A2"), note("D4"))[-3:]:
            hn.add(b * 4, 4.0, p, 72)
    hn.ramp(27 * 4, 31 * 4, 40, 96)
    hn.ramp(43 * 4, 62 * 4, 90, 112)
    for b in range(83, 99):
        for p in voicing(h[b], note("A2"), note("F4"))[-3:]:
            hn.add(b * 4, 4.0, p, 86)
    hn.ramp(83 * 4, 98 * 4, 108, 124)
    put_melody(hn, 135, THEME, 0, 100)
    put_melody(hn, 143, THEME[:3] + [THEME_END], 0, 104)
    hn.chord(147 * 4, 4, [note("Bb2"), note("F3"), note("D4")], 100)
    hn.chord(148 * 4, 4, [note("G2"), note("D3"), note("Bb3")], 104)
    hn.chord(149 * 4, 4, [note("A2"), note("E3"), note("C#4")], 110)
    hn.chord(150 * 4, 8, [note("D3"), note("A3"), note("F4")], 112)
    hn.ramp(135 * 4, 150 * 4, 118, 126)
    hn.ramp(150 * 4, 152 * 4, 126, 30)

    # Cor solo : thème doux en B, thème « appel » en C.
    hs = P["horn_solo"]
    put_melody(hs, 35, THEME, 0, 70)
    hs.ramp(35 * 4, 42 * 4, 70, 95)
    put_melody(hs, 67, THEME, 0, 88)
    hs.ramp(67 * 4, 74 * 4, 95, 115)

    # --- Cuivres graves -------------------------------------------------------
    lb, tu = P["low_brass"], P["tuba"]
    lb.add(8 * 4, 6 * 4, "D2", 60)            # la note grave unique de l'approche
    lb.ramp(8 * 4, 14 * 4, 5, 80)
    lb.add(16 * 4, 8 * 4, "D2", 64)
    lb.ramp(16 * 4, 24 * 4, 10, 95)
    lb.ramp(24 * 4, 26 * 4, 95, 40)
    for b in list(range(51, 63)) + list(range(87, 99)) + list(range(131, 151)):
        r = bass_root(h[b]) + 12
        lb.add(b * 4, 4, r, 84)
        tu.add(b * 4, 4, r - 12, 80)
    lb.ramp(51 * 4, 62 * 4, 80, 110)
    tu.ramp(51 * 4, 62 * 4, 80, 110)
    lb.ramp(87 * 4, 98 * 4, 100, 122)
    tu.ramp(87 * 4, 98 * 4, 100, 122)
    lb.ramp(131 * 4, 150 * 4, 95, 124)
    tu.ramp(131 * 4, 150 * 4, 95, 124)
    lb.add(150 * 4, 8, "D2", 100)
    tu.add(150 * 4, 8, "D1", 96)
    lb.ramp(150 * 4, 152 * 4, 124, 30)
    tu.ramp(150 * 4, 152 * 4, 124, 30)

    # --- Chœur (sans paroles) -------------------------------------------------
    ch = P["choir"]
    for b in list(range(16, 27)) + list(range(43, 63)) + list(range(99, 115)) + list(range(135, 151)):
        for p in voicing(h[b], note("D3"), note("A4"))[-3:]:
            ch.add(b * 4, 4.1, p, 64)
    ch.ramp(16 * 4, 26 * 4, 15, 70)
    ch.ramp(43 * 4, 62 * 4, 70, 92)
    ch.ramp(99 * 4, 114 * 4, 55, 40)
    ch.ramp(135 * 4, 150 * 4, 95, 105)
    ch.ramp(150 * 4, 152 * 4, 100, 20)

    # --- Bois -------------------------------------------------------------------
    fl, ob = P["flute"], P["oboe"]
    put_melody(fl, 75, SECOND, 12, 76)
    put_melody(fl, 79, SECOND[:3] + [SECOND_END], 12, 78)
    fl.ramp(75 * 4, 82 * 4, 80, 100)
    # D : motif fragile, deux fois, le second une octave plus haut.
    put_melody(fl, 101, FLUTE_MOTIF, 0, 66, legato=1.0)
    put_melody(fl, 107, FLUTE_MOTIF, 12, 62, legato=1.0)
    fl.ramp(101 * 4, 112 * 4, 80, 70)
    # Hautbois : contre-chant en B (thème secondaire), écho en D.
    put_melody(ob, 43, SECOND, 0, 72)
    put_melody(ob, 47, SECOND[:3] + [SECOND_END], 0, 74)
    ob.ramp(43 * 4, 50 * 4, 75, 95)
    put_melody(ob, 111, FLUTE_MOTIF[:2], -12, 58, legato=1.0)
    ob.ramp(111 * 4, 113 * 4, 60, 60)

    # --- Trémolo : tension de la reconstruction -------------------------------
    tr = P["tremolo"]
    for b in range(115, 135):
        for p in voicing(h[b], note("A3"), note("A5"))[-4:]:
            tr.add(b * 4, 4.05, p, 70)
    tr.ramp(115 * 4, 134 * 4, 20, 120)
    for b in range(95, 99):
        for p in voicing(h[b], note("A3"), note("A5"))[-4:]:
            tr.add(b * 4, 4.05, p, 80)
    tr.ramp(95 * 4, 98 * 4, 70, 120)

    # --- Timbales : roulements aux transitions, coups de cadence ---------------
    tp = P["timpani"]
    def roll(b0, bars, pitch, v0, v1):
        n = int(bars * 4 * 6)
        for k in range(n):
            tp.add(b0 * 4 + k / 6.0, 0.2, pitch, int(v0 + (v1 - v0) * k / max(1, n - 1)))
    roll(25, 2, note("A1"), 40, 110)
    roll(61, 2, note("A1"), 50, 115)
    roll(97, 2, note("A1"), 60, 120)
    roll(133, 2, note("A1"), 50, 122)
    for b, p in ((147, "Bb1"), (148, "G1"), (149, "A1")):
        tp.add(b * 4, 1, p, 115)
        tp.add(b * 4 + 2, 1, p, 100)
    tp.add(150 * 4, 2, "D2", 127)

    # --- Cors lointains (cœur des ténèbres) -----------------------------------
    fh = P["far_horns"]
    for b, phrase in ((103, [[("D3", 2), ("A3", 2)], [("G3", 3), ("F3", 1)], [("F3", 4)]]),
                      (109, [[("A3", 3), ("Bb3", 1)], [("Bb3", 2), ("G3", 2)]]),
                      (117, [[("D4", 2), ("C4", 2)], [("Bb3", 2), ("A3", 2)]])):
        put_melody(fh, b, phrase, 0, 70)
    return P


# ----------------------------------------------------------------- percussions

class Bank:
    """Banque de variations pré-calculées par instrument (frappes humanisées)."""

    def __init__(self, fn, count: int, seed: int, **kw):
        self.v = [fn(seed + i, **kw) for i in range(count)]
        self.i = 0

    def next(self) -> np.ndarray:
        self.i = (self.i + 1) % len(self.v)
        return self.v[self.i]


def section_of(bar: int) -> str:
    for n, start, count in SECTIONS:
        if start <= bar < start + count:
            return n
    return "E"


def compose_drums(n: int) -> tuple[np.ndarray, np.ndarray]:
    """Renvoie (bus proche, bus lointain) stéréo de toutes les percussions."""
    r = dsp.rng(424242)
    near = np.zeros((n, 2), dtype=np.float32)
    far = np.zeros((n, 2), dtype=np.float32)
    big = Bank(perc.taiko, 8, 100, f0=50.0, decay=1.2)
    mid_taiko = Bank(perc.taiko, 8, 200, f0=74.0, decay=0.7)
    battle_l = Bank(perc.battle_drum, 8, 300, f0=150.0)
    battle_r = Bank(perc.battle_drum, 8, 400, f0=172.0, snare=0.25)
    frame = Bank(perc.frame_drum, 6, 500)
    djb = {s: Bank(perc.djembe, 4, 600 + k * 10, stroke=s) for k, s in enumerate(("bass", "tone", "slap"))}
    finger = Bank(perc.finger_drum, 8, 700)
    shk = Bank(perc.shaker, 8, 800)
    wood = Bank(perc.woodblock, 4, 900)
    anv = Bank(perc.anvil, 4, 1000)
    chn = Bank(perc.chains, 4, 1100)
    boom = Bank(perc.cinematic_boom, 3, 1200)

    def hit(buf, bank, bar, beat, gain_db, p=0.0, jitter=0.006):
        t = (bar * 4 + beat) * BEAT + r.uniform(-jitter, jitter)
        g = dsp.from_db(gain_db + r.uniform(-1.2, 1.2))
        dsp.place(buf, bank.next(), max(0.0, t), g, p)

    for bar in range(TOTAL_BARS):
        s = section_of(bar)
        # 1) Pulsation fondamentale des grands taikos : jamais interrompue.
        if s == "A" or bar >= 151:
            # Lointaine, qui se rapproche au fil de l'approche (miroir en fin de piste).
            k = (bar / 26.0) if s == "A" else (158 - bar) / 8.0
            gain = -14.0 + 8.0 * min(1.0, max(0.0, k))
            hit(far, big, bar, 0, gain)
            hit(far, big, bar, 2, gain - 1.5)
            if bar >= 17 and s == "A":
                hit(far, big, bar, 3.5, gain - 6.0, 0.2)
        elif s == "D" and bar < 123:
            # Battement de cœur : « lub-dub ».
            hit(near, big, bar, 0, -18.0)
            hit(near, big, bar, 0.42, -23.0)
        else:
            hit(near, big, bar, 0, -4.0, -0.1)
            hit(near, big, bar, 2, -5.0, 0.1)
            if s in ("C", "E") or (s == "D" and bar >= 123):
                for beat, g, p in ((1.5, -10, -0.4), (2.75, -12, 0.4), (3.0, -9, -0.4), (3.5, -10, 0.4)):
                    hit(near, mid_taiko, bar, beat, g, p)
            elif s == "B":
                hit(near, mid_taiko, bar, 3.5, -10.0, 0.3)
                if bar % 2 == 1:
                    hit(near, mid_taiko, bar, 1.5, -12.0, -0.3)

        # 2) Tambours de bataille (élan).
        if s == "B" and bar >= 31:
            for k, acc in enumerate([1, 0, 1, 1, 1, 0, 1, 0]):
                if acc:
                    hit(near, battle_l, bar, k * 0.5, -15.0 if k % 2 else -12.0, -0.45)
        if s in ("C", "E") or (s == "D" and bar >= 123):
            pat_l = [1, 0, 0, 1, 0, 0, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0]
            pat_r = [0, 0, 1, 0, 1, 0, 0, 1, 0, 0, 1, 0, 1, 0, 0, 1]
            base = -13.0 if s != "D" else -16.0 + (bar - 123) * 0.3
            for k in range(16):
                if pat_l[k]:
                    hit(near, battle_l, bar, k * 0.25, base - (0 if k % 4 == 0 else 3), -0.5)
                if pat_r[k]:
                    hit(near, battle_r, bar, k * 0.25, base - 2, 0.5)

        # 3) Petites percussions rapides (texture aiguë).
        if s == "A" and bar >= 17:
            for k in range(0, 8, 2):
                hit(far, shk, bar, k * 0.5 + 0.5, -26.0, 0.6)
        if s == "B":
            for k in range(16):
                hit(near, shk, bar, k * 0.25, -27.0 if k % 2 else -24.0, 0.55)
        if s in ("C", "E"):
            for k in range(16):
                if k % 4 != 0:
                    hit(near, finger, bar, k * 0.25, -24.0 if k % 2 else -21.0, -0.6 if (k // 2) % 2 else 0.6)
                hit(near, shk, bar, k * 0.25, -26.0, 0.7)
            if bar % 2 == 0:
                for beat in (0.75, 2.25, 3.25):
                    hit(near, wood, bar, beat, -22.0, 0.35)
        if s == "D" and bar < 123 and bar % 4 == 2:
            hit(far, finger, bar, 1.0, -18.0, -0.5)
            hit(far, wood, bar, 3.0, -20.0, 0.5)

        # 4) Tambours du monde (couleur entre les sections).
        if s == "A" and bar >= 17:
            for beat in (1.0, 2.5, 3.0):
                hit(far, frame, bar, beat, -16.0, -0.3)
        if s == "B" and bar >= 43:
            hit(near, frame, bar, 1.0, -16.0, 0.4)
            hit(near, frame, bar, 3.0, -17.0, -0.4)
        if s == "C" and bar >= 75:
            for beat, stroke, g in ((0.5, "tone", -17), (1.0, "slap", -19), (1.75, "tone", -18),
                                    (2.5, "bass", -15), (3.25, "slap", -19), (3.75, "tone", -18)):
                hit(near, djb[stroke], bar, beat, g, 0.25)
        if s == "D" and bar < 123 and bar % 2 == 1:
            hit(far, frame, bar, 2.0, -14.0, 0.0)

        # 5) Métal : armures et armes.
        if s == "B" and bar % 4 == 3:
            hit(near, anv, bar, 3.5, -22.0, 0.5)
        if s in ("C", "E") and bar % 8 == 4:
            hit(near, chn, bar, 1.5, -20.0, -0.6)
        if s in ("C", "E") and bar % 4 == 0:
            hit(near, anv, bar, 0.0, -23.0, 0.55)
        if s == "D" and bar in (104, 112):
            hit(far, anv, bar, 0.0, -14.0, 0.0)

    # 6) Gongs et impacts : charnières de la forme.
    for bar, g in ((16, -16.0), (27, -8.0), (63, -7.0), (99, -9.0), (135, -6.0), (150, -5.0)):
        dsp.place(far if bar == 16 else near, perc.gong(bar), bar * BAR, dsp.from_db(g), 0.0)
    for bar in (27, 63, 83, 135, 150):
        dsp.place(near, boom.next(), bar * BAR, dsp.from_db(-6.0), 0.0)
    return near, far


# ----------------------------------------------------------------- mixage

ORCH_MIX = {  # pupitre : (gain dB, panoramique)
    "contrabass": (-1.0, 0.0), "cello": (-2.0, -0.15), "ostinato": (-4.0, 0.2), "pad": (-3.0, 0.0),
    "violins": (7.0, -0.3), "horns": (-5.0, 0.25), "horn_solo": (3.0, -0.1), "low_brass": (-4.0, 0.3),
    "tuba": (-5.0, 0.15), "choir": (-6.0, 0.0), "flute": (1.0, -0.35), "oboe": (7.0, 0.35),
    "tremolo": (-6.0, -0.2), "timpani": (-3.0, 0.0), "far_horns": (0.0, 0.0),
}


def stereo_width(x: np.ndarray, p: float) -> np.ndarray:
    """Place un rendu stéréo FluidSynth à un panoramique donné (rendu ≈ mono centré)."""
    m = x.mean(axis=1)
    return (dsp.pan(m, p) + 0.25 * (x - m[:, None])).astype(np.float32)


def build(preview: float | None = None, verbose: bool = True) -> np.ndarray:
    """Compose, rend et mixe la piste. Renvoie la boucle stéréo (LOOP_SECONDS)."""
    total = LOOP_SECONDS + TAIL_SECONDS
    n = dsp.samples(total)
    h = build_harmony()
    parts = compose(h)
    r = dsp.rng(7)
    hall = dsp.make_ir(3.2, r, predelay=0.025, damping_hz=7000.0)
    cave = dsp.make_ir(6.5, r, predelay=0.06, damping_hz=2500.0)

    orch = np.zeros((n, 2), dtype=np.float32)
    far_orch = np.zeros((n, 2), dtype=np.float32)
    for name, part in parts.items():
        if verbose:
            print(f"  rendu : {name} ({len(part.notes)} notes)")
        x = render(part, BPM, total)
        g, p = ORCH_MIX[name]
        x = stereo_width(x, p) * dsp.from_db(g)
        if name == "far_horns":
            far_orch += x
        else:
            orch += x
    if verbose:
        print("  percussions et textures…")
    near, far = compose_drums(n)
    # Textures : vent (approche, cœur des ténèbres, fin), tonnerre lointain, souffle grave.
    tex = textures.wind(total, 31)
    bar_pos = (np.arange(n, dtype=np.float32) / dsp.SR) / BAR
    wind_env = np.interp(bar_pos, [0, 20, 27, 30, 99, 101, 118, 124, 150, 153, 160],
                         [0.55, 0.5, 0.25, 0.08, 0.08, 0.6, 0.5, 0.12, 0.1, 0.45, 0.55]).astype(np.float32)
    del bar_pos
    tex *= (wind_env * dsp.from_db(-14.0))[:, None]
    del wind_env
    for bar, seed in ((12, 41), (21, 42), (104, 43), (119, 44)):
        dsp.place(tex, textures.thunder(seed), bar * BAR, dsp.from_db(-10.0), 0.0)

    # Bus : réverbération de salle (orchestre, percussions proches), caverne
    # (éléments lointains). Les tampons intermédiaires sont libérés au fur et à
    # mesure (9 minutes en stéréo 48 kHz : ~100 Mo par tampon).
    # L'orchestre rendu par FluidSynth sort ~12 dB sous les percussions : +9 dB
    # le remettent à égalité (les tambours restent l'épine dorsale).
    mix = dsp.reverb(orch, hall, wet=0.32, dry=0.9)[:n] * dsp.from_db(9.0)
    del orch
    mix += dsp.reverb(near, hall, wet=0.22, dry=1.0)[:n] * dsp.from_db(-1.0)
    del near
    far_bus = dsp.distance(far + far_orch * 0.8, 0.55)
    del far, far_orch
    mix += dsp.reverb(far_bus, cave, wet=0.75, dry=0.45)[:n]
    del far_bus
    mix += dsp.reverb(tex, cave, wet=0.35, dry=0.8)[:n]
    del tex
    mix += textures.drone_noise(total, 51) * dsp.from_db(-30.0)
    mix = dsp.highpass(mix, 28.0, 2)
    loop = dsp.wrap_loop(mix, dsp.samples(LOOP_SECONDS))
    loop = dsp.normalize_lufs(loop, -15.5)
    loop = dsp.limiter(loop, -1.0)
    if preview:
        loop = loop[: dsp.samples(preview)]
    return loop


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--out", default="game/data/audio/music/menu_lune_noire.ogg")
    ap.add_argument("--preview", type=float, default=None, help="ne garder que les N premières secondes")
    ap.add_argument("--wav", default=None, help="écrit aussi un WAV 32 bits (analyse)")
    a = ap.parse_args()
    print("SND.1 — composition et rendu…")
    loop = build(a.preview)
    if a.wav:
        import soundfile as sf
        sf.write(a.wav, loop.astype(np.float32), dsp.SR, subtype="FLOAT")
    dsp.write_ogg(loop, Path(a.out), quality=6)
    print(f"écrit : {a.out} ({len(loop) / dsp.SR:.1f} s, {dsp.loudness_lufs(loop):.1f} LUFS)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
