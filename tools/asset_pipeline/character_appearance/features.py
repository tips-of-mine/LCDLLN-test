"""Traits raciaux paramétriques : cornes et queue (démons), ailes repliées
(démons), défenses (orcs).

Repère : espace Blender (mètres, Z haut, +Y avant), relatif au corps de base
importé tel quel. Les cornes et défenses sont liées à l'os ``Head``, la queue
à ``pelvis``, les ailes à ``spine_03``.

Matériaux :
* cornes : ``MI_Horn`` (texture corne) ; défenses : ``MI_Tusk`` (ivoire) ;
* queue et membrane des ailes : peau du corps (``MI_Regular_<Genre>``), les
  ailes ont donc toujours la couleur de la peau (décision du propriétaire),
  sans plumes.
"""

from __future__ import annotations

import numpy as np

from geom import Mesh, bezier, merge, orient_outward, surface, tube
from heads import HeadShape, plain_skin_uv

HORN_MATERIAL = "MI_Horn"
TUSK_MATERIAL = "MI_Tusk"


def _skull_anchor(shape: HeadShape, eye_world: np.ndarray, x: float, y: float, z: float) -> np.ndarray:
    s = shape.scale
    return eye_world + np.array([x * s, (shape.center_y + y) * s, z * s])


# ----------------------------------------------------------------------
# Cornes
# ----------------------------------------------------------------------
HORN_STYLES = {
    # id: (nom, points de contrôle relatifs à la base (côté droit, m), rayon de base)
    "horns_ram": ("Bélier", [(0.0, 0.0, 0.0), (0.035, -0.035, 0.050), (0.075, -0.095, 0.015),
                             (0.070, -0.060, -0.060), (0.055, 0.000, -0.040)], 0.019),
    "horns_back": ("Balayées en arrière", [(0.0, 0.0, 0.0), (0.015, -0.050, 0.045), (0.035, -0.140, 0.070),
                                           (0.045, -0.230, 0.060)], 0.016),
    "horns_up": ("Dressées", [(0.0, 0.0, 0.0), (0.050, 0.005, 0.030), (0.070, 0.010, 0.110),
                              (0.040, -0.010, 0.170)], 0.017),
    "horns_short": ("Courtes", [(0.0, 0.0, 0.0), (0.012, -0.010, 0.030), (0.018, -0.030, 0.055)], 0.013),
}


def build_horns(style_id: str, shape: HeadShape, eye_world: np.ndarray, lod: int) -> Mesh:
    """Paire de cornes plantées sur le haut du front."""
    _name, ctrl, r0 = HORN_STYLES[style_id]
    samples, sides = [(28, 12), (16, 8), (8, 6)][lod]
    out = []
    for side in (1.0, -1.0):
        base = _skull_anchor(shape, eye_world, side * 0.040, 0.050, 0.072)
        pts = np.array([base + np.array([side * c[0], c[1], c[2]]) * shape.scale for c in ctrl])
        # La base est enfoncée dans le crâne pour éviter tout jour.
        pts = np.vstack([base - np.array([side * 0.006, 0.0, 0.012]), pts])
        path = bezier(pts, samples)

        def radius(t, r0=r0):
            return r0 * shape.scale * (1 - 0.92 * t ** 0.9) * (1 + 0.06 * np.sin(t * 60)) + 0.0008

        m = tube(path, radius, sides, cap_end=True, flatten=0.85, uv_scale=(1.0, 1.0))
        out.append(m)
    return merge(out)


# ----------------------------------------------------------------------
# Défenses (orcs)
# ----------------------------------------------------------------------
TUSK_STYLES = {
    "tusks_small": ("Petites", [(0.0, 0.0, 0.0), (0.002, 0.004, 0.012), (0.004, 0.002, 0.020)], 0.0050),
    "tusks_long": ("Longues", [(0.0, 0.0, 0.0), (0.004, 0.008, 0.020), (0.010, 0.006, 0.040),
                               (0.016, -0.002, 0.050)], 0.0065),
    "tusks_wide": ("Écartées", [(0.0, 0.0, 0.0), (0.012, 0.006, 0.014), (0.026, 0.002, 0.028),
                                (0.034, -0.006, 0.034)], 0.0060),
}


