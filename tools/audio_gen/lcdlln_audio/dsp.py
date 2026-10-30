"""Briques DSP communes du générateur audio LCDLLN (tools/audio_gen).

Tout est en float32/float64 numpy, stéréo = tableau (n, 2). Fréquence
d'échantillonnage unique : SR (48 kHz, celle du moteur miniaudio).
"""

from __future__ import annotations

import math
import subprocess
from pathlib import Path

import numpy as np
from scipy import signal

SR = 48000


# ----------------------------------------------------------------- utilitaires

def seconds(n: int) -> float:
    """Durée (s) de n échantillons."""
    return n / SR


def samples(t: float) -> int:
    """Nombre d'échantillons pour t secondes."""
    return int(round(t * SR))


def db(x: float) -> float:
    """Gain linéaire -> dB (borne basse -120 dB)."""
    return 20.0 * math.log10(max(x, 1e-6))


def from_db(d: float) -> float:
    """dB -> gain linéaire."""
    return 10.0 ** (d / 20.0)


def rng(seed: int) -> np.random.Generator:
    """Générateur aléatoire déterministe (même graine = même son)."""
    return np.random.default_rng(seed)


# ----------------------------------------------------------------- filtres

def _sos(kind: str, freq, order: int = 2):
    nyq = SR / 2.0
    if isinstance(freq, (tuple, list)):
        wn = [min(max(f / nyq, 1e-5), 0.999) for f in freq]
    else:
        wn = min(max(freq / nyq, 1e-5), 0.999)
    return signal.butter(order, wn, btype=kind, output="sos")


def _filt(kind: str, x: np.ndarray, freq, order: int) -> np.ndarray:
    sos = _sos(kind, freq, order).astype(np.float32 if x.dtype == np.float32 else np.float64)
    return signal.sosfilt(sos, x, axis=0).astype(x.dtype if x.dtype == np.float32 else np.float64, copy=False)


def lowpass(x: np.ndarray, fc: float, order: int = 2) -> np.ndarray:
    """Passe-bas Butterworth (mono ou stéréo, axe 0 = temps ; garde le float32)."""
    return _filt("lowpass", x, fc, order)


def highpass(x: np.ndarray, fc: float, order: int = 2) -> np.ndarray:
    """Passe-haut Butterworth."""
    return _filt("highpass", x, fc, order)


def bandpass(x: np.ndarray, lo: float, hi: float, order: int = 2) -> np.ndarray:
    """Passe-bande Butterworth [lo, hi] Hz."""
    return _filt("bandpass", x, (lo, hi), order)


# ----------------------------------------------------------------- bruits

def white(n: int, r: np.random.Generator) -> np.ndarray:
    return r.standard_normal(n)


def pink(n: int, r: np.random.Generator) -> np.ndarray:
    """Bruit rose (pente -3 dB/octave) par filtre IIR de P. Kellet (mémoire bornée)."""
    b = [0.049922035, -0.095993537, 0.050612699, -0.004408786]
    a = [1.0, -2.494956002, 2.017265875, -0.522189400]
    out = signal.lfilter(b, a, r.standard_normal(n, dtype=np.float32)).astype(np.float32)
    return out / (np.max(np.abs(out)) + 1e-9)


def brown(n: int, r: np.random.Generator) -> np.ndarray:
    """Bruit brun (pente -6 dB/octave), centré."""
    out = np.cumsum(r.standard_normal(n, dtype=np.float32), dtype=np.float32)
    out = highpass(out, 15.0)
    return out / (np.max(np.abs(out)) + 1e-9)


# ----------------------------------------------------------------- spatial

def pan(mono: np.ndarray, p: float) -> np.ndarray:
    """Panoramique à puissance constante. p ∈ [-1 (gauche), +1 (droite)]."""
    a = (p + 1.0) * math.pi / 4.0
    return np.stack([mono * math.cos(a), mono * math.sin(a)], axis=1)


def to_stereo(x: np.ndarray) -> np.ndarray:
    return x if x.ndim == 2 else np.stack([x, x], axis=1)


def place(buf: np.ndarray, snd: np.ndarray, t: float, gain: float = 1.0, p: float = 0.0) -> None:
    """Ajoute `snd` (mono -> panoramiqué, ou stéréo) dans `buf` à t secondes.

    Effet de bord : modifie buf en place ; ce qui dépasse la fin est perdu.
    """
    s = pan(snd, p) if snd.ndim == 1 else snd
    i = samples(t)
    if i >= len(buf) or i < 0:
        return
    j = min(len(buf), i + len(s))
    buf[i:j] += s[: j - i] * gain


