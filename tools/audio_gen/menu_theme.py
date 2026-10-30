"""SND.1 (v2) — « Aux portes de la Lune Noire » : musique de l'écran de connexion.

Version 2 après écoute (30/10/2026) : la v1 sonnait trop « batterie » et trop
électronique, l'ouverture était trop longue et les tambours trop lointains.
Cette version :

  - n'utilise plus de percussions de synthèse : grands tambours taiko, grosse
    caisse de concert, timbales et cymbales sont des sons ENREGISTRÉS (banque
    FluidR3_GM) ; seul le gong (tam-tam) reste synthétisé ;
  - place les tambours tout près, puissants, dès la première mesure ;
  - ouvre en ≈ 30 s (9 mesures) avant que l'ensemble soit en place ;
  - alterne une marche lourde (70 noires/min) et une marche de conquête
    (90 noires/min).

  O   0:00–0:31  Ouverture               tambours de puissance + bourdon, l'orchestre se rassemble
  B   0:31–2:07  Le monde se dévoile     marche lourde, thème aux cors, contre-chant du hautbois
  C   2:07–4:15  L'appel de la conquête  marche à 90, ostinato des cordes, thème en tutti
  D   4:15–6:05  Au cœur des ténèbres    battement de cœur des taikos (proche), flûte ; reconstruction
  C2  6:05–7:52  La conquête             marche à 90, thème aux cuivres, tutti
  E   7:52–8:53  Le retour inéluctable   marche lourde maestoso, cadence, gong, retour à l'ouverture

Ré mineur. Les tambours ne s'arrêtent jamais. La fin reprend le climat de
l'ouverture et les queues de réverbération sont repliées sur le début
(dsp.wrap_loop) : la boucle est inaudible.

Usage :  python3 menu_theme.py [--out game/data/audio/music/menu_lune_noire.ogg]
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))

from lcdlln_audio import dsp, percussion as perc, textures  # noqa: E402
from lcdlln_audio.orchestra import GM, Part, note, render  # noqa: E402

HEAVY = 70.0     # marche lourde
CONQUEST = 90.0  # marche de conquête

# Sections : (nom, nombre de mesures, tempo).
SECTIONS = [("O", 9, HEAVY), ("B", 28, HEAVY), ("C", 48, CONQUEST), ("D", 32, HEAVY),
            ("C2", 40, CONQUEST), ("E", 18, HEAVY)]
TOTAL_BARS = sum(c for _, c, _ in SECTIONS)  # 175
TAIL_SECONDS = 12.0


def section_starts() -> dict[str, int]:
    """Première mesure de chaque section."""
    out, b = {}, 0
    for name, count, _ in SECTIONS:
        out[name] = b
        b += count
    return out


START = section_starts()


def bpm_of_bar(bar: int) -> float:
    for name, count, bpm in SECTIONS:
        if START[name] <= bar < START[name] + count:
            return bpm
    return SECTIONS[-1][2]


def section_of(bar: int) -> str:
    for name, count, _ in SECTIONS:
        if START[name] <= bar < START[name] + count:
            return name
    return SECTIONS[-1][0]


def tempo_map() -> list[tuple[float, float]]:
    """Carte de tempo MIDI : [(temps en noires, bpm)] à chaque changement de section."""
    return [(START[name] * 4.0, bpm) for name, _, bpm in SECTIONS]


def time_of_beat(beat: float) -> float:
    """Instant (s) d'un temps global (en noires), selon la carte de tempo."""
    t = 0.0
    for name, count, bpm in SECTIONS:
        b0 = START[name] * 4.0
        b1 = b0 + count * 4.0
        if beat < b1 or name == SECTIONS[-1][0]:
            return t + (beat - b0) * 60.0 / bpm
        t += (b1 - b0) * 60.0 / bpm
    return t


LOOP_SECONDS = time_of_beat(TOTAL_BARS * 4.0)

# ----------------------------------------------------------------- harmonie

