"""Coiffures, barbes, moustaches et sourcils paramétriques.

Principe : la chevelure est une coque qui épouse le crâne (surface de la tête
décalée vers l'extérieur d'une épaisseur variable), limitée par une ligne
d'implantation (front, tempes, nuque). Les cheveux longs prolongent la coque
par un « rideau » qui tombe sous la ligne d'implantation. Des mèches
(ondulations de l'épaisseur et pointes irrégulières) donnent du relief.
Les éléments rapportés (queue de cheval, chignon, tresse, crête) sont des
tubes ou des volumes ajoutés.

Toutes ces pièces portent le matériau ``MI_Hair`` : la couleur est choisie à
l'exécution (une texture de mèches par couleur de la palette).
"""

from __future__ import annotations

from dataclasses import dataclass

import numpy as np

from geom import Mesh, bezier, merge, orient_outward, smoothstep, surface, tube
from heads import HeadShape, head_point

HAIR_MATERIAL = "MI_Hair"


def _v_of_z(shape: HeadShape, z: np.ndarray) -> np.ndarray:
    """Paramètre v de la tête pour une hauteur relative aux yeux (non mise à l'échelle)."""
    return (shape.z[0] - z) / (shape.z[0] - shape.z[-1])


def _head_and_normal(shape: HeadShape, u: np.ndarray, v: np.ndarray) -> tuple:
    """Point de la tête (repère tête mis à l'échelle) et normale extérieure."""
    eps = 1e-3
    p = head_point(shape, u, v)
    pu = head_point(shape, u + eps, v) - head_point(shape, u - eps, v)
    pv = head_point(shape, u, np.clip(v + eps, 0, 1)) - head_point(shape, u, np.clip(v - eps, 0, 1))
    n = np.cross(pu, pv)
    n /= np.maximum(np.linalg.norm(n, axis=1, keepdims=True), 1e-12)
    # Oriente vers l'extérieur (loin de l'axe vertical du crâne).
    radial = p.copy()
    radial[:, 2] = 0.0
    radial[:, 1] -= shape.center_y * shape.scale
    flip = np.einsum("ij,ij->i", n, radial) < 0
    n[flip] *= -1
    top = v < 0.02
    n[top] = np.array([0.0, 0.0, 1.0])
    return p, n


@dataclass
class HairStyle:
    """Description d'une coiffure (hauteurs relatives au niveau des yeux, mètres)."""

    hairline_front: float = 0.062     # haut du front
    hairline_temple: float = 0.030    # tempes
    hairline_side: float = -0.005     # au-dessus de l'oreille / pattes
    hairline_back: float = -0.085     # nuque
    thickness: float = 0.010          # épaisseur sur le dessus
    side_thickness: float = 0.007
    length: float = 0.0               # longueur du rideau sous l'implantation (0 = court)
    length_front: float = 0.0         # longueur des mèches de devant (encadrent le visage)
    flare: float = 0.015              # écartement du bas du rideau
    locks: int = 26                   # nombre de mèches autour de la tête
    lock_depth: float = 0.25          # amplitude du relief des mèches
    shaved_sides: bool = False        # côtés rasés (crête / iroquoise)
    crest_width: float = 0.0          # demi-largeur de la crête (azimut, 0..0.5)
    crest_height: float = 0.0
    part: float = 0.0                 # raie (décalage latéral, -1..1)
    front_open: float = 0.18          # demi-ouverture du visage pour les cheveux longs (azimut)


def _hairline_z(style: HairStyle, u: np.ndarray) -> np.ndarray:
    """Hauteur de la ligne d'implantation selon l'azimut (0 = avant, 0.5 = arrière)."""
    a = np.minimum(u, 1 - u)            # 0 avant .. 0.5 arrière
    pts_a = np.array([0.0, 0.08, 0.17, 0.24, 0.32, 0.5])
    pts_z = np.array([style.hairline_front, style.hairline_front - 0.008, style.hairline_temple,
                      style.hairline_side, (style.hairline_side + style.hairline_back) / 2,
                      style.hairline_back])
    return np.interp(a, pts_a, pts_z)


