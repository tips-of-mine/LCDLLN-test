"""Orchestre : écriture de partitions en MIDI et rendu par FluidSynth.

Chaque pupitre (Part) est rendu séparément (« stem ») sans réverbération ni
chorus internes : le mixage, la spatialisation et la réverbération sont faits
ensuite en numpy (dsp.py), pour garder la main sur l'équilibre.

Banque de sons : FluidR3_GM.sf2 (licence MIT), chemin surchargeable par la
variable d'environnement LCDLLN_SF2.
"""

from __future__ import annotations

import os
import subprocess
import tempfile
from dataclasses import dataclass, field
from pathlib import Path

import mido
import numpy as np

from .dsp import SR, rng

DEFAULT_SF2 = "/usr/share/sounds/sf2/FluidR3_GM.sf2"
TPB = 480  # ticks par noire

# Programmes General MIDI utilisés (numérotation 0..127).
GM = {
    "violin": 40, "viola": 41, "cello": 42, "contrabass": 43, "tremolo": 44, "pizzicato": 45,
    "harp": 46, "timpani": 47, "strings": 48, "strings_slow": 49, "choir": 52, "voice_oohs": 53,
    "trumpet": 56, "trombone": 57, "tuba": 58, "horn": 60, "brass": 61,
    "oboe": 68, "english_horn": 69, "bassoon": 70, "clarinet": 71, "flute": 73, "recorder": 74,
    "pan_flute": 75, "pad_warm": 89, "pad_choir": 91, "pad_bowed": 92, "taiko": 116,
}

NOTE_NAMES = {"C": 0, "D": 2, "E": 4, "F": 5, "G": 7, "A": 9, "B": 11}


def note(name: str) -> int:
    """Nom de note → numéro MIDI. Ex. « D4 » = 62, « Bb3 » = 58, « F#2 »."""
    letter = name[0].upper()
    rest = name[1:]
    acc = 0
    while rest and rest[0] in "#b":
        acc += 1 if rest[0] == "#" else -1
        rest = rest[1:]
    octave = int(rest)
    return 12 * (octave + 1) + NOTE_NAMES[letter] + acc


@dataclass
class Part:
    """Un pupitre : programme GM, notes (temps en noires) et courbe d'expression."""

    name: str
    program: int
    notes: list = field(default_factory=list)   # (début, durée, hauteur, vélocité)
    expr: list = field(default_factory=list)    # (temps, valeur CC11 0..127)
    volume: int = 100                           # CC7
    bank: int = 0                               # CC0 (banque SF2 : 8 = variantes, 128 = batteries)
    drums: bool = False                         # True : canal 10 (kits de percussions)
    humanize_ms: float = 8.0                    # jeu humain : décalage aléatoire max (ms)

    def add(self, start: float, dur: float, pitch, vel: int = 80) -> None:
        """Ajoute une note (hauteur MIDI ou nom « D4 ») ; durée en noires."""
        p = note(pitch) if isinstance(pitch, str) else int(pitch)
        self.notes.append((float(start), float(dur), p, int(max(1, min(127, vel)))))

    def chord(self, start: float, dur: float, pitches, vel: int = 80) -> None:
        for p in pitches:
            self.add(start, dur, p, vel)

    def ramp(self, start: float, end: float, v0: float, v1: float, step: float = 0.25) -> None:
        """Rampe d'expression CC11 linéaire de v0 à v1 entre deux instants (noires)."""
        t = start
        while t <= end + 1e-9:
            k = 0.0 if end <= start else (t - start) / (end - start)
            self.expr.append((t, int(round(v0 + (v1 - v0) * k))))
            t += step


def _tempo_map(bpm) -> list[tuple[float, float]]:
    """Normalise un tempo fixe ou une liste [(temps en noires, bpm), …]."""
    if isinstance(bpm, (int, float)):
        return [(0.0, float(bpm))]
    tm = sorted((float(b), float(v)) for b, v in bpm)
    if not tm or tm[0][0] > 0.0:
        tm.insert(0, (0.0, tm[0][1] if tm else 120.0))
    return tm


