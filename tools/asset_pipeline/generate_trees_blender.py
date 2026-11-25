# -*- coding: utf-8 -*-
"""Arbres « dark fantasy » procéduraux pour LCDLLN (Blender 5.1, sans add-on).

Génère, de façon DÉTERMINISTE (graine fixe par arbre), 8 arbres réutilisables
dans le style sombre des BD (troncs noueux, couleurs éteintes) :

  - DarkOak_1, DarkOak_2               chênes et feuillus (plaines, forêts)
  - DarkPine_1, DarkPine_2             conifères (montagnes, nord, > 900 m)
  - GnarledDeadTree_1, _2              arbres morts et tordus (landes, terres maudites)
  - MarshWillow_1, MarshWillow_2       saules de marais (rivières, lagunes)

Chaque arbre existe en 3 niveaux de détail :
  - LOD0 : game/data/meshes/props/<Nom>.gltf           (proche, détail complet)
  - LOD1 : game/data/meshes/props/<Nom>_LOD1.gltf  (moyen : moins de côtés,
           brindilles supprimées, cartes de feuillage plus grandes et moins nombreuses)
  - LOD2 : game/data/meshes/props/<Nom>_LOD2.gltf  (lointain : tronc et
           charpentières seules, une douzaine de grandes cartes de feuillage)
Le « billboard » (impostor octaédrique `.mipo`) est cuit à partir du LOD0 par
`tools/impostor_builder` (voir tools/asset_pipeline/README.md).

Les textures (écorce + normal map, cartes de feuillage RGBA) sont, elles aussi,
générées par ce script (bruit procédural NumPy) : aucune image externe, aucune
licence tierce. Unités : mètres, Y vers le haut dans le glTF (Z en haut dans Blender).

Usage (sortie dans le dépôt) :
    blender --background --python tools/asset_pipeline/generate_trees_blender.py
    blender --background --python tools/asset_pipeline/generate_trees_blender.py -- --only DarkOak_1
    blender --background --python tools/asset_pipeline/generate_trees_blender.py -- --out <dossier>
Aperçu dans un Blender ouvert (construit les arbres dans la scène, sans export) :
    exec(open(r"<dépôt>/tools/asset_pipeline/generate_trees_blender.py").read(),
         {"__name__": "__main__", "LCDLLN_TREES_PREVIEW": True})

Effets de bord : écrit les .gltf/.bin/.png sous le dossier de sortie ; en mode
aperçu, ajoute des collections « Arbres LCDLLN » à la scène courante.
"""

import json
import math
import os
import random
import sys

import bmesh
import bpy
import numpy as np
from mathutils import Matrix, Vector

# ---------------------------------------------------------------------------
# Paramètres des espèces
# ---------------------------------------------------------------------------

#: Une entrée par arbre exporté : (nom, espèce, graine).
TREES = [
    ("DarkOak_1", "oak", 11),
    ("DarkOak_2", "oak", 23),
    ("DarkPine_1", "pine", 31),
    ("DarkPine_2", "pine", 47),
    ("GnarledDeadTree_1", "dead", 53),
    ("GnarledDeadTree_2", "dead", 67),
    ("MarshWillow_1", "willow", 71),
    ("MarshWillow_2", "willow", 89),
]

#: Matériaux (écorce, feuillage) par espèce ; le feuillage est None pour l'arbre mort.
SPECIES_MATERIALS = {
    "oak": ("Bark_DarkOak", "Leaves_DarkOak"),
    "pine": ("Bark_DarkPine", "Leaves_DarkPine"),
    "dead": ("Bark_DeadTree_Dark", None),
    "willow": ("Bark_MarshWillow", "Leaves_MarshWillow"),
}

#: Nombre de côtés des tubes par niveau de branche, pour chaque LOD.
SIDES = {0: [12, 8, 6, 5, 4], 1: [7, 5, 4, 3, 3], 2: [5, 4, 3, 3, 3]}

#: Niveau de branche maximal conservé par LOD (0 = tronc).
MAX_LEVEL = {0: 9, 1: 2, 2: 1}

#: Budget de triangles (contrôle de fin de génération) par LOD.
TRI_BUDGET = {0: 26000, 1: 9000, 2: 2500}


class Node:
    """Point d'une branche : position (m), rayon (m)."""

    __slots__ = ("pos", "radius")

    def __init__(self, pos, radius):
        self.pos = pos
        self.radius = radius


class Branch:
    """Branche du squelette : suite de nœuds, niveau (0 = tronc), parent, enfants."""

    def __init__(self, level, nodes, parent=None):
        self.level = level
        self.nodes = nodes
        self.parent = parent
        self.children = []
        self.leaf_points = []   # (position, direction) des grappes de feuillage
        self.hanging = []       # saule : (position, longueur) des rideaux pendants


