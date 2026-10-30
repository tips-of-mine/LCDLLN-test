"""SND.1 (v4) — « Aux portes de la Lune Noire » : musique de l'écran de connexion.

Historique des écoutes (30/10/2026) :
  v1  trop « batterie », trop électronique, ouverture trop longue, tambours lointains ;
  v2  mieux, mais pas assez grave, un instrument non identifiable (cordes rapides) ;
  v3  mieux : tambours graves + cuivres ; mais accélération trop tardive, plus de
      cor (préférer l'hélicon, plus grave), et le rythme ne sonne pas comme une
      armée qui avance.

Version 4 :
  - deux familles seulement : TAMBOURS (ō-daiko, taikos, grosse caisse de
    concert, timbales ; cymbales et gong aux sommets) et HÉLICONS (le tuba
    General MIDI, la sonorité la plus proche : thème, harmonie, basse de marche) ;
  - marche militaire : un pas sur chaque temps (gauche–droite, appuis sur 1 et 3),
    cadence de tambours de troupe en boucle de 2 mesures, basse « oum-pa » des
    hélicons ;
  - tempo : départ lent (70) puis accélération dès la 4e mesure jusqu'au pas de
    marche (96) atteint vers 0:50 ; sommet à 100 ; ralentissement final vers 70
    pour boucler sur l'ouverture.

  O   Ouverture            ō-daiko seul, puis les pas de l'armée, accélération
  B   La troupe se met en marche   thème aux hélicons
  C   L'appel de la conquête       marche (96), cadence complète
  D   Au cœur des ténèbres         la marche continue, assourdie ; hélicon grave
  C2  La conquête                  sommet (98–100), tutti des hélicons
  E   Le retour inéluctable        maestoso, cadence, gong, ralentissement → ouverture

Ré mineur. Les tambours ne s'arrêtent jamais ; les queues de réverbération
sont repliées sur le début (dsp.wrap_loop) : la boucle est inaudible.

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

# Sections : (nom, nombre de mesures).
SECTIONS = [("O", 8), ("B", 24), ("C", 48), ("D", 36), ("C2", 64), ("E", 24)]
TOTAL_BARS = sum(c for _, c in SECTIONS)  # 204
TAIL_SECONDS = 12.0


def section_starts() -> dict[str, int]:
    """Première mesure de chaque section."""
    out, b = {}, 0
    for name, count in SECTIONS:
        out[name] = b
        b += count
    return out


START = section_starts()


def B(name: str, k: int = 0) -> int:
    """Mesure absolue : k-ième mesure de la section `name`."""
    return START[name] + k


def section_of(bar: int) -> str:
    for name, count in SECTIONS:
        if START[name] <= bar < START[name] + count:
            return name
    return SECTIONS[-1][0]


# ----------------------------------------------------------------- tempo

# Points de tempo (mesure, noires/min), interpolés linéairement.
TEMPO_POINTS = [
    (0, 70.0), (4, 70.0),          # départ lent : l'armée s'éveille
    (16, 96.0),                    # accélération : pas de marche atteint (≈ 0:50)
    (B("C2"), 96.0), (B("C2", 16), 100.0),  # sommet
    (B("E", 16), 100.0), (TOTAL_BARS, 70.0),  # ralentissement final → ouverture
]


def bpm_at_beat(beat: float) -> float:
    """Tempo au temps `beat` (noires depuis le début)."""
    bars = [p[0] * 4.0 for p in TEMPO_POINTS]
    vals = [p[1] for p in TEMPO_POINTS]
    return float(np.interp(beat, bars, vals))


def tempo_map() -> list[tuple[float, float]]:
    """Carte de tempo MIDI : un changement par temps (accélérations progressives)."""
    return [(float(k), bpm_at_beat(k + 0.5)) for k in range(TOTAL_BARS * 4)]


_BEAT_TIMES = np.concatenate([[0.0], np.cumsum([60.0 / bpm_at_beat(k + 0.5) for k in range(TOTAL_BARS * 4)])])


def time_of_beat(beat: float) -> float:
    """Instant (s) d'un temps global (en noires), selon la carte de tempo."""
    k = int(np.floor(beat))
    if k >= TOTAL_BARS * 4:
        return float(_BEAT_TIMES[-1] + (beat - TOTAL_BARS * 4) * 60.0 / TEMPO_POINTS[-1][1])
    return float(_BEAT_TIMES[k] + (beat - k) * 60.0 / bpm_at_beat(k + 0.5))


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
OPENING = ["Dm"] * 4 + ["Bb", "Gm", "A", "A"]