CHORDS = {
    "Dm": ("D", ["D", "F", "A"]), "Bb": ("Bb", ["Bb", "D", "F"]), "Gm": ("G", ["G", "Bb", "D"]),
    "A": ("A", ["A", "C#", "E"]), "C": ("C", ["C", "E", "G"]), "F": ("F", ["F", "A", "C"]),
    "Dm/Eb": ("D", ["Eb", "G", "Bb"]),
}
P1 = ["Dm", "Bb", "Gm", "A"]
P2 = ["Dm", "C", "Bb", "A"]
P3 = ["Bb", "F", "Gm", "A"]
DREAD = ["Dm", "Dm/Eb", "Dm", "Dm/Eb"]
TURN = ["Bb", "Gm", "A", "A"]


def build_harmony() -> list[str]:
    """Accord de chaque mesure (175 entrées)."""
    h: list[str] = []
    h += ["Dm"] * 4 + ["Bb", "Gm", "A", "A", "A"]                       # O (9)
    h += P1 * 2 + P1 * 2 + P3 * 2 + TURN                                # B (28)
    h += ["Dm"] * 4 + P1 * 2 + P1 * 2 + P3 * 2 + P2 * 2 + P1 * 2 + TURN  # C (48)
    h += DREAD * 4 + P2 * 2 + P1 + TURN                                 # D (32)
    h += P1 * 2 + P3 * 2 + P1 * 2 + P2 * 2 + ["Bb", "Gm", "A", "Dm"] + ["Dm", "Dm", "Bb", "A"]  # C2 (40)
    h += P1 * 2 + ["Bb", "Gm", "A", "Dm"] + ["Dm"] * 6                  # E (18)
    assert len(h) == TOTAL_BARS, len(h)
    return h


PC = {"C": 0, "C#": 1, "D": 2, "Eb": 3, "E": 4, "F": 5, "F#": 6, "G": 7, "Ab": 8, "A": 9, "Bb": 10, "B": 11}


def voicing(chord: str, low: int, high: int) -> list[int]:
    """Toutes les notes de l'accord entre deux hauteurs MIDI (incluses)."""
    pcs = {PC[n] for n in CHORDS[chord][1]}
    return [p for p in range(low, high + 1) if p % 12 in pcs]


def bass_root(chord: str) -> int:
    """Fondamentale de basse entre G1 et F#2."""
    r = note(CHORDS[chord][0] + "2")
    return r - 12 if r > note("F#2") else r


# ----------------------------------------------------------------- thèmes