# ---------------------------------------------------------------------------
# Croissance du squelette (commun à tous les LOD : même graine = même arbre)
# ---------------------------------------------------------------------------

def _perp(v):
    """Vecteur unitaire perpendiculaire à \\p v (quelconque, stable)."""
    a = Vector((0, 0, 1)) if abs(v.z) < 0.9 else Vector((1, 0, 0))
    return v.cross(a).normalized()


def _rotate(v, axis, angle):
    """Rotation de \\p v autour de \\p axis (unitaire) d'un angle en radians."""
    return Matrix.Rotation(angle, 3, axis) @ v


def _grow_branch(rng, start, direction, length, radius, level, params, parent):
    """Fait pousser une branche (nœuds) puis, récursivement, ses enfants.

    \\param start      origine (m)
    \\param direction  direction initiale (unitaire)
    \\param length     longueur totale (m)
    \\param radius     rayon à la base (m)
    \\param level      0 = tronc, 1 = charpentière, ...
    \\param params     dictionnaire de l'espèce (voir SPECIES)
    \\return la branche créée
    """
    segs = max(3, int(params["segments"][min(level, len(params["segments"]) - 1)]))
    gnarl = params["gnarl"][min(level, len(params["gnarl"]) - 1)]
    nodes = [Node(start.copy(), radius)]
    pos = start.copy()
    d = direction.normalized()
    seg_len = length / segs
    tip_ratio = params["tip_ratio"]
    for i in range(1, segs + 1):
        # Noueux : petite déviation aléatoire ; gravité / phototropisme selon l'espèce.
        jitter = Vector((rng.uniform(-1, 1), rng.uniform(-1, 1), rng.uniform(-1, 1))) * gnarl
        lift = Vector((0, 0, params["lift"][min(level, len(params["lift"]) - 1)]))
        d = (d + jitter + lift).normalized()
        pos = pos + d * seg_len
        t = i / segs
        r = radius * (1.0 - t * (1.0 - tip_ratio))
        nodes.append(Node(pos.copy(), max(r, 0.012)))
    br = Branch(level, nodes, parent)

    max_level = params["levels"]
    if level < max_level:
        n_child = params["children"][level]
        golden = 2.39996323
        phase = rng.uniform(0, math.tau)
        for k in range(n_child):
            t = params["child_t"][0] + (params["child_t"][1] - params["child_t"][0]) * ((k + rng.uniform(0.1, 0.9)) / n_child)
            idx = min(len(nodes) - 2, int(t * (len(nodes) - 1)))
            frac = t * (len(nodes) - 1) - idx
            a, b = nodes[idx], nodes[idx + 1]
            p = a.pos.lerp(b.pos, frac)
            pr = a.radius + (b.radius - a.radius) * frac
            axis = (b.pos - a.pos).normalized()
            side = _rotate(_perp(axis), axis, phase + golden * k)
            pitch = math.radians(rng.uniform(*params["pitch"][level]))
            cd = (axis * math.cos(pitch) + side * math.sin(pitch)).normalized()
            # Conifère : branches longues en bas, courtes au sommet (silhouette conique).
            clen = length * params["child_len"][level] * ((1.08 - t) * rng.uniform(0.85, 1.1) if params.get("cone") and level == 0
                                                          else rng.uniform(0.75, 1.1))
            crad = min(pr * 0.82, radius * params["child_rad"][level])
            if clen < 0.25 or crad < 0.01:
                continue
            br.children.append(_grow_branch(rng, p, cd, clen, crad, level + 1, params, br))

    # Feuillage aux extrémités des dernières branches.
    if params["leaves"] and level >= max_level - 1:
        n = params["leaf_per_branch"] if level == max_level else params["leaf_per_branch"] // 2
        for _ in range(n):
            t = rng.uniform(0.45, 1.0)
            idx = min(len(nodes) - 2, int(t * (len(nodes) - 1)))
            p = nodes[idx].pos.lerp(nodes[idx + 1].pos, t * (len(nodes) - 1) - idx)
            br.leaf_points.append((p, (nodes[-1].pos - nodes[0].pos).normalized()))
    if params.get("hanging") and level >= 1:
        for _ in range(params["hanging"]):
            t = rng.uniform(0.35, 1.0)
            idx = min(len(nodes) - 2, int(t * (len(nodes) - 1)))
            p = nodes[idx].pos.lerp(nodes[idx + 1].pos, t * (len(nodes) - 1) - idx)
            hang = min(p.z - 0.4, rng.uniform(*params["hang_len"]))
            if hang > 0.8:
                br.hanging.append((p, hang))
    return br