def build_harmony() -> list[str]:
    """Accord de chaque mesure (204 entrées)."""
    h: list[str] = []
    h += OPENING                                                         # O (8)
    h += P1 * 2 + P1 * 2 + P3 + TURN                                     # B (24)
    h += P1 * 2 + P1 * 2 + P3 * 2 + P2 * 2 + P1 * 2 + P1 + TURN          # C (48)
    h += DREAD * 4 + P2 * 2 + P1 * 2 + TURN                              # D (36)
    h += P1 * 2 + P3 * 2 + P1 * 2 + P2 * 2 + P1 * 2 + P3 * 2 + P1 * 2 + P1 + TURN  # C2 (64)
    h += P1 * 2 + ["Bb", "Gm", "A", "Dm"] + ["Dm"] * 4 + OPENING        # E (24)
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


def fifth(chord: str) -> int:
    """Quinte de l'accord, sous la fondamentale de basse (basse « oum-pa »)."""
    return bass_root(chord) - 5


# ----------------------------------------------------------------- hélicons

def compose_helicons(h: list[str]) -> dict[str, Part]:
    """Hélicon (tuba GM) en simple ACCOMPAGNATEUR : basse de marche « oum-pa »
    (fondamentale sur 1, quinte grave sur 3). Pas de thème ni d'harmonie : les
    tambours sont le premier plan (retour d'écoute v4 : trop de cuivre, saturation).
    """
    bass = Part("helicon_bass", GM["tuba"], volume=104)
    LIFT = {"O": -14, "B": -8, "C": -4, "D": -16, "C2": 0, "E": 0}

    def v(x: int, b: int) -> int:
        return max(1, min(127, x + LIFT[section_of(b)]))

    for b in range(TOTAL_BARS):
        s = section_of(b)
        r = bass_root(h[b])
        if (s == "O" and b < 4) or (s == "D" and b < B("D", 16)):
            bass.add(b * 4, 4.0, note("D1"), v(96, b))  # pédale tenue (ouverture lente, ténèbres)
        else:
            bass.add(b * 4, 1.4, r, v(104, b))
            bass.add(b * 4 + 2, 1.4, fifth(h[b]) if h[b] != "Dm/Eb" else r, v(94, b))
    bass.add(B("E", 11) * 4, 6, note("D1"), 112)  # cadence finale
    return {"helicon_bass": bass}


# ----------------------------------------------------------------- tambours

KIT_CRASH = (49, 57)  # cymbales frappées (kit orchestre)

# Cadence de troupe sur 2 mesures (croches) : 1 = frappe, 2 = accent.
CADENCE = [
    [2, 0, 1, 1, 2, 0, 1, 0],
    [2, 1, 1, 0, 2, 0, 2, 2],
]