def make_ir(t60: float, r: np.random.Generator, predelay: float = 0.02,
            damping_hz: float = 6000.0, length: float | None = None) -> np.ndarray:
    """Réponse impulsionnelle synthétique de salle (stéréo décorrélée).

    `t60` temps de décroissance à -60 dB (s) ; `damping_hz` : coupure
    passe-bas appliquée progressivement (les aigus s'éteignent plus vite).
    """
    length = length or t60 * 1.2
    n = samples(length)
    t = np.arange(n) / SR
    env = np.exp(-6.9 * t / t60)
    out = np.zeros((n + samples(predelay), 2))
    for ch in range(2):
        noise = r.standard_normal(n)
        bright = noise * env
        dark = lowpass(noise, damping_hz * 0.25, 1) * env
        mix = t / length
        ir = bright * (1.0 - mix) ** 2 + dark * (1.0 - (1.0 - mix) ** 2)
        ir = lowpass(ir, damping_hz, 1)
        out[samples(predelay):, ch] = ir
    out /= np.sqrt(np.sum(out ** 2) / 2.0) + 1e-9
    return out


def reverb(x: np.ndarray, ir: np.ndarray, wet: float, dry: float = 1.0) -> np.ndarray:
    """Convolution stéréo. La sortie est plus longue que l'entrée (queue)."""
    xs = to_stereo(x)
    n = len(xs) + len(ir) - 1
    out = np.zeros((n, 2), dtype=np.float32)
    for ch in range(2):
        # Convolution par blocs (overlap-add) : mémoire bornée sur 9 minutes.
        out[:, ch] = signal.oaconvolve(xs[:, ch].astype(np.float32), ir[:, ch].astype(np.float32))
    out *= wet
    out[: len(xs)] += xs * dry
    return out


def distance(x: np.ndarray, amount: float) -> np.ndarray:
    """Éloigne un son : coupe progressivement les aigus (amount ∈ [0, 1])."""
    if amount <= 0.0:
        return x
    fc = 16000.0 * (1.0 - amount) + 500.0 * amount
    return lowpass(x, fc, 2)


# ----------------------------------------------------------------- dynamique

def limiter(x: np.ndarray, ceiling_db: float = -1.0, release: float = 0.08) -> np.ndarray:
    """Limiteur à anticipation (5 ms) : aucun échantillon au-dessus du plafond.

    La réduction de gain démarre 5 ms avant chaque crête (pas de distorsion
    d'attaque) et remonte selon `release` (s).
    """
    from scipy.ndimage import maximum_filter1d
    ceil = from_db(ceiling_db)
    look = samples(0.005)
    xs = to_stereo(x)
    peak = np.max(np.abs(xs), axis=1)
    # Crête sur la fenêtre [i, i + look] (filtre max décalé vers l'avenir).
    pk = maximum_filter1d(peak, size=look, origin=-(look // 2))
    gain = np.minimum(1.0, ceil / np.maximum(pk, 1e-9))
    rel = math.exp(-1.0 / (release * SR))
    g = signal.lfilter([1.0 - rel], [1.0, -rel], gain, zi=[gain[0] * rel])[0]
    g = np.minimum(g, gain)
    return np.clip(xs * g[:, None], -ceil, ceil)


def loudness_lufs(x: np.ndarray) -> float:
    """Sonie intégrée (LUFS, ITU-R BS.1770) via pyloudnorm."""
    import pyloudnorm as pyln
    return float(pyln.Meter(SR).integrated_loudness(to_stereo(x)))


def normalize_lufs(x: np.ndarray, target: float) -> np.ndarray:
    """Ramène la sonie intégrée à `target` LUFS (gain global)."""
    cur = loudness_lufs(x)
    return x * from_db(target - cur)


# ----------------------------------------------------------------- boucle

def wrap_loop(x: np.ndarray, loop_len: int) -> np.ndarray:
    """Boucle sans couture : ce qui dépasse loop_len (queues de réverbération,
    notes tenues) est replié et ajouté au début, puis coupé à loop_len.
    Lue en boucle, la piste est alors continue à l'échantillon près.
    """
    xs = to_stereo(x)
    out = xs[:loop_len].copy()
    extra = xs[loop_len:]
    while len(extra) > 0:
        m = min(len(extra), loop_len)
        out[:m] += extra[:m]
        extra = extra[m:]
    return out


# ----------------------------------------------------------------- export

def write_ogg(x: np.ndarray, path: Path, quality: int = 6) -> None:
    """Écrit `x` en Ogg Vorbis (qualité 0..10) via ffmpeg/libvorbis.

    Effet de bord : crée le fichier (et un .wav temporaire supprimé ensuite).
    """
    import soundfile as sf
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_suffix(".tmp.wav")
    sf.write(tmp, to_stereo(x).astype(np.float32), SR, subtype="FLOAT")
    try:
        subprocess.run(["ffmpeg", "-y", "-loglevel", "error", "-i", str(tmp), "-c:a", "libvorbis",
                        "-q:a", str(quality), str(path)], check=True)
    finally:
        tmp.unlink(missing_ok=True)


def write_wav16(x: np.ndarray, path: Path) -> None:
    """Écrit `x` en WAV PCM 16 bits (sons courts : lecture immédiate, sans décodage)."""
    import soundfile as sf
    path.parent.mkdir(parents=True, exist_ok=True)
    sf.write(path, to_stereo(x) if x.ndim == 2 else x, SR, subtype="PCM_16")