def build_hair(shape: HeadShape, style: HairStyle, eye_world: np.ndarray, nu: int, nw: int) -> Mesh:
    """Coque de cheveux (+ rideau pour les cheveux longs) en espace Blender."""
    s = shape.scale
    z_top = shape.z[0]
    seed = np.random.default_rng(7)
    phase = seed.uniform(0, 2 * np.pi, 4)

    def lock_mod(u):
        k = style.locks
        return 1.0 + style.lock_depth * (0.5 * np.sin(2 * np.pi * k * u + phase[0])
                                         + 0.3 * np.sin(2 * np.pi * (k * 0.53) * u + phase[1])
                                         + 0.25 * np.sin(2 * np.pi * (k * 2.1) * u + phase[3]))

    has_curtain = style.length > 0 or style.length_front > 0
    w_scalp = 0.7 if has_curtain else 1.0

    def fn(U, W):
        hz = _hairline_z(style, U)
        ws = np.minimum(W / w_scalp, 1.0)
        z = z_top - (z_top - hz) * ws
        v = _v_of_z(shape, z)
        p, n = _head_and_normal(shape, U, v)
        a = np.minimum(U, 1 - U)
        t = style.side_thickness + (style.thickness - style.side_thickness) * (1 - smoothstep(0.0, 0.85, ws))
        if style.shaved_sides:
            crest = 1 - smoothstep(style.crest_width * 0.6, style.crest_width, np.abs(np.sin(U * 2 * np.pi)) * 0.25)
            t = 0.0015 + (style.crest_height * (1 - ws) ** 0.6 + style.thickness) * crest
        t = t * lock_mod(U) * (0.12 + 0.88 * smoothstep(0.0, 0.22, 1 - ws))
        # Raie : léger creux.
        if style.part != 0.0:
            t = t * (1 - 0.6 * np.exp(-((np.sin(U * 2 * np.pi) - style.part * 0.35) / 0.05) ** 2) * (1 - ws))
        P = p + n * t[:, None]
        if has_curtain:
            extra = np.clip((W - w_scalp) / (1 - w_scalp), 0, 1)
            fo = style.front_open
            L = np.interp(a, [0.0, fo * 0.85, fo * 1.35, 0.30, 0.38, 0.5],
                          [0.0, 0.0, style.length_front, style.length_front + (style.length - style.length_front) * 0.45,
                           style.length, style.length])
            L = L * (1 + 0.12 * np.sin(2 * np.pi * style.locks * 0.5 * U + phase[2]))
            out = n.copy()
            out[:, 2] = 0
            out /= np.maximum(np.linalg.norm(out, axis=1, keepdims=True), 1e-9)
            P = P + np.stack([np.zeros_like(U), np.zeros_like(U), -extra * L], axis=1) * s
            grow = smoothstep(0.0, 0.03, L)
            P = P + out * ((style.flare * extra ** 1.5 + 0.002 * extra) * grow)[:, None] * s
            # Dégagement : le rideau reste à l'extérieur de la tête et du cou.
            zc = np.clip(P[:, 2] / s, shape.z[-1], shape.z[0] - 0.01)
            hp, _hn = _head_and_normal(shape, U, _v_of_z(shape, zc))
            ctr = np.array([0.0, shape.center_y * s])
            rh = np.linalg.norm(hp[:, :2] - ctr, axis=1)
            rp = np.linalg.norm(P[:, :2] - ctr, axis=1)
            need = rh + style.side_thickness * 0.8 * s
            push = np.maximum(need - rp, 0.0) * (extra > 0) * grow
            dirs = (P[:, :2] - ctr) / np.maximum(rp, 1e-9)[:, None]
            P[:, :2] = P[:, :2] + dirs * push[:, None]
        return P

    def uvf(U, W, P):
        return np.stack([U * 6.0, W * 1.0], axis=1)

    m = surface(fn, nu, nw, wrap_u=True, uv_fn=uvf)
    m.v = m.v + eye_world
    m = orient_outward(m, eye_world + np.array([0.0, shape.center_y * s, 0.0]))
    if has_curtain:
        # Face intérieure seulement pour le rideau (le dessous du crâne est caché par la tête).
        j0 = max(int((w_scalp - 0.12) * (nw - 1)), 0)
        sub = m.f[j0 * 2 * nu:]
        inner = Mesh(m.v - m.normals() * 0.004, sub[:, ::-1].copy(), m.uv.copy())
        m = merge([m, inner])
    return m