def compose_drums(h: list[str]) -> dict[str, Part]:
    """Tambours de marche enregistrés, accordés grave.

    - ō-daiko : appuis sur 1 et 3 (le « DOOM » des pas lourds) ;
    - taiko central : un pas par temps (gauche–droite), l'armée qui avance ;
    - taikos gauche/droite : cadence de troupe (boucle de 2 mesures) ;
    - grosse caisse de concert avec l'ō-daiko, timbales sur la fondamentale ;
    - cymbales aux sommets seulement.
    """
    D = {
        "odaiko": Part("odaiko", GM["taiko"], volume=127, humanize_ms=3.0),
        "steps": Part("steps", GM["taiko"], volume=120, humanize_ms=4.0),
        "cadence_l": Part("cadence_l", GM["taiko"], volume=112, humanize_ms=6.0),
        "cadence_r": Part("cadence_r", GM["taiko"], volume=112, humanize_ms=6.0),
        "bass_drum": Part("bass_drum", 116, bank=8, volume=120, humanize_ms=3.0),  # Concert Bass Drum
        "timpani": Part("timpani", GM["timpani"], volume=118, humanize_ms=4.0),
        "cymbals": Part("cymbals", 48, drums=True, volume=96, humanize_ms=3.0),  # Orchestra Kit
        # Tambour aigu et tendu (rôle du shime-daiko) : le rythme soutenu, en continu.
        "drive": Part("drive", GM["taiko"], volume=116, humanize_ms=5.0),
    }
    od, st, cl, cr, bd, ti, cy, dr = (D[k] for k in ("odaiko", "steps", "cadence_l", "cadence_r",
                                                     "bass_drum", "timpani", "cymbals", "drive"))
    DRIVE_HI, DRIVE_LO = note("D3"), note("A2")
    ODAIKO, STEP, MID, HIGH = note("D1"), note("A1"), note("D2"), note("G2")
    LIFT = {"O": -8, "B": -4, "C": 0, "D": -18, "C2": 4, "E": 6}

    def v(x: int, s: str) -> int:
        return max(1, min(127, x + LIFT[s]))

    for b in range(TOTAL_BARS):
        s = section_of(b)
        t0 = b * 4.0
        root_timp = bass_root(h[b]) + 12 if h[b] != "Dm/Eb" else note("D3")
        opening = s == "O" or b >= B("E", 16)  # ouverture et sa reprise finale
        k_open = b if s == "O" else b - B("E", 16)

        # Appuis lourds : ō-daiko + grosse caisse sur 1 et 3.
        od.add(t0, 2, ODAIKO, v(124, s) if not opening else 100 + k_open * 3)
        od.add(t0 + 2, 2, ODAIKO, v(116, s) if not opening else 94 + k_open * 3)
        if not opening or k_open >= 2:
            bd.add(t0, 2, "D2", v(118, s))
            bd.add(t0 + 2, 2, "D2", v(108, s))

        # Les pas : un par temps, appui sur 1 et 3 (gauche), plus léger sur 2 et 4 (droite).
        if not opening or k_open >= 2:
            for beat, x in ((0, 120), (1, 104), (2, 116), (3, 104)):
                st.add(t0 + beat, 1, STEP, v(x, s) if not opening else 84 + k_open * 4 + (8 if beat % 2 == 0 else 0))

        # Cadence de troupe (boucle de 2 mesures), absente de l'ouverture lente.
        if not opening or k_open >= 4:
            pat = CADENCE[b % 2]
            for k, hit in enumerate(pat):
                if hit:
                    vel = v(104 if hit == 2 else 86, s) if not opening else 70 + k_open * 4
                    (cl if k % 2 == 0 else cr).add(t0 + k * 0.5, 0.5, MID if hit == 2 else HIGH, vel)

        # Timbales : fondamentale sur 1 (sur 1 et 3 aux sommets).
        if not opening or k_open >= 4:
            ti.add(t0, 1, root_timp, v(106, s))
            if s in ("C2", "E") and not opening:
                ti.add(t0 + 2, 1, root_timp, v(98, s))

        # Roulements de cadence en fin de phrase de 8 mesures (plus fréquents au sommet).
        phrase = 4 if s == "C2" else 8
        if not opening and (b - START[s]) % phrase == phrase - 1 and s != "D":
            for k in range(8):
                (cl if k % 2 else cr).add(t0 + 2 + k * 0.25, 0.25, HIGH, v(92 + k * 4, s))

    # Tambour soutenu : croches continues, accents 3-3-2 (élan de la marche), doubles
    # croches en levée dans les sections pleines ; présent dès la fin de l'ouverture.
    ACCENTS = (0, 3, 6)
    for b in range(TOTAL_BARS):
        s = section_of(b)
        opening = s == "O" or b >= B("E", 16)
        k_open = b if s == "O" else b - B("E", 16)
        if opening and k_open < 4:
            continue
        t0 = b * 4.0
        base = {"O": 70, "B": 84, "C": 92, "D": 66, "C2": 98, "E": 98}[s]
        if opening:
            base = 66 + (k_open - 4) * 6
        for k in range(8):
            accent = k in ACCENTS
            dr.add(t0 + k * 0.5, 0.25, DRIVE_LO if accent else DRIVE_HI, min(127, base + (22 if accent else 0)))
        if s in ("C", "C2", "E") and not opening:
            # Doubles croches en levée sur le 4e temps (une mesure sur deux, chaque mesure au sommet).
            if s == "C2" or b % 2 == 1:
                for k in range(4):
                    dr.add(t0 + 3.0 + k * 0.25, 0.2, DRIVE_HI, min(127, base + k * 6))

    # Cymbales : entrées du thème et sections.
    for bar in (B("B"), B("C"), B("C", 32), B("C2"), B("C2", 16), B("C2", 32), B("C2", 48), B("E")):
        cy.add(bar * 4.0, 4, KIT_CRASH[bar % 2], 110)
        cy.add(bar * 4.0, 4, KIT_CRASH[(bar + 1) % 2], 96)
    cy.add(B("E", 11) * 4.0, 4, KIT_CRASH[0], 122)

    # Cadence finale : timbales et ō-daiko.
    for k, p in enumerate(("Bb2", "G2", "A2")):
        ti.add(B("E", 8 + k) * 4, 1, p, 124)
        ti.add(B("E", 8 + k) * 4 + 2, 1, p, 114)
    ti.add(B("E", 11) * 4, 2, "D2", 127)
    od.add(B("E", 11) * 4, 4, ODAIKO, 127)
    # Roulements de timbales avant les grandes entrées.
    for bar in (B("C") - 1, B("C2") - 1, B("E") - 1):
        for k in range(16):
            ti.add(bar * 4 + k * 0.25, 0.25, note("A2"), 74 + k * 3)
    return D


