"""Textures des pièces de personnalisation (générées, licence CC0 du projet).

* Mèches de cheveux : UNE texture neutre ; la couleur est une teinte du
  matériau appliquée par le moteur (MaterialFlags::Tint).
* Yeux : UNE texture (sclère + iris neutre masqué par l'alpha), teintée par
  le moteur (MaterialFlags::TintMasked).
* Corne / ivoire : dégradé avec anneaux de croissance.
* Teintes de peau : calcul des teintes multiplicatives appliquées aux textures
  de peau existantes (aucune nouvelle texture de peau ; le budget de 64
  textures du moteur est préservé).

Aucune image n'est extraite de la bande dessinée.
"""

from __future__ import annotations

from pathlib import Path

import numpy as np
from PIL import Image


def _hex(c: str) -> np.ndarray:
    c = c.lstrip("#")
    return np.array([int(c[i:i + 2], 16) for i in (0, 2, 4)], dtype=np.float64) / 255.0


def hair_texture(path: Path, size: int = 256, seed: int = 3) -> None:
    """Texture de mèches NEUTRE (gris clair, répétable horizontalement).

    La couleur est appliquée par le moteur (teinte du matériau, drapeau Tint) :
    une seule texture sert toutes les couleurs de cheveux de la palette.
    Valeurs entre ~0,45 et 1,0 (moyenne ~0,8) : texture × teinte ≈ couleur cible.
    """
    rng = np.random.default_rng(seed)
    h = w = size
    x = np.arange(w)[None, :]
    y = np.arange(h)[:, None]
    field = np.zeros((h, w))
    for _ in range(260):
        cx = rng.uniform(0, w)
        width = rng.uniform(0.5, 2.4)
        amp = rng.uniform(-0.4, 0.45)
        drift = rng.uniform(-0.06, 0.06)
        d = np.abs(((x - cx - y * drift) + w / 2) % w - w / 2)
        field += amp * np.exp(-(d / width) ** 2)
    field = (field - field.min()) / (field.max() - field.min() + 1e-9)
    v = (y / h)
    shade = (0.50 + 0.50 * field) * (0.86 + 0.14 * v)    # racines plus sombres en haut
    shade = np.clip(shade + 0.05 * field * (1 - v), 0, 1)
    img = np.repeat(shade[..., None], 3, axis=2)
    path.parent.mkdir(parents=True, exist_ok=True)
    Image.fromarray((img * 255).astype(np.uint8)).save(path, optimize=True)


def eye_texture(path: Path, size: int = 128) -> None:
    """Œil vu de face : sclère (alpha 1, non teintée), iris gris clair strié
    (alpha 0 : zone teintée par le moteur, drapeau TintMasked), pupille sombre."""
    yy, xx = np.mgrid[0:size, 0:size]
    c = (size - 1) / 2
    r = np.hypot(xx - c, yy - c) / (size / 2)
    ang = np.arctan2(yy - c, xx - c)
    img = np.ones((size, size, 4))
    img[..., :3] = np.array([0.92, 0.90, 0.86])
    stri = 0.80 + 0.20 * np.sin(ang * 23) * np.sin(ang * 7 + 1.3)
    ring = np.clip((r - 0.30) / 0.10, 0, 1)
    iris = (stri * (1.0 - 0.35 * ring))[..., None] * np.ones(3)
    m_iris = r < 0.42
    img[m_iris, :3] = iris[m_iris]
    img[m_iris, 3] = 0.0
    pupil = r < 0.17
    img[pupil, :3] = np.array([0.04, 0.03, 0.03])
    img[pupil, 3] = 1.0
    edge = (r >= 0.40) & (r < 0.44)
    img[edge, :3] = img[edge, :3] * 0.5
    img[edge, 3] = 1.0
    hl = (r > 0.25) & (r < 0.31) & (yy < c - 0.18 * size) & (xx < c)
    img[hl, :3] = 0.95
    img[hl, 3] = 1.0
    path.parent.mkdir(parents=True, exist_ok=True)
    Image.fromarray((np.clip(img, 0, 1) * 255).astype(np.uint8), mode="RGBA").save(path, optimize=True)


def horn_texture(path: Path, base: str = "#4A3A2C", tip: str = "#1E1914", size: int = 256) -> None:
    """Corne : dégradé de la base à la pointe (v), anneaux de croissance."""
    v = np.linspace(0, 1, size)[:, None]
    u = np.linspace(0, 1, size)[None, :]
    col = _hex(base)[None, None, :] * (1 - v[..., None]) + _hex(tip)[None, None, :] * v[..., None]
    rings = 0.88 + 0.12 * np.sin(v * 70 + 0.6 * np.sin(u * 2 * np.pi * 3))
    grain = 0.95 + 0.05 * np.sin(u * 2 * np.pi * 17 + v * 9)
    img = np.clip(col * (rings * grain)[..., None], 0, 1)
    path.parent.mkdir(parents=True, exist_ok=True)
    Image.fromarray((img * 255).astype(np.uint8)).save(path, optimize=True)


def tint_for(target: str, reference: tuple) -> str:
    """Teinte multiplicative (« #RRGGBB ») qui amène la couleur ``reference``
    (médiane de la texture, 0..1) vers ``target`` ; bornée à 1 par canal."""
    tgt = _hex(target)
    ref = np.maximum(np.asarray(reference, dtype=np.float64), 1e-3)
    t = np.clip(tgt / ref, 0.0, 1.0)
    return "#" + "".join(f"{int(round(c * 255)):02X}" for c in t)


def median_skin(path: Path) -> tuple:
    """Couleur médiane de la peau d'une texture (pixels assez clairs)."""
    img = np.asarray(Image.open(path).convert("RGB"), dtype=np.float64) / 255.0
    lum = img @ np.array([0.299, 0.587, 0.114])
    sel = img[lum > 0.25]
    return tuple(np.median(sel, axis=0)) if len(sel) else (0.7, 0.5, 0.35)
