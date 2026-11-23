"""Têtes paramétriques (crâne + visage + cou) et yeux.

La tête est un « loft » de sections horizontales (du sommet du crâne jusqu'à
la base du cou, qui plonge dans le col du corps). Chaque section est une
super-ellipse décrite par trois profils interpolés selon la hauteur :
demi-largeur, avant, arrière. Les reliefs du visage (nez, arcades, orbites,
pommettes, lèvres, menton) sont des déplacements gaussiens vers l'avant.

Le visage peint est celui de la texture de peau du corps
(T_Regular_<Genre>_BaseColor : îlot « visage » en haut à gauche). La face
avant est projetée sur cet îlot en alignant les repères (yeux, nez, bouche) ;
l'arrière et le dessus du crâne sont projetés sur une zone de peau unie.
La tête porte donc le matériau de peau du corps et suit automatiquement la
teinte choisie (claire, foncée, teintes orques ou démoniaques).
"""

from __future__ import annotations

from dataclasses import dataclass, field

import numpy as np

from geom import Mesh, gauss2, surface, smoothstep

TEX_SIZE = 2048.0

# Repères de l'îlot « visage » des textures de peau (pixels, texture 2048²).
FACE_LANDMARKS = {
    "male": {"cx": 382.0, "eye_v": 365.0, "eye_dx": 102.5, "plain": (382.0, 215.0), "top_v": 180.0, "bottom_v": 600.0},
    "female": {"cx": 375.0, "eye_v": 368.0, "eye_dx": 107.0, "plain": (375.0, 225.0), "top_v": 185.0, "bottom_v": 595.0},
}

# Zone de peau unie (pixels) utilisée par les oreilles, le cou, les ailes et la
# queue : bas de la jambe (texture 2048², même zone homme/femme).
PLAIN_SKIN_PATCH = (1660.0, 1540.0, 120.0)   # centre u, centre v, demi-taille


@dataclass
class HeadShape:
    """Paramètres d'une tête (mètres, repère tête : x droite, y avant, z haut,
    origine au niveau des yeux sur l'axe de symétrie)."""

    gender: str = "male"
    # Profils (hauteur relative aux yeux -> valeur). Interpolés linéairement.
    z: tuple = (0.118, 0.108, 0.090, 0.062, 0.030, 0.0, -0.020, -0.036, -0.052, -0.066,
                -0.080, -0.094, -0.106, -0.128, -0.150)
    half_width: tuple = (0.006, 0.050, 0.067, 0.076, 0.078, 0.077, 0.075, 0.071, 0.066, 0.058,
                         0.047, 0.052, 0.054, 0.056, 0.060)
    front: tuple = (0.000, 0.042, 0.068, 0.082, 0.088, 0.088, 0.090, 0.090, 0.088, 0.085,
                    0.079, 0.052, 0.040, 0.038, 0.042)
    back: tuple = (0.000, 0.066, 0.092, 0.104, 0.106, 0.103, 0.097, 0.088, 0.076, 0.064,
                   0.056, 0.052, 0.050, 0.050, 0.052)
    center_y: float = -0.006         # décalage avant/arrière de l'axe du crâne
    face_exp: float = 2.6            # exposant de super-ellipse (avant plus plat)
    nose: float = 1.0                # taille du nez (multiplicateur)
    nose_width: float = 1.0
    brow: float = 1.0                # arcades
    jaw: float = 1.0                 # largeur de mâchoire (multiplicateur bas du visage)
    chin: float = 1.0                # menton
    cheek: float = 1.0               # pommettes
    lips: float = 1.0
    eye_spacing: float = 0.0315      # demi-écart des yeux (aligné sur la texture)
    scale: float = 1.0               # mise à l'échelle globale autour des yeux
    brow_slope: float = 0.0          # front fuyant (orc)
    neck: float = 1.0                # épaisseur du cou (races trapues)
    extras: dict = field(default_factory=dict)


def _interp(shape: HeadShape, key: str, z: np.ndarray) -> np.ndarray:
    zs = np.array(shape.z)[::-1]
    vals = np.array(getattr(shape, key))[::-1]
    return np.interp(z, zs, vals)