# ----------------------------------------------------------------- mixage

MIX = {  # pupitre : (gain dB, panoramique)
    "helicon_bass": (-4.0, 0.0), "drive": (-2.0, 0.25),
    "odaiko": (3.0, 0.0), "steps": (1.0, 0.0), "cadence_l": (-1.0, -0.55), "cadence_r": (-1.0, 0.55),
    "bass_drum": (2.0, 0.0), "timpani": (-1.0, 0.1), "cymbals": (-8.0, 0.0),
}
DRUM_PARTS = {"odaiko", "steps", "cadence_l", "cadence_r", "bass_drum", "timpani", "cymbals", "drive"}


def low_shelf(x: np.ndarray, fc: float, gain_db: float) -> np.ndarray:
    """Renforce le grave sous `fc` Hz de `gain_db` dB (ajout de la bande filtrée)."""
    return x + dsp.lowpass(x, fc, 2) * (dsp.from_db(gain_db) - 1.0)


def stereo_width(x: np.ndarray, p: float) -> np.ndarray:
    """Place un rendu stéréo FluidSynth à un panoramique donné."""
    m = x.mean(axis=1)
    return (dsp.pan(m, p) + 0.25 * (x - m[:, None])).astype(np.float32)


def build(verbose: bool = True, brass_db: float = 8.0, drums_db: float = 6.0, stats: dict | None = None) -> np.ndarray:
    """Compose, rend et mixe la piste. Renvoie la boucle stéréo (LOOP_SECONDS)."""
    total = LOOP_SECONDS + TAIL_SECONDS
    n = dsp.samples(total)
    h = build_harmony()
    parts = {**compose_helicons(h), **compose_drums(h)}
    tm = tempo_map()
    r = dsp.rng(7)
    hall = dsp.make_ir(2.4, r, predelay=0.02, damping_hz=7000.0)

    brass = np.zeros((n, 2), dtype=np.float32)
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
            brass += x
        del x

    # Gongs (tam-tam) aux charnières : début de C, de C2, cadence finale.
    for bar, g in ((B("C"), -12.0), (B("C2"), -12.0), (B("E", 11), -7.0)):
        dsp.place(drums, perc.gong(bar, f0=62.0), time_of_beat(bar * 4.0), dsp.from_db(g), 0.0)

    # Vent discret dans l'ouverture, le cœur des ténèbres et la fin.
    tex = textures.wind(total, 31)
    t = np.arange(n, dtype=np.float32) / dsp.SR
    pts_t = [0, time_of_beat(B("B") * 4), time_of_beat(B("B", 2) * 4), time_of_beat(B("D") * 4),
             time_of_beat(B("D", 2) * 4), time_of_beat(B("D", 14) * 4), time_of_beat(B("D", 18) * 4),
             time_of_beat(B("E", 14) * 4), total]
    env = np.interp(t, pts_t, [0.5, 0.35, 0.05, 0.05, 0.45, 0.4, 0.05, 0.05, 0.5]).astype(np.float32)
    del t
    tex *= (env * dsp.from_db(-18.0))[:, None]
    del env

    mix = dsp.reverb(brass, hall, wet=0.28, dry=0.9)[:n] * dsp.from_db(brass_db)
    del brass
    drums = low_shelf(drums, 110.0, 5.0)  # tambours plus graves (corps de l'ō-daiko)
    # Attaque des peaux plus nette (le « claquement » des frappes, 1,5–5 kHz).
    drums = drums + dsp.bandpass(drums, 1500.0, 5000.0) * (dsp.from_db(4.0) - 1.0)
    mix += dsp.reverb(drums, hall, wet=0.15, dry=1.0)[:n] * dsp.from_db(drums_db)
    del drums
    mix += dsp.reverb(tex, hall, wet=0.3, dry=0.8)[:n]
    del tex
    mix = dsp.highpass(mix, 25.0, 2)
    loop = dsp.wrap_loop(mix, dsp.samples(LOOP_SECONDS))
    loop = dsp.normalize_lufs(loop, -15.0)
    if stats is not None:  # part du temps où le limiteur réduit de plus de 2 dB (saturation perçue)
        pk = np.max(np.abs(loop), axis=1)
        stats["over_2db"] = float(np.mean(pk > dsp.from_db(-1.0 + 2.0)))
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