def build_skeleton(species, seed):
    """Construit le squelette complet d'un arbre (tronc, racines, branches).

    \\param species  « oak », « pine », « dead » ou « willow »
    \\param seed     graine (même graine = même arbre, pour tous les LOD)
    \\return (tronc, liste des racines)
    """
    rng = random.Random(seed)
    p = SPECIES[species]
    lean = Vector((rng.uniform(-1, 1), rng.uniform(-1, 1), 0)) * p["lean"]
    trunk_dir = (Vector((0, 0, 1)) + lean).normalized()
    height = rng.uniform(*p["height"])
    radius = rng.uniform(*p["trunk_radius"])
    trunk = _grow_branch(rng, Vector((0, 0, -0.25)), trunk_dir, height, radius, 0, p, None)
    roots = []
    for k in range(p["roots"]):
        a = math.tau * k / p["roots"] + rng.uniform(-0.3, 0.3)
        d = Vector((math.cos(a), math.sin(a), -0.35)).normalized()
        r_rng = random.Random(seed * 1000 + k)
        root_p = dict(p, levels=0, leaves=False, hanging=0, lift=[0.06], gnarl=[0.18], segments=[4])
        roots.append(_grow_branch(r_rng, Vector((0, 0, 0.55 * radius)), d, radius * rng.uniform(2.6, 3.6),
                                  radius * 0.55, 0, root_p, None))
    return trunk, roots


SPECIES = {
    # Chêne : tronc court et massif, couronne large et noueuse.
    "oak": dict(height=(8.5, 10.5), trunk_radius=(0.6, 0.75), lean=0.08, levels=3, roots=6,
                segments=[7, 6, 5, 4], gnarl=[0.10, 0.22, 0.28, 0.3], lift=[0.04, 0.03, 0.03, 0.02],
                tip_ratio=0.3, children=[7, 5, 4], child_t=(0.28, 0.95),
                pitch=[(50, 80), (35, 70), (30, 65)], child_len=[0.72, 0.55, 0.5],
                child_rad=[0.55, 0.55, 0.55], leaves=True, leaf_per_branch=12, leaf_size=(1.6, 2.4)),
    # Pin : fût droit et élancé, verticilles de branches de plus en plus courtes vers le haut.
    "pine": dict(height=(15.0, 19.0), trunk_radius=(0.32, 0.42), lean=0.03, levels=2, roots=4,
                 segments=[12, 5, 3], gnarl=[0.03, 0.1, 0.18], lift=[0.0, -0.05, 0.0],
                 tip_ratio=0.06, children=[34, 5], child_t=(0.16, 0.98), cone=True,
                 pitch=[(75, 105), (40, 70)], child_len=[0.42, 0.42], child_rad=[0.34, 0.5],
                 leaves=True, leaf_per_branch=9, leaf_size=(1.3, 1.8)),
    # Arbre mort : tronc tordu, branches cassées et anguleuses, aucun feuillage.
    "dead": dict(height=(8.0, 10.5), trunk_radius=(0.42, 0.56), lean=0.22, levels=3, roots=5,
                 segments=[8, 6, 5, 4], gnarl=[0.22, 0.35, 0.4, 0.42], lift=[0.02, 0.06, 0.05, 0.03],
                 tip_ratio=0.2, children=[5, 3, 3], child_t=(0.32, 0.92),
                 pitch=[(35, 75), (30, 70), (25, 65)], child_len=[0.55, 0.5, 0.45],
                 child_rad=[0.45, 0.5, 0.55], leaves=False, leaf_per_branch=0, leaf_size=(1, 1)),
    # Saule de marais : tronc court penché, branches arquées, rideaux de feuilles pendants.
    "willow": dict(height=(6.5, 8.5), trunk_radius=(0.4, 0.52), lean=0.18, levels=2, roots=6,
                   segments=[6, 7, 5], gnarl=[0.12, 0.12, 0.15], lift=[0.05, -0.05, -0.08],
                   tip_ratio=0.3, children=[7, 4], child_t=(0.45, 0.98),
                   pitch=[(35, 60), (25, 55)], child_len=[0.75, 0.55], child_rad=[0.5, 0.5],
                   leaves=True, leaf_per_branch=8, leaf_size=(1.2, 1.7), hanging=6, hang_len=(2.5, 4.5)),
}


# ---------------------------------------------------------------------------
# Textures procédurales (NumPy)
# ---------------------------------------------------------------------------

def _fbm(h, w, rng, octaves=5, scale=(4, 4)):
    """Bruit fractal périodique (tuilable) h×w dans [0, 1], par sommes de sinusoïdes aléatoires."""
    y = np.linspace(0, 2 * np.pi, h, endpoint=False)[:, None]
    x = np.linspace(0, 2 * np.pi, w, endpoint=False)[None, :]
    out = np.zeros((h, w), np.float32)
    amp = 1.0
    for o in range(octaves):
        fy, fx = scale[0] * (2 ** o), scale[1] * (2 ** o)
        for _ in range(4):
            ky = rng.randint(1, max(1, int(fy)))
            kx = rng.randint(0, max(1, int(fx)))
            ph = rng.uniform(0, math.tau)
            out += amp * np.sin(ky * y + kx * x + ph).astype(np.float32)
        amp *= 0.55
    out -= out.min()
    out /= max(1e-6, out.max())
    return out