THEME = [  # thème principal (8 mesures sur P1×2)
    [("D4", 2), ("A4", 1.5), ("G4", 0.5)],
    [("F4", 1), ("E4", 1), ("D4", 2)],
    [("G4", 2), ("Bb4", 1), ("A4", 1)],
    [("A4", 3), ("E4", 1)],
    [("A4", 3), ("D5", 1)],
    [("D5", 1), ("C5", 1), ("Bb4", 1), ("A4", 1)],
    [("Bb4", 2), ("G4", 1), ("A4", 1)],
    [("A4", 4)],
]
SECOND = [  # thème secondaire (4 mesures sur P3)
    [("D5", 3), ("C5", 1)],
    [("A4", 2), ("C5", 2)],
    [("Bb4", 1), ("A4", 1), ("G4", 1), ("F4", 1)],
    [("E4", 4)],
]
SECOND_END = [("C#5", 2), ("A4", 2)]
FLUTE_MOTIF = [  # sur DREAD (ré mineur / mi bémol sur pédale de ré)
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


def B(name: str, k: int = 0) -> int:
    """Mesure absolue : k-ième mesure de la section `name`."""
    return START[name] + k


# ----------------------------------------------------------------- orchestre

def compose_orchestra(h: list[str]) -> dict[str, Part]:
    """Cordes, cuivres, bois, chœur."""
    P = {
        "contrabass": Part("contrabass", GM["contrabass"], volume=108),
        "cello": Part("cello", GM["cello"], volume=104),
        "ostinato": Part("ostinato", GM["strings"], volume=96),
        "pad": Part("pad", GM["strings_slow"], volume=96),
        "violins": Part("violins", GM["strings"], volume=104),
        "horns": Part("horns", GM["horn"], volume=104),
        "horn_solo": Part("horn_solo", GM["horn"], volume=110),
        "trombones": Part("trombones", GM["trombone"], volume=106),
        "tuba": Part("tuba", GM["tuba"], volume=104),
        "trumpets": Part("trumpets", GM["trumpet"], volume=96),
        "choir": Part("choir", GM["choir"], volume=84),
        "flute": Part("flute", GM["flute"], volume=98),
        "oboe": Part("oboe", GM["oboe"], volume=96),
        "tremolo": Part("tremolo", GM["tremolo"], volume=94),
    }
    full = set(range(B("B"), B("D"))) | set(range(B("D", 16), B("E", 12)))  # sections « pleines »

    # Basses : fondamentales tenues partout, pédale de ré au début de l'ouverture.
    cb, ce = P["contrabass"], P["cello"]
    for b in range(TOTAL_BARS):
        r = note("D2") if b < 4 else bass_root(h[b])
        cb.add(b * 4, 4.05, r - 12 if r >= note("D2") else r, 84)
        ce.add(b * 4, 4.05, r + 12 if h[b] != "Dm/Eb" else note("D3"), 76)
    cb.ramp(0, 9 * 4, 55, 112)
    ce.ramp(0, 9 * 4, 45, 108)
    cb.ramp(B("D") * 4, B("D", 2) * 4, 112, 70)
    ce.ramp(B("D") * 4, B("D", 2) * 4, 108, 60)
    cb.ramp(B("D", 16) * 4, B("C2") * 4, 70, 115)
    ce.ramp(B("D", 16) * 4, B("C2") * 4, 60, 115)
    cb.ramp(B("E", 12) * 4, TOTAL_BARS * 4, 112, 60)
    ce.ramp(B("E", 12) * 4, TOTAL_BARS * 4, 108, 50)

    # Nappe de cordes (accords tenus).
    pad = P["pad"]
    for b in range(TOTAL_BARS):
        lo, hi = (note("F3"), note("A4")) if section_of(b) in ("O", "D") else (note("D3"), note("D5"))
        for p in voicing(h[b], lo, hi):
            pad.add(b * 4, 4.1, p, 74)
    pad.ramp(0, 9 * 4, 30, 100)
    pad.ramp(B("D") * 4, B("D", 2) * 4, 100, 55)
    pad.ramp(B("D", 16) * 4, B("C2") * 4, 55, 105)
    pad.ramp(B("E", 12) * 4, TOTAL_BARS * 4, 110, 45)

    # Ostinato des cordes graves (croches) : moteur des marches de conquête.
    ost = P["ostinato"]
    for b in list(range(B("C"), B("D"))) + list(range(B("D", 24), B("E"))):
        r = bass_root(h[b]) + 12
        for k, p in enumerate([r, r, r + 7, r, r + 12, r, r + 7, r]):
            ost.add(b * 4 + k * 0.5, 0.42, p, 76 + (16 if k in (0, 3, 6) else 0))
    ost.ramp(B("C") * 4, B("C", 2) * 4, 80, 110)
    ost.ramp(B("D", 24) * 4, B("C2") * 4, 60, 110)

    # Violons : thème à l'octave (B 2e fois, C, C tutti, C2 tutti, E maestoso).
    vn = P["violins"]
    for bar, vel in ((B("B", 8), 82), (B("C", 12), 90), (B("C", 36), 96), (B("C2", 16), 96), (B("E"), 100)):
        put_melody(vn, bar, THEME, 12, vel)
    put_melody(vn, B("C", 20), SECOND, 0, 76)
    put_melody(vn, B("C", 24), SECOND[:3] + [SECOND_END], 0, 78)
    vn.add(B("E", 8) * 4, 4, "D5", 100)
    vn.add(B("E", 9) * 4, 4, "D5", 104)
    vn.add(B("E", 10) * 4, 4, "C#5", 108)
    vn.add(B("E", 11) * 4, 6, "D5", 110)
    vn.ramp(B("E", 11) * 4, B("E", 13) * 4, 120, 40)

    # Cors : accords des sections pleines + thème.
    hn = P["horns"]
    for b in sorted(full):
        for p in voicing(h[b], note("A2"), note("D4"))[-3:]:
            hn.add(b * 4, 4.0, p, 80)
    hn.ramp(B("B") * 4, B("B", 2) * 4, 70, 104)
    put_melody(hn, B("C2"), THEME, 0, 104)       # C2 : thème aux cors
    put_melody(hn, B("E"), THEME, 0, 110)        # E : maestoso
    hn.chord(B("E", 8) * 4, 4, [note("Bb2"), note("F3"), note("D4")], 106)
    hn.chord(B("E", 9) * 4, 4, [note("G2"), note("D3"), note("Bb3")], 108)
    hn.chord(B("E", 10) * 4, 4, [note("A2"), note("E3"), note("C#4")], 112)
    hn.chord(B("E", 11) * 4, 8, [note("D3"), note("A3"), note("F4")], 114)
    hn.ramp(B("E", 11) * 4, B("E", 13) * 4, 124, 30)

    # Cor solo : thème dans B, appel dans C.
    hs = P["horn_solo"]
    put_melody(hs, B("B"), THEME, 0, 84)
    put_melody(hs, B("C", 4), THEME, 0, 96)
    hs.ramp(B("B") * 4, B("B", 4) * 4, 85, 110)

    # Trombones + tuba : fondamentales martelées dans les marches (puissance).
    tb, tu = P["trombones"], P["tuba"]
    for b in sorted(full):
        r = bass_root(h[b]) + 12
        if bpm_of_bar(b) == CONQUEST:
            for beat in (0, 2):
                tb.add(b * 4 + beat, 1.6, r, 96)
                tu.add(b * 4 + beat, 1.6, r - 12, 92)
        else:
            tb.add(b * 4, 3.8, r, 92)
            tu.add(b * 4, 3.8, r - 12, 88)
    for b in range(4, 9):  # l'ouverture se rassemble
        tb.add(b * 4, 3.8, bass_root(h[b]) + 12, 80)
    tb.ramp(16, 36, 40, 110)

    # Trompettes : appels de conquête (quinte et octave) aux charnières.
    tp = P["trumpets"]
    for bar in (B("C"), B("C", 36), B("C2"), B("C2", 16), B("E")):
        for beat, p, d in ((0, "D4", 0.5), (0.5, "D4", 0.5), (1, "A4", 1.0), (2, "D5", 2.0)):
            tp.add(bar * 4 + beat, d, p, 100)

    # Chœur (discret, sans paroles) dans les grands moments.
    ch = P["choir"]
    for b in list(range(B("B", 16), B("C"))) + list(range(B("C2", 16), B("C2", 32))) + list(range(B("E"), B("E", 12))):
        for p in voicing(h[b], note("D3"), note("A4"))[-3:]:
            ch.add(b * 4, 4.1, p, 70)

    # Bois : contre-chant du hautbois (B), flûte (C, D).
    fl, ob = P["flute"], P["oboe"]
    put_melody(ob, B("B", 16), SECOND, 0, 80)
    put_melody(ob, B("B", 20), SECOND[:3] + [SECOND_END], 0, 82)
    put_melody(fl, B("C", 20), SECOND, 12, 80)
    put_melody(fl, B("C", 24), SECOND[:3] + [SECOND_END], 12, 82)
    put_melody(fl, B("D", 2), FLUTE_MOTIF, 0, 72, legato=1.0)
    put_melody(fl, B("D", 8), FLUTE_MOTIF, 12, 68, legato=1.0)
    put_melody(ob, B("D", 12), FLUTE_MOTIF[:2], -12, 64, legato=1.0)

    # Trémolo : tension de la reconstruction (fin de D) et avant le retour.
    tr = P["tremolo"]
    for b in list(range(B("D", 16), B("C2"))) + list(range(B("C2", 36), B("E"))):
        for p in voicing(h[b], note("A3"), note("A5"))[-4:]:
            tr.add(b * 4, 4.05, p, 74)
    tr.ramp(B("D", 16) * 4, B("C2") * 4, 25, 120)
    return P


# ----------------------------------------------------------------- tambours

KIT_CRASH = (49, 57)  # cymbales (kit orchestre)
KIT_CHINA = 52


def compose_drums(h: list[str]) -> dict[str, Part]:
    """Tambours de puissance enregistrés : 3 pupitres de taikos (gauche,
    centre, droite, en léger décalage = un ensemble de tambourinaires),
    grosse caisse de concert, timbales, cymbales.
    """
    D = {
        "taiko_c": Part("taiko_c", GM["taiko"], volume=120, humanize_ms=6.0),
        "taiko_l": Part("taiko_l", GM["taiko"], volume=112, humanize_ms=12.0),
        "taiko_r": Part("taiko_r", GM["taiko"], volume=112, humanize_ms=12.0),
        "bass_drum": Part("bass_drum", 116, bank=8, volume=118, humanize_ms=4.0),  # Concert Bass Drum
        "timpani": Part("timpani", GM["timpani"], volume=116, humanize_ms=5.0),
        "cymbals": Part("cymbals", 48, drums=True, volume=100, humanize_ms=3.0),  # Orchestra Kit
    }
    tc, tl, trr, bd, ti, cy = (D[k] for k in ("taiko_c", "taiko_l", "taiko_r", "bass_drum", "timpani", "cymbals"))
    LOW, MID, HIGH = note("C2"), note("F2"), note("A2")  # hauteurs des taikos (grave = puissant)
    section_firsts = {START[x] for x, _, _ in SECTIONS if x != "O"}

    for b in range(TOTAL_BARS):
        s = section_of(b)
        t0 = b * 4.0
        root_timp = bass_root(h[b]) + 12 if h[b] != "Dm/Eb" else note("D3")
        if s == "O":
            # Ouverture : puissance immédiate, l'ensemble grossit mesure après mesure.
            v = 92 + b * 3
            tc.add(t0, 1, LOW, v)
            tc.add(t0 + 2, 1, LOW, v - 4)
            if b >= 2:
                bd.add(t0, 2, "D2", v)
            if b >= 4:
                tl.add(t0 + 3, 0.5, MID, v - 10)
                trr.add(t0 + 3.5, 0.5, MID, v - 8)
            if b >= 6:
                ti.add(t0, 1, root_timp, v)
            if b == 8:  # roulement vers B
                for k in range(16):
                    ti.add(t0 + k * 0.25, 0.25, note("A2"), 70 + k * 3)
        elif s == "D" and b < B("D", 24):
            # Cœur des ténèbres : battement de cœur des taikos, proche mais retenu.
            tc.add(t0, 0.5, LOW, 88)
            tc.add(t0 + 0.42, 0.5, LOW, 70)
            if (b - B("D")) % 4 == 3:
                ti.add(t0 + 2, 1, note("D2"), 60)
            if b >= B("D", 16):  # reconstruction
                k = b - B("D", 16)
                tl.add(t0 + 2, 0.5, MID, 70 + k * 4)
                trr.add(t0 + 3, 0.5, MID, 70 + k * 4)
        elif bpm_of_bar(b) == HEAVY:
            # Marche lourde : « DOOM — DOOM », reprise en levée, ensemble de trois.
            for player, lag in ((tc, 0.0), (tl, 0.02), (trr, -0.015)):
                player.add(t0 + max(0.0, lag), 1, LOW, 120 if player is tc else 108)
                player.add(t0 + 2 + lag, 1, LOW, 116 if player is tc else 104)
            tl.add(t0 + 3.5, 0.5, MID, 96)
            trr.add(t0 + 3.75, 0.25, MID, 92)
            bd.add(t0, 2, "D2", 116)
            bd.add(t0 + 2, 2, "D2", 108)
            ti.add(t0, 1, root_timp, 104)
            ti.add(t0 + 2, 1, root_timp, 96)
            if (b - START[s]) % 4 == 3:  # appel en fin de groupe de 4 mesures
                for k, beat in enumerate((2.5, 3.0, 3.25, 3.5, 3.75)):
                    (tl if k % 2 else trr).add(t0 + beat, 0.25, HIGH, 90 + k * 6)
        else:
            # Marche de conquête : les quatre temps, accents sur 1 et 3, croches en levée.
            for beat, v in ((0, 122), (1, 100), (2, 118), (3, 102)):
                tc.add(t0 + beat, 1, LOW, v)
            for beat, v in ((0.5, 84), (1.5, 88), (2.5, 84), (3.5, 96)):
                (tl if beat in (0.5, 2.5) else trr).add(t0 + beat, 0.5, MID, v)
            tl.add(t0, 1, LOW, 108)
            trr.add(t0 + 2, 1, LOW, 106)
            bd.add(t0, 1, "D2", 118)
            bd.add(t0 + 2, 1, "D2", 112)
            ti.add(t0, 1, root_timp, 108)
            ti.add(t0 + 2, 1, root_timp, 100)
            if (b - START[s]) % 8 == 7:  # roulement de taikos aigus en fin de phrase
                for k in range(8):
                    (tl if k % 2 else trr).add(t0 + 2 + k * 0.25, 0.25, HIGH, 88 + k * 4)

        # Cymbales : début de chaque phrase de 8 mesures (sections pleines) et de chaque section.
        if s != "O" and (b - START[s]) % 8 == 0 and not (s == "D" and b < B("D", 24)):
            cy.add(t0, 4, KIT_CRASH[(b // 8) % 2], 112)
        if b in section_firsts:
            cy.add(t0, 4, KIT_CHINA, 100)

    # Cadence finale : timbales.
    for k, p in enumerate(("Bb2", "G2", "A2")):
        ti.add(B("E", 8 + k) * 4, 1, p, 120)
        ti.add(B("E", 8 + k) * 4 + 2, 1, p, 110)
    ti.add(B("E", 11) * 4, 2, "D2", 127)
    # Roulements de timbales avant chaque retour de marche.
    for bar in (B("C") - 1, B("C2") - 1, B("E") - 1):
        for k in range(16):
            ti.add(bar * 4 + k * 0.25, 0.25, note("A2"), 70 + k * 3)
    return D


# ----------------------------------------------------------------- mixage

MIX = {  # pupitre : (gain dB, panoramique)
    "contrabass": (0.0, 0.0), "cello": (-1.0, -0.15), "ostinato": (-2.0, 0.2), "pad": (-3.0, 0.0),
    "violins": (6.0, -0.3), "horns": (-3.0, 0.25), "horn_solo": (4.0, -0.1), "trombones": (-1.0, 0.3),
    "tuba": (-3.0, 0.15), "trumpets": (-2.0, 0.35), "choir": (-6.0, 0.0), "flute": (2.0, -0.35),
    "oboe": (6.0, 0.35), "tremolo": (-4.0, -0.2),
    "taiko_c": (3.0, 0.0), "taiko_l": (0.0, -0.55), "taiko_r": (0.0, 0.55), "bass_drum": (1.0, 0.0),
    "timpani": (-1.0, 0.1), "cymbals": (-6.0, 0.0),
}
DRUM_PARTS = {"taiko_c", "taiko_l", "taiko_r", "bass_drum", "timpani", "cymbals"}


def stereo_width(x: np.ndarray, p: float) -> np.ndarray:
    """Place un rendu stéréo FluidSynth à un panoramique donné."""
    m = x.mean(axis=1)
    return (dsp.pan(m, p) + 0.25 * (x - m[:, None])).astype(np.float32)


def build(verbose: bool = True, orch_db: float = 8.0, drums_db: float = 5.0) -> np.ndarray:
    """Compose, rend et mixe la piste. Renvoie la boucle stéréo (LOOP_SECONDS)."""
    total = LOOP_SECONDS + TAIL_SECONDS
    n = dsp.samples(total)
    h = build_harmony()
    parts = {**compose_orchestra(h), **compose_drums(h)}
    tm = tempo_map()
    r = dsp.rng(7)
    hall = dsp.make_ir(2.6, r, predelay=0.02, damping_hz=7500.0)

    orch = np.zeros((n, 2), dtype=np.float32)
    drums = np.zeros((n, 2), dtype=np.float32)
    for name, part in parts.items():
        if verbose:
            print(f"  rendu : {name} ({len(part.notes)} notes)")
        x = render(part, tm, total)
        g, p = MIX[name]
        x = stereo_width(x, p) * dsp.from_db(g)
        if name in DRUM_PARTS:
            drums += x
        else:
            orch += x
        del x

    # Gongs (tam-tam) aux charnières : début de B, de C, de C2, cadence finale.
    for bar, g in ((B("B"), -10.0), (B("C"), -12.0), (B("C2"), -12.0), (B("E", 11), -7.0)):
        dsp.place(drums, perc.gong(bar, f0=62.0), time_of_beat(bar * 4.0), dsp.from_db(g), 0.0)

    # Vent discret dans l'ouverture, le cœur des ténèbres et la fin ; tonnerre lointain.
    tex = textures.wind(total, 31)
    t = np.arange(n, dtype=np.float32) / dsp.SR
    pts_t = [0, time_of_beat(B("B") * 4), time_of_beat(B("B", 2) * 4), time_of_beat(B("D") * 4),
             time_of_beat(B("D", 2) * 4), time_of_beat(B("D", 20) * 4), time_of_beat(B("D", 26) * 4),
             time_of_beat(B("E", 12) * 4), total]
    env = np.interp(t, pts_t, [0.5, 0.35, 0.05, 0.05, 0.5, 0.45, 0.05, 0.1, 0.5]).astype(np.float32)
    del t
    tex *= (env * dsp.from_db(-18.0))[:, None]
    del env
    for bar, seed in ((B("D", 4), 43), (B("D", 12), 44)):
        dsp.place(tex, textures.thunder(seed), time_of_beat(bar * 4.0), dsp.from_db(-12.0), 0.0)

    # Équilibre orchestre / tambours (gains de bus), tambours proches (peu de réverbération).
    mix = dsp.reverb(orch, hall, wet=0.30, dry=0.9)[:n] * dsp.from_db(orch_db)
    del orch
    mix += dsp.reverb(drums, hall, wet=0.16, dry=1.0)[:n] * dsp.from_db(drums_db)
    del drums
    mix += dsp.reverb(tex, hall, wet=0.3, dry=0.8)[:n]
    del tex
    mix = dsp.highpass(mix, 28.0, 2)
    loop = dsp.wrap_loop(mix, dsp.samples(LOOP_SECONDS))
    loop = dsp.normalize_lufs(loop, -14.5)
    return dsp.limiter(loop, -1.0)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--out", default="game/data/audio/music/menu_lune_noire.ogg")
    ap.add_argument("--wav", default=None, help="écrit aussi un WAV 32 bits (analyse)")
    a = ap.parse_args()
    print("SND.1 — composition et rendu…")
    loop = build()
    if a.wav:
        import soundfile as sf
        sf.write(a.wav, loop.astype(np.float32), dsp.SR, subtype="FLOAT")
    dsp.write_ogg(loop, Path(a.out), quality=6)
    print(f"écrit : {a.out} ({len(loop) / dsp.SR:.1f} s, {dsp.loudness_lufs(loop):.1f} LUFS)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