def _ponytail(shape: HeadShape, eye_world: np.ndarray, high: bool, length: float, samples: int, sides: int) -> Mesh:
    s = shape.scale
    zb = 0.02 if high else -0.045
    yb = (shape.center_y - 0.105) * s
    start = eye_world + np.array([0.0, yb, zb * s])
    ctrl = np.array([start, start + np.array([0, -0.05, -0.02]) * s,
                     start + np.array([0, -0.06, -length * 0.6]) * s,
                     start + np.array([0, -0.035, -length]) * s])
    path = bezier(ctrl, samples)
    m = tube(path, lambda t: 0.020 * s * (1 - 0.55 * t) * (1 + 0.15 * np.sin(t * 25)), sides, cap_end=True)
    m.uv = np.stack([m.uv[:, 0] * 2.0, m.uv[:, 1]], axis=1)
    return m


def _bun(shape: HeadShape, eye_world: np.ndarray, top: bool, nu: int, nv: int) -> Mesh:
    s = shape.scale
    c = eye_world + (np.array([0.0, shape.center_y - 0.03, 0.135]) if top
                     else np.array([0.0, shape.center_y - 0.11, 0.02])) * s
    r = 0.038 * s

    def fn(U, V):
        th, ph = V * np.pi, U * 2 * np.pi
        rr = r * (1 + 0.12 * np.sin(ph * 7 + th * 3))
        return c + np.stack([rr * np.sin(th) * np.sin(ph), rr * np.sin(th) * np.cos(ph) * 0.85,
                             rr * np.cos(th) * 0.8], axis=1)

    m = surface(fn, nu, nv, wrap_u=True, uv_fn=lambda U, V, P: np.stack([U * 3, V], axis=1))
    return m.flipped() if _inward(m, c) else m


def _braid(shape: HeadShape, eye_world: np.ndarray, length: float, samples: int, sides: int, x: float = 0.0) -> Mesh:
    s = shape.scale
    start = eye_world + np.array([x, (shape.center_y - 0.10) * s, -0.04 * s])
    ctrl = np.array([start, start + np.array([0, -0.035, -length * 0.4]) * s,
                     start + np.array([0, -0.02, -length]) * s])
    path = bezier(ctrl, samples)
    m = tube(path, lambda t: 0.016 * s * (1 - 0.45 * t) * (0.8 + 0.2 * np.abs(np.sin(t * 40))), sides, cap_end=True)
    return m


def _inward(m: Mesh, c: np.ndarray) -> bool:
    a, b, cc = m.v[m.f[:, 0]], m.v[m.f[:, 1]], m.v[m.f[:, 2]]
    fn = np.cross(b - a, cc - a)
    return float(np.sum(np.einsum("ij,ij->i", fn, (a + b + cc) / 3 - c))) < 0