def head_point(shape: HeadShape, u: np.ndarray, v: np.ndarray) -> np.ndarray:
    """Point de la surface pour (u = azimut 0..1, 0 = avant ; v = 0 sommet .. 1 base du cou)."""
    z_top, z_bot = shape.z[0], shape.z[-1]
    # Échantillonnage en hauteur plus dense vers le visage.
    z = z_top + (z_bot - z_top) * v
    phi = u * 2 * np.pi
    s, c = np.sin(phi), np.cos(phi)
    a = _interp(shape, "half_width", z)
    f = _interp(shape, "front", z)
    b = _interp(shape, "back", z)
    # Mâchoire : élargit/rétrécit le bas du visage.
    jaw_zone = smoothstep(-0.02, -0.07, z) * (1 - smoothstep(-0.085, -0.11, z))
    a = a * (1 + (shape.jaw - 1) * jaw_zone)
    neck_zone = smoothstep(-0.092, -0.115, z)
    neck_mul = 1 + (shape.neck - 1) * neck_zone
    a = a * neck_mul
    f = f * neck_mul
    b = b * neck_mul
    p = shape.face_exp
    x = a * np.sign(s) * np.abs(s) ** (2.0 / p)
    yf = f * np.sign(c) * np.abs(c) ** (2.0 / p)
    yb = b * np.sign(c) * np.abs(c) ** (2.0 / 2.2)
    y = np.where(c >= 0, yf, yb) + shape.center_y
    # Front fuyant : recule le haut de l'avant du crâne.
    y = y - shape.brow_slope * np.clip(z - 0.02, 0, None) * np.clip(c, 0, None)
    # Reliefs du visage, uniquement sur l'avant.
    front_w = smoothstep(0.35, 0.8, c)
    d = np.zeros_like(z)
    nz = -0.022
    d += 0.020 * shape.nose * gauss2(x, z, 0.0, nz, 0.010 * shape.nose_width, 0.017)
    d += 0.008 * shape.nose * gauss2(x, z, 0.0, -0.030, 0.007 * shape.nose_width, 0.006)
    d -= 0.004 * gauss2(np.abs(x), z, 0.0, 0.006, 0.006, 0.010)         # racine du nez
    d -= 0.010 * gauss2(np.abs(x), z, shape.eye_spacing, 0.0, 0.012, 0.009)
    d += 0.006 * shape.brow * gauss2(np.abs(x), z, 0.026, 0.017, 0.026, 0.007)
    d += 0.004 * shape.cheek * gauss2(np.abs(x), z, 0.045, -0.024, 0.016, 0.014)
    d += 0.004 * shape.lips * gauss2(x, z, 0.0, -0.053, 0.018, 0.005)
    d += 0.003 * shape.lips * gauss2(x, z, 0.0, -0.060, 0.015, 0.004)
    d += 0.006 * shape.chin * gauss2(x, z, 0.0, -0.077, 0.018, 0.010)
    y = y + d * front_w
    pts = np.stack([x, y, z], axis=1) * shape.scale
    return pts


def head_uv(shape: HeadShape, P: np.ndarray, u: np.ndarray) -> np.ndarray:
    """UV : avant projeté sur l'îlot visage, arrière sur une zone unie du front."""
    lm = FACE_LANDMARKS[shape.gender]
    k = lm["eye_dx"] / (shape.eye_spacing * shape.scale)      # pixels par mètre
    c = np.cos(u * 2 * np.pi)
    x, z = P[:, 0], P[:, 2]
    # Le bas du visage se rétrécit dans l'îlot : on resserre la projection
    # horizontale sous la bouche pour rester dans la zone peinte.
    narrow = 1.0 - 0.25 * smoothstep(-0.04, -0.085, z / shape.scale)
    fu = lm["cx"] + x * k * narrow
    fv = np.clip(lm["eye_v"] - z * k, lm["top_v"], lm["bottom_v"])
    # Arrière : bande unie au-dessus du visage, compressée.
    bu = lm["plain"][0] + x * k * 0.45
    bv = lm["plain"][1] + np.clip(-z, -0.15, 0.2) * k * 0.15
    # Cou (sous la mâchoire) : même zone unie que l'arrière du crâne.
    zl = z / shape.scale
    w = smoothstep(-0.15, 0.25, c) * (1 - smoothstep(-0.070, -0.092, zl))
    U = fu * w + bu * (1 - w)
    V = fv * w + bv * (1 - w)
    return np.stack([U, V], axis=1) / TEX_SIZE