def _bark_texture(name, seed, base, moss, stretch):
    """Écorce tuilable 512×1024 (couleur sRGB) + normal map dérivée de la hauteur.

    \\param base     couleur moyenne (r, g, b) dans [0, 1]
    \\param moss     intensité de mousse/lichen verdâtre
    \\param stretch  allongement vertical des fibres
    \\return (pixels couleur RGBA, pixels normale RGBA), tableaux float32 h×w×4
    """
    rng = random.Random(seed)
    h, w = 1024, 512
    fibers = _fbm(h, w, rng, octaves=5, scale=(2, 10 * stretch))
    cracks = _fbm(h, w, rng, octaves=3, scale=(3, 6))
    cracks = np.clip((cracks - 0.55) * 4.0, 0, 1) ** 1.5
    height = np.clip(0.75 * fibers - 0.6 * cracks + 0.25, 0, 1)
    patches = _fbm(h, w, rng, octaves=3, scale=(2, 2))
    moss_mask = np.clip((patches - 0.62) * 3.0, 0, 1) * moss
    col = np.zeros((h, w, 4), np.float32)
    shade = 0.45 + 0.75 * height
    for c in range(3):
        col[..., c] = base[c] * shade
    moss_col = np.array([0.18, 0.22, 0.10], np.float32)
    col[..., :3] = col[..., :3] * (1 - moss_mask[..., None]) + moss_col * moss_mask[..., None] * (0.6 + 0.6 * height[..., None])
    col[..., 3] = 1.0
    gy, gx = np.gradient(height * 6.0)
    n = np.stack([-gx, -gy, np.ones_like(gx)], -1)
    n /= np.linalg.norm(n, axis=-1, keepdims=True)
    nrm = np.concatenate([n * 0.5 + 0.5, np.ones((h, w, 1), np.float32)], -1)
    return np.clip(col, 0, 1), nrm.astype(np.float32)


def _draw_ellipse(img, cx, cy, rx, ry, ang, color, lobes=0):
    """Dessine une feuille (ellipse orientée, bord lobé optionnel) dans \\p img (RGBA float)."""
    h, w = img.shape[:2]
    r = int(max(rx, ry)) + 2
    x0, x1 = max(0, int(cx - r)), min(w, int(cx + r))
    y0, y1 = max(0, int(cy - r)), min(h, int(cy + r))
    if x0 >= x1 or y0 >= y1:
        return
    yy, xx = np.mgrid[y0:y1, x0:x1].astype(np.float32)
    dx, dy = xx - cx, yy - cy
    ca, sa = math.cos(ang), math.sin(ang)
    u = (dx * ca + dy * sa) / rx
    v = (-dx * sa + dy * ca) / ry
    rad = np.sqrt(u * u + v * v)
    if lobes:
        theta = np.arctan2(v, u)
        rad = rad / (1.0 + 0.16 * np.cos(lobes * theta))
    m = rad <= 1.0
    shade = (0.75 + 0.35 * (1.0 - rad)) * (0.9 + 0.2 * (u > 0))
    for c in range(3):
        img[y0:y1, x0:x1, c] = np.where(m, color[c] * shade, img[y0:y1, x0:x1, c])
    img[y0:y1, x0:x1, 3] = np.where(m, 1.0, img[y0:y1, x0:x1, 3])


def _draw_line(img, x0, y0, x1, y1, width, color):
    """Trait épais (brindille, aiguille) dans \\p img (RGBA float)."""
    n = int(max(abs(x1 - x0), abs(y1 - y0))) + 1
    for i in range(n + 1):
        t = i / max(1, n)
        _draw_ellipse(img, x0 + (x1 - x0) * t, y0 + (y1 - y0) * t, width, width, 0.0, color)