def write_midi(part: Part, bpm, path: Path, humanize_seed: int | None = 1) -> None:
    """Écrit le pupitre dans un fichier MIDI (piste unique).

    `bpm` : tempo fixe ou carte de tempo [(temps en noires, bpm), …].
    Humanisation légère (±`humanize_ms`, ±5 de vélocité) pour éviter la
    raideur mécanique, sauf si `humanize_seed` est None.
    """
    r = rng(humanize_seed) if humanize_seed is not None else None
    ch = 9 if part.drums else 0
    tm = _tempo_map(bpm)
    mid = mido.MidiFile(ticks_per_beat=TPB)
    tr = mido.MidiTrack()
    mid.tracks.append(tr)
    events = []  # (tick, ordre, message)
    for beat, b in tm:
        events.append((int(beat * TPB), 0, mido.MetaMessage("set_tempo", tempo=mido.bpm2tempo(b))))
    if not part.drums:  # canal 10 : FluidSynth choisit lui-même la banque des kits
        events.append((0, 1, mido.Message("control_change", control=0, value=min(127, part.bank), channel=ch)))
    events.append((0, 1, mido.Message("program_change", program=part.program, channel=ch)))
    for cc, val in ((7, part.volume), (91, 0), (93, 0), (10, 64), (11, 127 if not part.expr else part.expr[0][1])):
        events.append((0, 2, mido.Message("control_change", control=cc, value=val, channel=ch)))
    ticks_per_ms = TPB * tm[0][1] / 60000.0
    for (beat, val) in part.expr:
        events.append((int(beat * TPB), 3, mido.Message("control_change", control=11, value=max(0, min(127, val)), channel=ch)))
    hz = part.humanize_ms
    for (start, dur, pitch, vel) in part.notes:
        jt = int(r.uniform(-hz, hz) * ticks_per_ms) if r is not None else 0
        jv = int(r.integers(-5, 6)) if r is not None else 0
        on = max(0, int(start * TPB) + jt)
        off = max(on + 1, int((start + dur) * TPB) + jt)
        events.append((on, 5, mido.Message("note_on", note=pitch, velocity=max(1, min(127, vel + jv)), channel=ch)))
        events.append((off, 4, mido.Message("note_off", note=pitch, velocity=0, channel=ch)))
    events.sort(key=lambda e: (e[0], e[1]))
    last = 0
    for tick, _, msg in events:
        msg.time = tick - last
        tr.append(msg)
        last = tick
    mid.save(path)


def render(part: Part, bpm, total_seconds: float, sf2: str | None = None, gain: float = 0.5) -> np.ndarray:
    """Rend le pupitre en audio stéréo (n, 2) float32 à SR, longueur fixée.

    Effet de bord : fichiers temporaires (MIDI, WAV) supprimés à la fin.
    """
    import soundfile as sf
    sf2 = sf2 or os.environ.get("LCDLLN_SF2", DEFAULT_SF2)
    with tempfile.TemporaryDirectory() as d:
        mid = Path(d) / "part.mid"
        wav = Path(d) / "part.wav"
        write_midi(part, bpm, mid)
        subprocess.run(["fluidsynth", "-ni", "-q", "-R", "0", "-C", "0", "-g", str(gain), "-r", str(SR),
                        "-O", "float", "-T", "wav", "-F", str(wav), sf2, str(mid)],
                       check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        x, sr = sf.read(wav, dtype="float32", always_2d=True)
    assert sr == SR, f"fluidsynth a rendu à {sr} Hz"
    n = int(round(total_seconds * SR))
    out = np.zeros((n, 2), dtype=np.float32)
    m = min(n, len(x))
    out[:m] = x[:m]
    return out