def build_head(shape: HeadShape, eye_world: np.ndarray, nu: int, nv: int) -> Mesh:
    """Tête + cou en espace Blender. ``eye_world`` = point entre les yeux."""
    def fn(U, V):
        return head_point(shape, U, V)

    def uvf(U, V, P):
        return head_uv(shape, P, U)

    m = surface(fn, nu, nv, wrap_u=True, uv_fn=uvf)
    m.v = m.v + eye_world
    # Pôle du sommet : tous les points de la première ligne se confondent ; la
    # dernière ligne (base du cou) reste ouverte (cachée dans le col).
    return _orient_outward(m, eye_world + np.array([0.0, 0.0, -0.02]))


def _orient_outward(m: Mesh, center: np.ndarray) -> Mesh:
    """Oriente les faces vers l'extérieur (par rapport à un point intérieur)."""
    a, b, c = m.v[m.f[:, 0]], m.v[m.f[:, 1]], m.v[m.f[:, 2]]
    fn = np.cross(b - a, c - a)
    ctr = (a + b + c) / 3 - center
    if np.sum(np.einsum("ij,ij->i", fn, ctr)) < 0:
        return m.flipped()
    return m


def eye_positions(shape: HeadShape, eye_world: np.ndarray) -> list:
    """Centres des globes oculaires (espace Blender) et rayon."""
    xs = np.array([shape.eye_spacing, -shape.eye_spacing])
    out = []
    for x in xs:
        # Avant de la surface au niveau de l'œil, recherche le long de u.
        us = np.linspace(-0.25, 0.25, 801) % 1.0
        P = head_point(shape, us, np.full_like(us, (shape.z[0] - 0.0) / (shape.z[0] - shape.z[-1])))
        P = P[P[:, 1] > 0]
        i = np.argmin(np.abs(P[:, 0] - x * shape.scale))
        surf_y = P[i, 1]
        r = 0.0108 * shape.scale
        center = np.array([x * shape.scale, surf_y - r * 0.75, 0.0]) + eye_world
        out.append((center, r))
    return out


def build_eye(center: np.ndarray, r: float, nu: int, nv: int) -> Mesh:
    """Globe oculaire ; UV : projection frontale (iris au centre de la texture)."""
    def fn(U, V):
        th = V * np.pi
        ph = U * 2 * np.pi
        return np.stack([r * np.sin(th) * np.sin(ph), r * np.sin(th) * np.cos(ph), r * np.cos(th)], axis=1)

    def uvf(U, V, P):
        return np.stack([0.5 + P[:, 0] / (2.2 * r), 0.5 - P[:, 2] / (2.2 * r)], axis=1)

    m = surface(fn, nu, nv, wrap_u=True, uv_fn=uvf)
    m.v = m.v + center
    return _orient_outward(m, center)