def _leaf_card(species, seed):
    """Carte de feuillage 1024×1024 RGBA (fond transparent) pour une espèce.

    Chêne : rameau de feuilles lobées ; pin : rameau d'aiguilles ; saule : rideau
    de feuilles étroites (carte haute). Couleurs volontairement éteintes (olive,
    vert-gris) comme les forêts des BD.
    """
    rng = random.Random(seed)
    S = 1024
    img = np.zeros((S, S, 4), np.float32)
    twig = (0.16, 0.12, 0.09)
    if species == "oak":
        greens = [(0.24, 0.31, 0.12), (0.31, 0.35, 0.15), (0.21, 0.27, 0.13), (0.36, 0.36, 0.17)]
        stems = []
        for k in range(5):
            a = math.radians(-90 + rng.uniform(-60, 60))
            x0, y0 = S * 0.5, S * 0.97
            x1, y1 = x0 + math.cos(a) * S * 0.45, y0 + math.sin(a) * S * 0.85
            _draw_line(img, x0, y0, x1, y1, 4, twig)
            stems.append((x0, y0, x1, y1))
        for _ in range(110):
            x0, y0, x1, y1 = stems[rng.randrange(len(stems))]
            t = rng.uniform(0.25, 1.0)
            cx, cy = x0 + (x1 - x0) * t + rng.uniform(-60, 60), y0 + (y1 - y0) * t + rng.uniform(-60, 60)
            _draw_ellipse(img, cx, cy, rng.uniform(34, 52), rng.uniform(18, 26), rng.uniform(0, math.pi),
                          rng.choice(greens), lobes=7)
    elif species == "pine":
        greens = [(0.10, 0.17, 0.11), (0.13, 0.20, 0.12), (0.09, 0.14, 0.10)]
        _draw_line(img, S * 0.5, S * 0.98, S * 0.5, S * 0.05, 6, twig)
        for _ in range(9):
            yb = rng.uniform(S * 0.2, S * 0.9)
            side = rng.choice((-1, 1))
            xe, ye = S * 0.5 + side * rng.uniform(S * 0.22, S * 0.42), yb - rng.uniform(40, 160)
            _draw_line(img, S * 0.5, yb, xe, ye, 3, twig)
            for k in range(36):
                t = k / 36
                px, py = S * 0.5 + (xe - S * 0.5) * t, yb + (ye - yb) * t
                for s in (-1, 1):
                    a = math.atan2(ye - yb, xe - S * 0.5) + s * math.radians(rng.uniform(35, 60))
                    _draw_line(img, px, py, px + math.cos(a) * 78, py + math.sin(a) * 78, 3.2, rng.choice(greens))
        for k in range(70):
            t = k / 70
            py = S * 0.98 + (S * 0.05 - S * 0.98) * t
            for s in (-1, 1):
                a = -math.pi / 2 + s * math.radians(rng.uniform(40, 70))
                _draw_line(img, S * 0.5, py, S * 0.5 + math.cos(a) * 85, py + math.sin(a) * 85, 3.2, rng.choice(greens))
    else:  # willow : rideau pendant, carte haute (bas = extrémité)
        greens = [(0.22, 0.27, 0.12), (0.27, 0.30, 0.15), (0.19, 0.24, 0.12)]
        for k in range(7):
            x = S * (0.12 + 0.76 * k / 6) + rng.uniform(-20, 20)
            _draw_line(img, x, S * 0.02, x + rng.uniform(-40, 40), S * 0.98, 2.5, twig)
            for j in range(26):
                t = (j + rng.uniform(0, 1)) / 26
                cy = S * 0.02 + t * S * 0.96
                for s in (-1, 1):
                    _draw_ellipse(img, x + s * rng.uniform(10, 26), cy, 24, 7, math.radians(90 + s * rng.uniform(15, 35)),
                                  rng.choice(greens))
    # Les texels transparents prennent la couleur moyenne (évite les franges sombres au filtrage).
    mask = img[..., 3] > 0
    if mask.any():
        mean = img[mask][:, :3].mean(0)
        img[~mask, :3] = mean
    return np.clip(img, 0, 1)


def _make_image(name, pixels, colorspace):
    """Crée (ou remplace) une image Blender à partir de pixels float h×w×4 (bas en premier)."""
    old = bpy.data.images.get(name)
    if old:
        bpy.data.images.remove(old)
    h, w = pixels.shape[:2]
    im = bpy.data.images.new(name, w, h, alpha=True)
    im.colorspace_settings.name = colorspace
    im.pixels.foreach_set(np.ascontiguousarray(pixels[::-1]).ravel())
    im.file_format = "PNG"
    im.pack()
    return im


_TEX_CACHE = {}


def make_materials(species):
    """Matériaux glTF (Principled BSDF) de l'espèce, textures comprises (mis en cache).

    \\return (matériau écorce, matériau feuillage ou None)
    """
    if species in _TEX_CACHE:
        return _TEX_CACHE[species]
    bark_name, leaf_name = SPECIES_MATERIALS[species]
    bark_cfg = {
        "oak": ((0.20, 0.16, 0.12), 0.8, 1.0),
        "pine": ((0.24, 0.15, 0.11), 0.3, 1.6),
        "dead": ((0.23, 0.22, 0.20), 0.15, 1.2),
        "willow": ((0.21, 0.19, 0.14), 1.0, 1.3),
    }[species]
    seed = sum(map(ord, bark_name))
    col, nrm = _bark_texture(bark_name, seed, *bark_cfg)
    bark = _material(bark_name, _make_image(bark_name, col, "sRGB"),
                     _make_image(bark_name + "_Normal", nrm, "Non-Color"), alpha=False)
    leaves = None
    if leaf_name:
        card = _leaf_card(species, sum(map(ord, leaf_name)))
        leaves = _material(leaf_name, _make_image(leaf_name, card, "sRGB"), None, alpha=True)
    _TEX_CACHE[species] = (bark, leaves)
    return bark, leaves