# ----------------------------------------------------------------------
# Catalogue des coiffures
# ----------------------------------------------------------------------
HAIR_STYLES = {
    # id: (nom affiché, HairStyle, ajouts)
    "short": ("Court", HairStyle(thickness=0.009, side_thickness=0.006, locks=30, lock_depth=0.3), []),
    "cropped": ("Ras", HairStyle(thickness=0.0035, side_thickness=0.0028, locks=60, lock_depth=0.1,
                                 hairline_front=0.058), []),
    "swept": ("Coiffé en arrière", HairStyle(thickness=0.014, side_thickness=0.007, locks=22, lock_depth=0.35,
                                            hairline_front=0.066, part=0.0), []),
    "medium": ("Mi-long", HairStyle(thickness=0.012, side_thickness=0.009, length=0.07, length_front=0.03,
                                    flare=0.012, locks=24, front_open=0.20), []),
    "long": ("Long", HairStyle(thickness=0.012, side_thickness=0.009, length=0.20, length_front=0.10,
                               flare=0.03, locks=22, front_open=0.17), []),
    "bob": ("Carré", HairStyle(thickness=0.013, side_thickness=0.011, length=0.055, length_front=0.045,
                               flare=0.018, locks=36, lock_depth=0.15, front_open=0.19, part=0.6), []),
    "ponytail": ("Queue de cheval", HairStyle(thickness=0.008, side_thickness=0.006, locks=34, lock_depth=0.2),
                 ["ponytail_low"]),
    "high_tail": ("Queue haute", HairStyle(thickness=0.008, side_thickness=0.006, locks=34, lock_depth=0.2),
                  ["ponytail_high"]),
    "bun": ("Chignon", HairStyle(thickness=0.008, side_thickness=0.006, locks=34, lock_depth=0.2), ["bun_back"]),
    "topknot": ("Chignon haut", HairStyle(thickness=0.006, side_thickness=0.004, locks=40, lock_depth=0.15),
                ["bun_top"]),
    "braid": ("Tresse", HairStyle(thickness=0.008, side_thickness=0.006, locks=34, lock_depth=0.2), ["braid"]),
    "twin_braids": ("Deux tresses", HairStyle(thickness=0.008, side_thickness=0.006, locks=34, lock_depth=0.2,
                                              part=0.0), ["braid_left", "braid_right"]),
    "mohawk": ("Crête", HairStyle(thickness=0.006, shaved_sides=True, crest_width=0.07, crest_height=0.035,
                                  locks=50, lock_depth=0.25), []),
    "shaved_tail": ("Rasé à queue", HairStyle(thickness=0.003, shaved_sides=True, crest_width=0.05,
                                              crest_height=0.004, locks=50, lock_depth=0.1), ["ponytail_high"]),
    "wild": ("Hirsute", HairStyle(thickness=0.020, side_thickness=0.015, length=0.09, length_front=0.04,
                                  flare=0.04, locks=16, lock_depth=0.5, front_open=0.2), []),
}


def build_hair_style(style_id: str, shape: HeadShape, eye_world: np.ndarray, lod: int) -> Mesh:
    """Construit une coiffure complète pour un niveau de détail (0 = plus fin)."""
    res = [(84, 28, 18, 12), (50, 16, 12, 8), (30, 9, 8, 6)][lod]
    _name, style, extras = HAIR_STYLES[style_id]
    nw = res[1] + (16 if style.length > 0.1 else 0) // (lod + 1)
    parts = [build_hair(shape, style, eye_world, res[0], nw)]
    for e in extras:
        if e == "ponytail_low":
            parts.append(_ponytail(shape, eye_world, False, 0.22, res[2], res[3]))
        elif e == "ponytail_high":
            parts.append(_ponytail(shape, eye_world, True, 0.26, res[2], res[3]))
        elif e == "bun_back":
            parts.append(_bun(shape, eye_world, False, res[3] * 2, res[3]))
        elif e == "bun_top":
            parts.append(_bun(shape, eye_world, True, res[3] * 2, res[3]))
        elif e == "braid":
            parts.append(_braid(shape, eye_world, 0.30, res[2] * 2, res[3]))
        elif e == "braid_left":
            parts.append(_braid(shape, eye_world, 0.26, res[2] * 2, res[3], x=0.05 * shape.scale))
        elif e == "braid_right":
            parts.append(_braid(shape, eye_world, 0.26, res[2] * 2, res[3], x=-0.05 * shape.scale))
    return merge(parts)


# ----------------------------------------------------------------------
# Pilosité faciale
# ----------------------------------------------------------------------
@dataclass
class BeardStyle:
    """Barbe : coque sur le bas du visage, éventuellement prolongée sous le menton."""

    top: float = -0.025          # haut des joues (relatif aux yeux)
    side_reach: float = 0.27     # étendue en azimut (0.25 = jusqu'aux oreilles)
    thickness: float = 0.006
    length: float = 0.0          # prolongement sous le menton
    width: float = 1.0           # largeur du prolongement (1 = toute la mâchoire)
    chin_only: float = 0.0       # 1 = bouc (seulement le menton)
    braided: bool = False


