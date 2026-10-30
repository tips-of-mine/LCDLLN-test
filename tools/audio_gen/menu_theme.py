"""SND.1 (v3) — « Aux portes de la Lune Noire » : musique de l'écran de connexion.

Version 3 après deux écoutes (30/10/2026) : « des tambours bien forts, bien en
termes de percussion, avec des cors, et une montée en puissance » ; le son doit
être plus grave et ne plus comporter d'instrument difficile à identifier.

  - Deux familles seulement : TAMBOURS (taikos dont un ō-daiko très grave,
    grosse caisse de concert, timbales, cymbales et gong aux sommets) et
    CUIVRES (cors pour le thème et les accords, trombones et tuba pour le grave).
    Plus de cordes, de chœur, de bois ni de trompettes.
  - Grave renforcé : taikos accordés plus bas, ō-daiko sur les temps forts,
    plancher de trombones/tuba, accentuation des basses du bus de tambours.
  - Montée en puissance : chaque section ajoute des tambourinaires et des cors ;
    le cœur des ténèbres retombe, puis la conquête et le retour vont plus haut.
  - Sons enregistrés (banque FluidR3_GM) ; seul le gong est synthétisé.

  O   0:00–0:31  Ouverture               ō-daiko seul, puis les taikos, puis les cuivres graves
  B   0:31–2:07  Le monde se dévoile     marche lourde (70), thème au cor solo puis aux cors
  C   2:07–4:15  L'appel de la conquête  marche de conquête (90), thème des cors en octaves
  D   4:15–6:05  Au cœur des ténèbres    battement de cœur, cor lointain ; reconstruction
  C2  6:05–7:52  La conquête             (90) plus dense, thème cors + trombones
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


# ----------------------------------------------------------------- cuivres

def compose_brass(h: list[str]) -> dict[str, Part]:
    """Cors (thème, accords) et cuivres graves (trombones, tuba)."""
    P = {
        "horn_solo": Part("horn_solo", GM["horn"], volume=112),
        "horns": Part("horns", GM["horn"], volume=108),
        "horns_low": Part("horns_low", GM["horn"], volume=106),
        "trombones": Part("trombones", GM["trombone"], volume=110),
        "tuba": Part("tuba", GM["tuba"], volume=112),
    }
    hs, hn, hl, tb, tu = (P[k] for k in ("horn_solo", "horns", "horns_low", "trombones", "tuba"))

    def chords(bars, vel, top=note("F4")):
        """Accords des cors (4 voix) mesure par mesure."""
        for b in bars:
            for p in voicing(h[b], note("A2"), top)[-4:]:
                hn.add(b * 4, 4.0, p, vel)

    def low_floor(bars, vel, hammer=False):
        """Plancher grave : trombones (octave 2) et tuba (octave 1) sur la fondamentale."""
        for b in bars:
            r = bass_root(h[b])
            beats = (0, 2) if hammer else (0,)
            for beat in beats:
                d = 1.8 if hammer else 3.9
                tb.add(b * 4 + beat, d, r + 12, vel)
                tu.add(b * 4 + beat, d, r, vel)

    # O — ouverture : plancher grave qui enfle, puis les cors se rassemblent.
    low_floor(range(2, 9), 92)
    tb.ramp(2 * 4, 9 * 4, 30, 112)
    tu.ramp(2 * 4, 9 * 4, 40, 118)
    chords(range(6, 9), 84)
    hn.ramp(6 * 4, 9 * 4, 40, 105)

    # B — marche lourde : thème au cor solo, puis tous les cors (avec cors graves à l'octave).
    low_floor(range(B("B"), B("C")), 96)
    put_melody(hs, B("B"), THEME, 0, 92)
    put_melody(hn, B("B", 8), THEME, 0, 100)
    put_melody(hl, B("B", 8), THEME, -12, 92)
    chords(range(B("B"), B("B", 8)), 72)
    put_melody(hs, B("B", 16), SECOND, -12, 96)
    put_melody(hs, B("B", 20), SECOND[:3] + [SECOND_END], -12, 98)
    chords(range(B("B", 16), B("C")), 84)
    hn.ramp(B("B", 24) * 4, B("C") * 4, 95, 124)

    # C — marche de conquête : plancher martelé, thème des cors en octaves.
    low_floor(range(B("C"), B("D")), 102, hammer=True)
    chords(range(B("C"), B("C", 4)), 96)
    for bar, v in ((B("C", 4), 100), (B("C", 12), 108), (B("C", 36), 116)):
        put_melody(hs, bar, THEME, 0, v)
        put_melody(hl, bar, THEME, -12, v - 4)
        chords(range(bar, bar + 8), v - 22)
    put_melody(hs, B("C", 20), SECOND, -12, 104)
    put_melody(hs, B("C", 24), SECOND[:3] + [SECOND_END], -12, 106)
    chords(range(B("C", 20), B("C", 36)), 92)
    chords(range(B("C", 44), B("D")), 110)

    # D — cœur des ténèbres : cor lointain et grave, reconstruction des cuivres.
    for b in range(B("D"), B("D", 16)):
        tu.add(b * 4, 4.0, note("D1"), 70)
    put_melody(hs, B("D", 2), FLUTE_MOTIF, -12, 74, legato=1.0)
    put_melody(hs, B("D", 8), FLUTE_MOTIF, -12, 80, legato=1.0)
    low_floor(range(B("D", 16), B("C2")), 90)
    chords(range(B("D", 16), B("C2")), 80)
    hn.ramp(B("D", 16) * 4, B("C2") * 4, 30, 122)
    tb.ramp(B("D", 16) * 4, B("C2") * 4, 30, 122)

    # C2 — la conquête : plus haut encore, thème cors + trombones (octave grave).
    low_floor(range(B("C2"), B("E")), 108, hammer=True)
    for bar, v in ((B("C2"), 112), (B("C2", 16), 120)):
        put_melody(hs, bar, THEME, 0, v)
        put_melody(hn, bar, THEME, 0, v - 6)
        put_melody(hl, bar, THEME, -12, v - 6)
        put_melody(tb, bar, THEME, -24, v - 10)
    chords(range(B("C2", 8), B("C2", 16)), 100)
    put_melody(hs, B("C2", 8), SECOND, -12, 110)
    put_melody(hs, B("C2", 12), SECOND[:3] + [SECOND_END], -12, 112)
    chords(range(B("C2", 24), B("E")), 108)

    # E — retour inéluctable : maestoso, cadence, puis tout retombe vers l'ouverture.
    low_floor(range(B("E"), B("E", 12)), 112)
    put_melody(hs, B("E"), THEME, 0, 124)
    put_melody(hn, B("E"), THEME, 0, 118)
    put_melody(hl, B("E"), THEME, -12, 116)
    for k, ch in enumerate(([note("Bb2"), note("F3"), note("Bb3"), note("D4")],
                            [note("G2"), note("D3"), note("G3"), note("Bb3")],
                            [note("A2"), note("E3"), note("A3"), note("C#4")])):
        hn.chord(B("E", 8 + k) * 4, 4, ch, 118)
    hn.chord(B("E", 11) * 4, 8, [note("D3"), note("A3"), note("D4"), note("F4")], 124)
    tu.add(B("E", 11) * 4, 8, "D1", 124)
    tb.add(B("E", 11) * 4, 8, "D2", 120)
    hn.ramp(B("E", 11) * 4, B("E", 14) * 4, 127, 20)
    tb.ramp(B("E", 11) * 4, B("E", 14) * 4, 127, 20)
    tu.ramp(B("E", 11) * 4, B("E", 14) * 4, 127, 30)
    return P


# ----------------------------------------------------------------- tambours

KIT_CRASH = (49, 57)  # cymbales frappées (kit orchestre)


def compose_drums(h: list[str]) -> dict[str, Part]:
    """Tambours de puissance enregistrés, accordés grave.

    - ō-daiko (taiko joué très bas) : le « DOOM » des temps forts ;
    - 3 pupitres de taikos (gauche, centre, droite, légèrement décalés = un
      ensemble de tambourinaires) ;
    - grosse caisse de concert, timbales sur la fondamentale ;
    - cymbales frappées uniquement aux sommets (entrées de thème).
    L'intensité (vélocités, nombre de joueurs, roulements) suit la montée en
    puissance : O < B < C ; D retombe ; D (fin) < C2 < E.
    """
    D = {
        "odaiko": Part("odaiko", GM["taiko"], volume=127, humanize_ms=3.0),
        "taiko_c": Part("taiko_c", GM["taiko"], volume=120, humanize_ms=6.0),
        "taiko_l": Part("taiko_l", GM["taiko"], volume=114, humanize_ms=12.0),
        "taiko_r": Part("taiko_r", GM["taiko"], volume=114, humanize_ms=12.0),
        "bass_drum": Part("bass_drum", 116, bank=8, volume=120, humanize_ms=4.0),  # Concert Bass Drum
        "timpani": Part("timpani", GM["timpani"], volume=118, humanize_ms=5.0),
        "cymbals": Part("cymbals", 48, drums=True, volume=96, humanize_ms=3.0),  # Orchestra Kit
    }
    od, tc, tl, trr, bd, ti, cy = (D[k] for k in ("odaiko", "taiko_c", "taiko_l", "taiko_r",
                                                 "bass_drum", "timpani", "cymbals"))
    ODAIKO, LOW, MID, HIGH = note("D1"), note("A1"), note("D2"), note("G2")
    # Gain d'intensité par section (montée en puissance), en vélocité ajoutée.
    LIFT = {"O": -10, "B": -4, "C": 0, "D": -6, "C2": 4, "E": 6}

    def v(x: int, s: str) -> int:
        return max(1, min(127, x + LIFT[s]))

    for b in range(TOTAL_BARS):
        s = section_of(b)
        t0 = b * 4.0
        root_timp = bass_root(h[b]) + 12 if h[b] != "Dm/Eb" else note("D3")
        if s == "O":
            # Ouverture : ō-daiko seul, puis l'ensemble se rassemble mesure après mesure.
            g = 96 + b * 3
            od.add(t0, 2, ODAIKO, g)
            od.add(t0 + 2, 2, ODAIKO, g - 6)
            if b >= 2:
                tc.add(t0, 1, LOW, g)
                tc.add(t0 + 2, 1, LOW, g - 6)
            if b >= 3:
                bd.add(t0, 2, "D2", g)
            if b >= 5:
                tl.add(t0 + 3, 0.5, MID, g - 8)
                trr.add(t0 + 3.5, 0.5, MID, g - 6)
            if b >= 6:
                ti.add(t0, 1, root_timp, g)
            if b == 8:  # roulement de timbales vers B
                for k in range(16):
                    ti.add(t0 + k * 0.25, 0.25, note("A2"), 74 + k * 3)
        elif s == "D" and b < B("D", 24):
            # Cœur des ténèbres : battement de cœur (ō-daiko + taiko), retenu mais présent.
            od.add(t0, 1, ODAIKO, 100)
            tc.add(t0 + 0.42, 0.5, LOW, 84)
            if (b - B("D")) % 4 == 3:
                ti.add(t0 + 2, 1, note("D2"), 70)
            if b >= B("D", 16):  # reconstruction : les tambourinaires reviennent un à un
                k = b - B("D", 16)
                tc.add(t0 + 2, 1, LOW, 80 + k * 4)
                tl.add(t0 + 3, 0.5, MID, 76 + k * 4)
                trr.add(t0 + 3.5, 0.5, MID, 76 + k * 4)
                if k >= 4:
                    bd.add(t0, 2, "D2", 90 + k * 3)
        elif bpm_of_bar(b) == HEAVY:
            # Marche lourde : « DOOM — DOOM », ensemble de trois, levée sur le 4e temps.
            od.add(t0, 2, ODAIKO, v(124, s))
            od.add(t0 + 2, 2, ODAIKO, v(116, s))
            for player, lag in ((tc, 0.0), (tl, 0.02), (trr, 0.012)):
                player.add(t0 + lag, 1, LOW, v(122 if player is tc else 110, s))
                player.add(t0 + 2 + lag, 1, LOW, v(116 if player is tc else 104, s))
            tl.add(t0 + 3.5, 0.5, MID, v(100, s))
            trr.add(t0 + 3.75, 0.25, MID, v(96, s))
            bd.add(t0, 2, "D2", v(120, s))
            bd.add(t0 + 2, 2, "D2", v(110, s))
            ti.add(t0, 1, root_timp, v(108, s))
            ti.add(t0 + 2, 1, root_timp, v(98, s))
            if (b - START[s]) % 4 == 3:  # appel en fin de groupe de 4 mesures
                for k, beat in enumerate((2.5, 3.0, 3.25, 3.5, 3.75)):
                    (tl if k % 2 else trr).add(t0 + beat, 0.25, HIGH, v(92 + k * 6, s))
        else:
            # Marche de conquête : les quatre temps, accents sur 1 et 3, croches en levée.
            od.add(t0, 2, ODAIKO, v(124, s))
            od.add(t0 + 2, 2, ODAIKO, v(118, s))
            for beat, x in ((0, 122), (1, 102), (2, 118), (3, 104)):
                tc.add(t0 + beat, 1, LOW, v(x, s))
            for beat, x in ((0.5, 88), (1.5, 92), (2.5, 88), (3.5, 100)):
                (tl if beat in (0.5, 2.5) else trr).add(t0 + beat, 0.5, MID, v(x, s))
            tl.add(t0, 1, LOW, v(110, s))
            trr.add(t0 + 2, 1, LOW, v(108, s))
            bd.add(t0, 1, "D2", v(120, s))
            bd.add(t0 + 2, 1, "D2", v(114, s))
            ti.add(t0, 1, root_timp, v(110, s))
            ti.add(t0 + 2, 1, root_timp, v(102, s))
            # Roulements de taikos en fin de phrase (plus fréquents dans C2).
            if (b - START[s]) % (4 if s == "C2" else 8) == 3 + (0 if s == "C2" else 4):
                for k in range(8):
                    (tl if k % 2 else trr).add(t0 + 2 + k * 0.25, 0.25, HIGH, v(90 + k * 4, s))

    # Cymbales : seulement aux sommets (entrées du thème, sections).
    for bar in (B("B"), B("B", 8), B("C"), B("C", 12), B("C", 36), B("C2"), B("C2", 16), B("E")):
        cy.add(bar * 4.0, 4, KIT_CRASH[bar % 2], 110)
        cy.add(bar * 4.0, 4, KIT_CRASH[(bar + 1) % 2], 96)
    cy.add(B("E", 11) * 4.0, 4, KIT_CRASH[0], 120)

    # Cadence finale : timbales, ō-daiko.
    for k, p in enumerate(("Bb2", "G2", "A2")):
        ti.add(B("E", 8 + k) * 4, 1, p, 124)
        ti.add(B("E", 8 + k) * 4 + 2, 1, p, 114)
    ti.add(B("E", 11) * 4, 2, "D2", 127)
    od.add(B("E", 11) * 4, 4, ODAIKO, 127)
    # Roulements de timbales avant chaque retour de marche.
    for bar in (B("C") - 1, B("C2") - 1, B("E") - 1):
        for k in range(16):
            ti.add(bar * 4 + k * 0.25, 0.25, note("A2"), 74 + k * 3)
    return D


# ----------------------------------------------------------------- mixage

MIX = {  # pupitre : (gain dB, panoramique)
    "horn_solo": (2.0, -0.1), "horns": (-2.0, 0.25), "horns_low": (-1.0, -0.2),
    "trombones": (0.0, 0.3), "tuba": (1.0, 0.0),
    "odaiko": (3.0, 0.0), "taiko_c": (2.0, 0.0), "taiko_l": (0.0, -0.55), "taiko_r": (0.0, 0.55),
    "bass_drum": (2.0, 0.0), "timpani": (-1.0, 0.1), "cymbals": (-8.0, 0.0),
}
DRUM_PARTS = {"odaiko", "taiko_c", "taiko_l", "taiko_r", "bass_drum", "timpani", "cymbals"}


def low_shelf(x: np.ndarray, fc: float, gain_db: float) -> np.ndarray:
    """Renforce le grave sous `fc` Hz de `gain_db` dB (ajout de la bande filtrée)."""
    return x + dsp.lowpass(x, fc, 2) * (dsp.from_db(gain_db) - 1.0)


def stereo_width(x: np.ndarray, p: float) -> np.ndarray:
    """Place un rendu stéréo FluidSynth à un panoramique donné."""
    m = x.mean(axis=1)
    return (dsp.pan(m, p) + 0.25 * (x - m[:, None])).astype(np.float32)


def build(verbose: bool = True, orch_db: float = 8.0, drums_db: float = 4.0) -> np.ndarray:
    """Compose, rend et mixe la piste. Renvoie la boucle stéréo (LOOP_SECONDS)."""
    total = LOOP_SECONDS + TAIL_SECONDS
    n = dsp.samples(total)
    h = build_harmony()
    parts = {**compose_brass(h), **compose_drums(h)}
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

    # Équilibre cuivres / tambours (gains de bus), tambours proches (peu de réverbération).
    mix = dsp.reverb(orch, hall, wet=0.30, dry=0.9)[:n] * dsp.from_db(orch_db)
    del orch
    drums = low_shelf(drums, 110.0, 5.0)  # tambours plus graves (corps de l'ō-daiko)
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