def _material(name, base_img, normal_img, alpha):
    """Matériau Principled BSDF : couleur de base, normal map optionnelle, découpe alpha."""
    m = bpy.data.materials.get(name) or bpy.data.materials.new(name)
    m.use_nodes = True
    nt = m.node_tree
    nt.nodes.clear()
    out = nt.nodes.new("ShaderNodeOutputMaterial")
    bsdf = nt.nodes.new("ShaderNodeBsdfPrincipled")
    bsdf.inputs["Roughness"].default_value = 0.92 if not alpha else 0.75
    bsdf.inputs["Metallic"].default_value = 0.0
    tex = nt.nodes.new("ShaderNodeTexImage")
    tex.image = base_img
    nt.links.new(tex.outputs["Color"], bsdf.inputs["Base Color"])
    if alpha:
        nt.links.new(tex.outputs["Alpha"], bsdf.inputs["Alpha"])
        m.surface_render_method = "DITHERED"
    if normal_img:
        nt_tex = nt.nodes.new("ShaderNodeTexImage")
        nt_tex.image = normal_img
        nmap = nt.nodes.new("ShaderNodeNormalMap")
        nt.links.new(nt_tex.outputs["Color"], nmap.inputs["Color"])
        nt.links.new(nmap.outputs["Normal"], bsdf.inputs["Normal"])
    nt.links.new(bsdf.outputs["BSDF"], out.inputs["Surface"])
    m.use_backface_culling = False
    return m


# ---------------------------------------------------------------------------
# Maillage
# ---------------------------------------------------------------------------

def _tube(bm, uv_layer, nodes, sides, v_scale):
    """Ajoute un tube fermé au sommet le long de \\p nodes (anneaux de \\p sides côtés).

    UV : u autour (1 tour = 1), v le long (mètres / v_scale) ; continuité u assurée
    par un sommet dupliqué en couture.
    """
    rings = []
    prev_side = None
    vacc = 0.0
    for i, nd in enumerate(nodes):
        if i < len(nodes) - 1:
            axis = (nodes[i + 1].pos - nd.pos).normalized()
        else:
            axis = (nd.pos - nodes[i - 1].pos).normalized()
        if i > 0:
            vacc += (nd.pos - nodes[i - 1].pos).length
        side = _perp(axis) if prev_side is None else (prev_side - axis * prev_side.dot(axis)).normalized()
        prev_side = side
        other = axis.cross(side)
        ring = []
        for s in range(sides + 1):
            a = math.tau * s / sides
            p = nd.pos + (side * math.cos(a) + other * math.sin(a)) * nd.radius
            ring.append((bm.verts.new(p), s / sides, vacc / v_scale))
        rings.append(ring)
    for i in range(len(rings) - 1):
        for s in range(sides):
            a, b = rings[i][s], rings[i][s + 1]
            c, d = rings[i + 1][s + 1], rings[i + 1][s]
            f = bm.faces.new((a[0], b[0], c[0], d[0]))
            for loop, (u, v) in zip(f.loops, ((a[1], a[2]), (b[1], b[2]), (c[1], c[2]), (d[1], d[2]))):
                loop[uv_layer].uv = (u * 2.0, v)
    # Bout fermé (éventail).
    tip = nodes[-1].pos + (nodes[-1].pos - nodes[-2].pos).normalized() * nodes[-1].radius
    tv = bm.verts.new(tip)
    last = rings[-1]
    for s in range(sides):
        f = bm.faces.new((last[s][0], last[s + 1][0], tv))
        for loop, uv in zip(f.loops, ((last[s][1] * 2.0, last[s][2]), (last[s + 1][1] * 2.0, last[s][2]), (0.5, last[s][2] + 0.1))):
            loop[uv_layer].uv = uv


def _card(bm, uv_layer, center, right, up, w, h, anchor_bottom=False):
    """Ajoute une carte (quad) de feuillage : \\p w × \\p h mètres, orientée par \\p right / \\p up."""
    o = center if not anchor_bottom else center + up * (h * 0.5)
    pts = [o - right * (w / 2) - up * (h / 2), o + right * (w / 2) - up * (h / 2),
           o + right * (w / 2) + up * (h / 2), o - right * (w / 2) + up * (h / 2)]
    vs = [bm.verts.new(p) for p in pts]
    f = bm.faces.new(vs)
    for loop, uv in zip(f.loops, ((0, 0), (1, 0), (1, 1), (0, 1))):
        loop[uv_layer].uv = uv
    return f


def _walk(branch):
    """Parcourt le squelette (branche puis descendants)."""
    yield branch
    for c in branch.children:
        yield from _walk(c)