BEARD_STYLES = {
    "stubble": ("Barbe de trois jours", BeardStyle(thickness=0.0012, top=-0.040, side_reach=0.235)),
    "short_beard": ("Barbe courte", BeardStyle(thickness=0.005, top=-0.040, side_reach=0.235)),
    "full_beard": ("Barbe fournie", BeardStyle(thickness=0.009, top=-0.038, side_reach=0.235, length=0.045,
                                               width=0.85)),
    "long_beard": ("Longue barbe", BeardStyle(thickness=0.010, top=-0.038, side_reach=0.235, length=0.15,
                                              width=0.7)),
    "goatee": ("Bouc", BeardStyle(thickness=0.006, top=-0.064, side_reach=0.07, chin_only=1.0, length=0.02,
                                  width=0.5)),
    "braided_beard": ("Barbe tressée", BeardStyle(thickness=0.010, top=-0.038, side_reach=0.235, length=0.20,
                                                  width=0.55, braided=True)),
    "chin_strap": ("Collier", BeardStyle(thickness=0.004, top=-0.072, side_reach=0.235, chin_only=1.0)),
}


def build_beard(shape: HeadShape, style: BeardStyle, eye_world: np.ndarray, nu: int, nw: int) -> Mesh:
    """Barbe en espace Blender : coque qui suit la mâchoire (+ prolongement sous le menton)."""
    s = shape.scale
    ext = 0.45 if style.length > 0 else 0.0
    reach = max(style.side_reach, 1e-3)

    def fn(U, W):
        az = (U - 0.5) * 2 * reach
        uu = az % 1.0
        r = np.abs(az) / reach
        w1 = np.minimum(W / (1 - ext) if ext else W, 1.0)
        top_c = max(style.top, -0.046) if style.chin_only == 0 else style.top
        z_top_line = top_c + (style.top + 0.045 - top_c) * r ** 1.6 if style.chin_only == 0 else \
            style.top + 0.0 * r
        z_low = -0.090 + 0.055 * r ** 2.0
        z = z_top_line + (z_low - z_top_line) * w1
        v = _v_of_z(shape, z)
        p, n = _head_and_normal(shape, uu, v)
        x = p[:, 0] / s
        zz = p[:, 2] / s
        mouth = np.exp(-(x / 0.024) ** 2 - ((zz + 0.056) / 0.008) ** 2)
        t = style.thickness * (1 - 0.95 * mouth) * smoothstep(0.0, 0.12, W) * (1 - 0.6 * r ** 3)
        t = t * (0.85 + 0.15 * np.sin(U * 70) ** 2)
        P = p + n * t[:, None]
        if ext:
            e = np.clip((W - (1 - ext)) / ext, 0, 1)
            narrow = 1 - (1 - style.width) * e
            P[:, 0] = P[:, 0] * narrow
            P[:, 2] = P[:, 2] - e * style.length * s * (1 - 0.6 * r ** 2)
            P[:, 1] = P[:, 1] - e * style.length * 0.25 * s
        return P

    m = surface(fn, nu, nw, uv_fn=lambda U, V, P: np.stack([U * 3.0, V], axis=1))
    m.v = m.v + eye_world
    m = orient_outward(m, eye_world + np.array([0.0, shape.center_y * s, -0.02 * s]))
    inner = m.copy()
    inner.v = inner.v - m.normals() * 0.0025
    return merge([m, inner.flipped()])