def build_tusks(style_id: str, shape: HeadShape, eye_world: np.ndarray, lod: int) -> Mesh:
    """Défenses sortant de la mâchoire inférieure, de part et d'autre de la bouche."""
    from heads import head_point
    _name, ctrl, r0 = TUSK_STYLES[style_id]
    samples, sides = [(14, 8), (9, 6), (5, 5)][lod]
    out = []
    for side in (1.0, -1.0):
        # Point de surface au coin de la bouche.
        ua = np.linspace(-0.2, 0.2, 401) % 1.0
        v = (shape.z[0] - (-0.064)) / (shape.z[0] - shape.z[-1])
        pp = head_point(shape, ua, np.full_like(ua, v))
        pp = pp[pp[:, 1] > 0]
        k = np.argmin(np.abs(pp[:, 0] - side * 0.024 * shape.scale))
        base = eye_world + pp[k] + np.array([0.0, -0.006, 0.0])
        pts = np.array([base + np.array([side * c[0], c[1], c[2]]) * shape.scale for c in ctrl])
        path = bezier(pts, samples)
        out.append(tube(path, lambda t: r0 * shape.scale * (1 - 0.85 * t ** 1.3) + 0.0005, sides,
                        cap_end=True))
    return merge(out)


# ----------------------------------------------------------------------
# Queue (démons)
# ----------------------------------------------------------------------
TAIL_LENGTHS = {
    "tail_short": ("Courte", 0.45),
    "tail_medium": ("Moyenne", 0.80),
    "tail_long": ("Longue", 1.15),
}


def build_tail(style_id: str, root: np.ndarray, lod: int, scale: float = 1.0) -> Mesh:
    """Queue partant du bas du dos, terminée par une pointe en fer de lance.

    ``root`` : point d'attache (espace Blender) sur le bas du dos.
    """
    _name, length = TAIL_LENGTHS[style_id]
    L = length * scale
    samples, sides = [(40, 10), (22, 8), (12, 6)][lod]
    # Trajet : vers l'arrière puis vers le bas, ondulation en S, sans toucher le sol.
    drop = min(0.55 * L, root[2] - 0.18)
    ctrl = np.array([
        root + np.array([0.0, 0.03, 0.0]),
        root + np.array([0.0, -0.20 * L, -0.05 * L]),
        root + np.array([0.06 * L, -0.45 * L, -drop * 0.8]),
        root + np.array([-0.05 * L, -0.70 * L, -drop]),
        root + np.array([0.02 * L, -0.85 * L, -drop * 0.75]),
    ])
    path = bezier(ctrl, samples)
    body = tube(path, lambda t: 0.026 * scale * (1 - 0.78 * t) + 0.004, sides, cap_end=False)
    body.uv = plain_skin_uv(body.uv * 0.4 + 0.3)
    # Pointe : losange aplati.
    tip_dir = path[-1] - path[-3]
    tip_dir /= np.linalg.norm(tip_dir)
    side_dir = np.cross(tip_dir, np.array([0.0, 0.0, 1.0]))
    if np.linalg.norm(side_dir) < 1e-6:
        side_dir = np.array([1.0, 0.0, 0.0])
    side_dir /= np.linalg.norm(side_dir)
    up = np.cross(side_dir, tip_dir)
    c0 = path[-1]
    w = 0.035 * scale
    h = 0.075 * scale
    th = 0.006 * scale
    verts = np.array([c0, c0 + tip_dir * h * 0.35 + side_dir * w, c0 + tip_dir * h,
                      c0 + tip_dir * h * 0.35 - side_dir * w,
                      c0 + tip_dir * h * 0.4 + up * th, c0 + tip_dir * h * 0.4 - up * th])
    faces = np.array([[0, 1, 4], [1, 2, 4], [2, 3, 4], [3, 0, 4],
                      [0, 5, 1], [1, 5, 2], [2, 5, 3], [3, 5, 0]])
    tip = Mesh(verts, faces, plain_skin_uv(np.full((6, 2), 0.5)))
    tip = orient_outward(tip, c0 + tip_dir * h * 0.4)
    return merge([body, tip])