def build_tree_object(name, species, seed, lod):
    """Construit l'objet Blender d'un arbre pour un LOD donné.

    \\param lod  0, 1 ou 2 (voir SIDES / MAX_LEVEL)
    \\return l'objet (maillage à 2 matériaux : écorce, feuillage)
    Effet de bord : crée un objet et un maillage dans bpy.data (non lié à une collection).
    """
    trunk, roots = build_skeleton(species, seed)
    params = SPECIES[species]
    bark, leaves = make_materials(species)
    bm = bmesh.new()
    uv = bm.loops.layers.uv.new("UVMap")
    max_level = MAX_LEVEL[lod]
    bark_faces_end = 0
    for br in [*roots, *_walk(trunk)]:
        if br.level > max_level:
            continue
        sides = SIDES[lod][min(br.level, len(SIDES[lod]) - 1)]
        v_scale = max(0.6, br.nodes[0].radius * math.tau * 1.2)
        _tube(bm, uv, br.nodes, sides, v_scale)
    bark_faces_end = len(bm.faces)

    rng = random.Random(seed * 7 + lod)
    leaf_faces = []
    if leaves:
        pts = []
        for br in _walk(trunk):
            pts.extend(br.leaf_points)
        pts_all = len(pts)
        size = params["leaf_size"]
        crown_c = sum((p for p, _ in pts), Vector()) / max(1, len(pts)) if pts else Vector((0, 0, 5))
        if lod == 1:
            pts = pts[::2]
            size = (size[0] * 1.45, size[1] * 1.45)
        elif lod == 2:
            pts = pts[::max(1, len(pts) // 45)]
            size = (size[0] * 2.6, size[1] * 2.6)
        for p, d in pts:
            s = rng.uniform(*size)
            # La carte regarde vers l'extérieur de la couronne (face avant = normale
            # personnalisée), légèrement vers le haut, avec une part d'aléa : vue de
            # l'extérieur, on voit toujours la face avant, éclairée.
            out = (p - crown_c)
            out.z = out.z * 0.5
            out = out.normalized() if out.length > 1e-3 else Vector((1, 0, 0))
            n = (out + Vector((0, 0, 0.35)) + Vector((rng.uniform(-1, 1), rng.uniform(-1, 1), rng.uniform(-1, 1))) * 0.35).normalized()
            right = _rotate(_perp(n), n, rng.uniform(0, math.tau))
            up = n.cross(right).normalized()
            if p.z - s * 0.5 < 0.3:
                # Carte trop basse (branches basses du pin) : remontée au-dessus du sol.
                p = Vector((p.x, p.y, 0.3 + s * 0.5))
            leaf_faces.append(_card(bm, uv, p, right, up, s, s))
        for br in _walk(trunk):
            if br.level > max_level + 1:
                continue
            for p, hang in br.hanging:
                if lod == 2 and rng.random() > 0.35:
                    continue
                w = 1.2 if lod == 0 else 1.8
                # Rideau vertical, face avant tournée vers l'extérieur de la couronne.
                oh = Vector((p.x - crown_c.x, p.y - crown_c.y, 0))
                oh = oh.normalized() if oh.length > 1e-3 else Vector((1, 0, 0))
                right = Vector((0, 0, 1)).cross(oh).normalized()
                leaf_faces.append(_card(bm, uv, p - Vector((0, 0, hang)), right, Vector((0, 0, 1)), w, hang, anchor_bottom=True))

    me = bpy.data.meshes.new(name)
    bm.to_mesh(me)
    bm.free()
    me.materials.append(bark)
    if leaves:
        me.materials.append(leaves)
        mi = np.zeros(len(me.polygons), np.int32)
        mi[bark_faces_end:] = 1
        me.polygons.foreach_set("material_index", mi)
    # Normales : écorce lisse ; feuillage orienté vers l'extérieur de la couronne (éclairage
    # doux et volumineux, au lieu de cartes plates sombres vues par la tranche).
    for poly in me.polygons:
        poly.use_smooth = True
    if leaves:
        verts = me.vertices
        cz = crown_c if pts_all else Vector((0, 0, 5))  # vecteur nul = normale automatique (écorce)
        normals = []
        for poly in me.polygons:
            for li in poly.loop_indices:
                if poly.index >= bark_faces_end:
                    v = verts[me.loops[li].vertex_index].co - cz
                    v.z = v.z * 0.6 + 0.8 * max(0.0, v.z) + 1.5
                    normals.append(v.normalized())
                else:
                    normals.append(Vector((0, 0, 0)))
        me.normals_split_custom_set(normals)
    obj = bpy.data.objects.new(name, me)
    return obj


# ---------------------------------------------------------------------------
# Export glTF et post-traitement
# ---------------------------------------------------------------------------

def _export(obj, path):
    """Exporte \\p obj seul en glTF séparé (.gltf + .bin + .png voisins, Y en haut)."""
    scene = bpy.context.scene
    coll = bpy.data.collections.new("_export_tmp")
    scene.collection.children.link(coll)
    coll.objects.link(obj)
    for o in bpy.context.view_layer.objects:
        o.select_set(False)
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj
    bpy.ops.export_scene.gltf(filepath=path, export_format="GLTF_SEPARATE", use_selection=True,
                              export_yup=True, export_apply=False, export_materials="EXPORT",
                              export_image_format="AUTO", export_texcoords=True, export_normals=True,
                              export_tangents=False, export_animations=False, export_extras=False)
    coll.objects.unlink(obj)
    bpy.data.collections.remove(coll)


def _postprocess(path, image_prefix):
    """Normalise le .gltf exporté pour le moteur LCDLLN.

    - feuillage : alphaMode BLEND (le moteur le lit et active la découpe alpha,
      cf. tools/asset_pipeline/texture_trees.py), doubleSided ;
    - écorce : doubleSided (comme les arbres Quaternius) ;
    - URIs des images préfixées par \\p image_prefix (vide : les LOD sont rangés à
      côté du LOD0, convention <Nom>_LOD1/_LOD2 lue par le client).
    \\return nombre de triangles
    """
    with open(path, encoding="utf-8") as f:
        g = json.load(f)
    for m in g.get("materials", []):
        m["doubleSided"] = True
        if m["name"].startswith("Leaves_"):
            m["alphaMode"] = "BLEND"
            m.pop("alphaCutoff", None)
        else:
            m.pop("alphaMode", None)
        m.setdefault("pbrMetallicRoughness", {})["metallicFactor"] = 0
    for im in g.get("images", []):
        im["uri"] = image_prefix + os.path.basename(im["uri"])
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        json.dump(g, f, indent=2)
        f.write("\n")
    return sum(g["accessors"][p["indices"]]["count"] // 3 for m in g["meshes"] for p in m["primitives"])


def generate(out_dir, only=None):
    """Génère et exporte tous les arbres (ou \\p only) dans \\p out_dir.

    Écrit <out>/<Nom>.gltf (LOD0) et <out>/<Nom>_LOD1|2.gltf (convention du client :
    niveaux de détail à côté du mesh) ; les textures partagées sont dans <out>/. Vérifie le budget de triangles de chaque LOD.
    \\return liste (nom, lod, triangles)
    """
    os.makedirs(out_dir, exist_ok=True)
    report = []
    for name, species, seed in TREES:
        if only and name not in only:
            continue
        for lod in (0, 1, 2):
            obj = build_tree_object(name if lod == 0 else "%s_LOD%d" % (name, lod), species, seed, lod)
            if lod == 0:
                path = os.path.join(out_dir, name + ".gltf")
            else:
                path = os.path.join(out_dir, "%s_LOD%d.gltf" % (name, lod))
            _export(obj, path)
            tris = _postprocess(path, "")
            assert tris <= TRI_BUDGET[lod], "%s LOD%d : %d triangles > budget %d" % (name, lod, tris, TRI_BUDGET[lod])
            report.append((name, lod, tris))
            me = obj.data
            bpy.data.objects.remove(obj)
            bpy.data.meshes.remove(me)
    return report


def preview(only=None):
    """Construit les arbres (3 LOD côte à côte) dans la scène ouverte, sans export.

    Effet de bord : crée/remplace la collection « Arbres LCDLLN ».
    """
    old = bpy.data.collections.get("Arbres LCDLLN")
    if old:
        for o in list(old.objects):
            bpy.data.objects.remove(o)
        bpy.data.collections.remove(old)
    coll = bpy.data.collections.new("Arbres LCDLLN")
    bpy.context.scene.collection.children.link(coll)
    for i, (name, species, seed) in enumerate(TREES):
        if only and name not in only:
            continue
        for lod in (0, 1, 2):
            obj = build_tree_object(name if lod == 0 else "%s_LOD%d" % (name, lod), species, seed, lod)
            obj.location = (i * 16.0, lod * 18.0, 0.0)
            coll.objects.link(obj)


def _args():
    """Arguments après « -- » : --only A,B et --out <dossier>."""
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    only, out = None, None
    for i, a in enumerate(argv):
        if a == "--only":
            only = set(argv[i + 1].split(","))
        if a == "--out":
            out = argv[i + 1]
    return only, out


if __name__ == "__main__":
    _only, _out = _args()
    if globals().get("LCDLLN_TREES_PREVIEW"):
        preview(_only)
    else:
        repo = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
        target = _out or os.path.join(repo, "game", "data", "meshes", "props")
        for n, l, t in generate(target, _only):
            print("[arbres] %-20s LOD%d %6d triangles" % (n, l, t))