def build_beard_style(style_id: str, shape: HeadShape, eye_world: np.ndarray, lod: int) -> Mesh:
    res = [(48, 22), (28, 12), (16, 7)][lod]
    _name, style = BEARD_STYLES[style_id]
    m = build_beard(shape, style, eye_world, res[0], res[1] + (8 if style.length > 0.1 else 0) // (lod + 1))
    if style.braided:
        s = shape.scale
        start = eye_world + np.array([0.0, (shape.center_y + 0.07) * s, -0.10 * s])
        path = bezier(np.array([start, start + np.array([0, 0.012, -0.12]) * s,
                                start + np.array([0, 0.0, -0.24]) * s]), [24, 14, 8][lod])
        m = merge([m, tube(path, lambda t: 0.014 * s * (1 - 0.5 * t) * (0.8 + 0.2 * np.abs(np.sin(t * 36))),
                           [10, 8, 6][lod])])
    return m


MOUSTACHE_STYLES = {
    # id: (nom, épaisseur, largeur, tombant (m), pointes relevées (m))
    "thin": ("Fine", 0.0035, 0.026, 0.004, 0.0),
    "thick": ("Épaisse", 0.0065, 0.030, 0.008, 0.0),
    "handlebar": ("Guidon", 0.0055, 0.034, 0.002, 0.014),
    "drooping": ("Tombante", 0.0060, 0.030, 0.030, 0.0),
}


def build_moustache(style_id: str, shape: HeadShape, eye_world: np.ndarray, lod: int) -> Mesh:
    """Moustache : deux tubes partant du milieu de la lèvre supérieure."""
    _name, thick, width, droop, curl = MOUSTACHE_STYLES[style_id]
    s = shape.scale
    samples, sides = [(16, 8), (10, 6), (6, 5)][lod]
    us = np.array([0.0])
    v = _v_of_z(shape, np.array([-0.046]))
    p0, n0 = _head_and_normal(shape, us, v)
    out = []
    for side in (1.0, -1.0):
        pts = []
        for t in np.linspace(0, 1, 5):
            x = side * width * t
            # Point de surface du visage à cette abscisse.
            ua = np.linspace(-0.2, 0.2, 401) % 1.0
            zz = -0.046 - droop * t ** 1.5 + curl * t ** 3
            pp = head_point(shape, ua, np.full_like(ua, _v_of_z(shape, np.array([zz]))[0]))
            pp = pp[pp[:, 1] > 0]
            k = np.argmin(np.abs(pp[:, 0] - x * s))
            pts.append(pp[k] + np.array([0.0, thick * 0.8 * s, 0.0]))
        path = bezier(np.array(pts), samples) + eye_world
        out.append(tube(path, lambda t: thick * s * (1 - 0.7 * t ** 1.5) + 0.0008, sides, cap_end=True,
                        flatten=0.7))
    return merge(out)


BROWS = ("brows", 0.0022)


def build_brows(shape: HeadShape, eye_world: np.ndarray, lod: int, thickness_mul: float = 1.0) -> Mesh:
    """Sourcils : deux coques fines au-dessus des yeux (couleur des cheveux)."""
    s = shape.scale
    nu, nw = [(16, 5), (10, 3), (6, 2)][lod]
    out = []
    for side in (1.0, -1.0):
        def fn(U, W, side=side):
            x = side * (0.010 + U * 0.034)
            zc = 0.019 + 0.006 * np.sin(U * np.pi) - 0.004 * U
            z = zc + (W - 0.5) * 0.007 * (1 - 0.5 * U)
            ua = np.linspace(-0.22, 0.22, 801) % 1.0
            res = []
            for xi, zi in zip(x, z):
                pp, nn = _head_and_normal(shape, ua, np.full_like(ua, _v_of_z(shape, np.array([zi]))[0]))
                mask = pp[:, 1] > 0
                pp, nn = pp[mask], nn[mask]
                k = np.argmin(np.abs(pp[:, 0] - xi * s))
                res.append(pp[k] + nn[k] * BROWS[1] * thickness_mul * s)
            return np.array(res)

        m = surface(fn, nu, nw, uv_fn=lambda U, V, P: np.stack([U, V], axis=1))
        m.v = m.v + eye_world
        # Oriente vers l'avant.
        a, b, c = m.v[m.f[:, 0]], m.v[m.f[:, 1]], m.v[m.f[:, 2]]
        if np.sum(np.cross(b - a, c - a)[:, 1]) < 0:
            m = m.flipped()
        out.append(m)
    return merge(out)