# ----------------------------------------------------------------------
# Variantes de visage par race et genre
# ----------------------------------------------------------------------
def face_variants(race: str, gender: str) -> list:
    """Retourne les 3 formes de visage proposées pour une race et un genre."""
    base = HeadShape(gender=gender)
    if gender == "female":
        base.scale = 0.95
        base.brow = 0.35
        base.jaw = 0.90
        base.chin = 0.7
        base.nose = 0.80
        base.nose_width = 0.85
        base.lips = 1.4
        base.eye_spacing = 0.0315 * (107.0 / 102.5) * 0.98
    if race == "nains":
        base.scale *= 1.10
        base.neck = 1.25
        base.jaw *= 1.12
        base.nose *= 1.35
        base.nose_width *= 1.3
        base.brow *= 1.6
        base.cheek *= 1.3
    elif race == "orcs":
        base.neck = 1.18
        base.jaw *= 1.22
        base.brow *= 2.4
        base.brow_slope = 0.35
        base.nose *= 0.75
        base.nose_width *= 1.6
        base.chin *= 1.5
    elif race == "elfes":
        base.jaw *= 0.9
        base.chin *= 0.8
        base.cheek *= 1.5
        base.nose *= 0.9
        base.nose_width *= 0.85
    elif race == "demons":
        base.brow *= 1.8
        base.cheek *= 1.8
        base.jaw *= 1.05
        base.chin *= 1.3
    variants = []
    for jaw, nose, chin, cheek in ((1.0, 1.0, 1.0, 1.0), (1.08, 1.18, 1.25, 0.8), (0.92, 0.88, 0.8, 1.35)):
        s = HeadShape(**{k: getattr(base, k) for k in base.__dataclass_fields__})
        s.jaw *= jaw
        s.nose *= nose
        s.chin *= chin
        s.cheek *= cheek
        variants.append(s)
    return variants


# ----------------------------------------------------------------------
# Oreilles
# ----------------------------------------------------------------------
def plain_skin_uv(param_uv: np.ndarray, spread: float = 1.0) -> np.ndarray:
    """Projette des UV paramétriques (0..1) dans la zone de peau unie."""
    cu, cv, half = PLAIN_SKIN_PATCH
    uv = np.asarray(param_uv, dtype=np.float64) % 1.0
    return np.stack([cu + (uv[:, 0] - 0.5) * 2 * half * spread,
                     cv + (uv[:, 1] - 0.5) * 2 * half * spread], axis=1) / TEX_SIZE


# Formes d'oreilles : (longueur, largeur, épaisseur, direction (arrière, haut), courbure, pointe)
EAR_STYLES = {
    "ear_human": (0.046, 0.021, 0.008, (0.12, 1.0), 0.008, 1.0),
    "ear_dwarf": (0.052, 0.025, 0.009, (0.12, 1.0), 0.008, 1.0),
    "ear_orc": (0.060, 0.022, 0.008, (0.7, 0.9), 0.006, 0.15),
    "ear_demon": (0.062, 0.018, 0.007, (0.8, 0.85), 0.006, 0.10),
    "ear_elf_short": (0.070, 0.020, 0.007, (0.55, 1.0), 0.008, 0.08),
    "ear_elf_long": (0.105, 0.019, 0.007, (0.85, 0.9), 0.010, 0.05),
    "ear_elf_swept": (0.090, 0.020, 0.007, (1.0, 0.35), 0.006, 0.05),
}


def build_ears(style: str, shape: HeadShape, eye_world: np.ndarray, samples: int, sides: int) -> Mesh:
    """Paire d'oreilles (gauche + droite) posées sur les côtés de la tête."""
    from geom import bezier, merge, tube
    length, width, thick, (back, up), curl, tip = EAR_STYLES[style]
    s = shape.scale
    zb = -0.012
    half = float(np.interp(zb, np.array(shape.z)[::-1], np.array(shape.half_width)[::-1])) * s
    meshes = []
    for side in (1.0, -1.0):
        base = eye_world + np.array([side * (half - 0.004), shape.center_y * s - 0.008, zb * s - 0.012])
        d = np.array([side * 0.25, -back, up])
        d /= np.linalg.norm(d)
        ctrl = np.array([base,
                         base + d * length * 0.45 + np.array([side * curl, 0.0, 0.0]),
                         base + d * length])
        path = bezier(ctrl, samples)

        def radius(t, w=width, tp=tip):
            prof = np.sqrt(np.maximum(np.sin(np.pi * (0.06 + 0.92 * np.clip(t, 0, 1))), 0.0))
            return np.maximum(w * prof * (1 - (1 - tp) * t ** 1.6), 0.0012)

        m = tube(path, radius, sides, cap_end=True, flatten=thick / width,
                 up_hint=(side, 0.0, 0.0))
        m.uv = plain_skin_uv(m.uv * 0.3 + 0.35)
        meshes.append(m)
    return merge(meshes)