def _skin(m: Mesh) -> Mesh:
    """Ramène les UV paramétriques d'une pièce dans la zone de peau unie."""
    m.uv = plain_skin_uv(np.clip(m.uv, 0.0, 1.0) * 0.5 + 0.25)
    return m


# ----------------------------------------------------------------------
# Ailes repliées (démons)
# ----------------------------------------------------------------------
WING_STYLES = {
    # id: (nom, hauteur au-dessus de l'épaule, largeur repliée, chute, nb de doigts, épines)
    "wings_bat": ("Chauve-souris", 0.22, 0.30, 0.55, 4, False),
    "wings_spiked": ("Griffues", 0.34, 0.32, 0.60, 4, True),
    "wings_small": ("Petites", 0.12, 0.20, 0.36, 3, False),
}


def build_wings(style_id: str, root_l: np.ndarray, lod: int, scale: float = 1.0) -> Mesh:
    """Paire d'ailes de chauve-souris repliées dans le dos.

    ``root_l`` : point d'attache gauche (x > 0) sur l'omoplate ; la droite est
    obtenue par symétrie. Os de l'aile = tubes ; membrane = surface entre les
    doigts (double face : visible des deux côtés).
    """
    _name, rise, width, fall, fingers, spikes = WING_STYLES[style_id]
    samples, sides, nm = [(16, 7, 10), (10, 5, 6), (6, 4, 4)][lod]
    out = []
    for side in (1.0, -1.0):
        root = root_l * np.array([side, 1.0, 1.0])
        wrist = root + np.array([side * width * 0.55, -0.10, rise]) * scale
        arm = bezier(np.array([root, root + np.array([side * 0.06, -0.08, rise * 0.6]) * scale, wrist]), samples)
        out.append(_skin(tube(arm, lambda t: 0.016 * scale * (1 - 0.4 * t) + 0.003, sides, cap_end=True)))
        tips = []
        for f in range(fingers):
            a = f / max(fingers - 1, 1)
            tip = wrist + np.array([side * (width * (0.35 - 0.55 * a)), -0.06 - 0.03 * a,
                                    -fall * (0.55 + 0.45 * a)]) * scale
            mid = (wrist + tip) / 2 + np.array([side * 0.04, -0.04, 0.0]) * scale
            fpath = bezier(np.array([wrist, mid, tip]), samples)
            out.append(_skin(tube(fpath, lambda t: 0.008 * scale * (1 - 0.7 * t) + 0.002, max(sides - 2, 4),
                                  cap_end=True)))
            tips.append(fpath)
        if spikes:
            sp = bezier(np.array([wrist, wrist + np.array([side * 0.02, -0.02, 0.06]) * scale,
                                  wrist + np.array([side * 0.01, -0.05, 0.12]) * scale]), samples // 2 + 2)
            out.append(_skin(tube(sp, lambda t: 0.010 * scale * (1 - 0.95 * t) + 0.0006, max(sides - 2, 4),
                                  cap_end=True)))
        # Membrane : entre le bras (root -> wrist) et chaque doigt, puis entre doigts.
        edges = [np.vstack([bezier(np.array([root, wrist]), samples)])] + tips
        # Bord de fuite : du dernier doigt jusqu'à la base, le long du dos.
        back_edge = bezier(np.array([tips[-1][-1], root + np.array([side * 0.02, -0.02, -fall * 0.65]) * scale,
                                     root]), samples)
        edges.append(back_edge[::-1])
        for e0, e1 in zip(edges[:-1], edges[1:]):
            def fn(U, V, e0=e0, e1=e1):
                i0 = np.clip((U * (len(e0) - 1)).astype(int), 0, len(e0) - 1)
                i1 = np.clip((U * (len(e1) - 1)).astype(int), 0, len(e1) - 1)
                p = e0[i0] * (1 - V)[:, None] + e1[i1] * V[:, None]
                # Légère courbure (membrane tendue vers l'arrière).
                sag = np.sin(np.pi * V) * np.sin(np.pi * U) * 0.025 * scale
                return p + np.array([0.0, -1.0, 0.0]) * sag[:, None]

            mem = surface(fn, nm, max(nm // 2, 3), uv_fn=lambda U, V, P: plain_skin_uv(np.stack([U, V], 1) * 0.5 + 0.25))
            out.append(mem)
            out.append(mem.flipped())
    return merge(out)
