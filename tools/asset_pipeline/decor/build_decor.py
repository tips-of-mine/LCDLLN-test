# Atelier décors — modélisation procédurale Blender des décors issus de la BD.
#
# Pour chaque entrée de decor_specs.json : construit le LOD0 (géométrie complète),
# LOD1 et LOD2 (mêmes squelette et silhouette, détail réduit), et pour les entrées
# `billboard` un billboard (2 plans croisés) dont la texture est rendue par Cycles
# depuis le LOD0. Exporte en glTF 2.0 (.gltf + .bin) dans game/data/meshes/props/,
# textures T_Decor_* référencées par URI relatif (générées par decor_textures.py).
#
# Conventions (lues dans convert_props_blender.py et le moteur) :
#   1 unité = 1 m ; Blender Z-up -> glTF Y-up (export_yup) ; origine au sol, au
#   centre de la base ; matériaux PBR BaseColor / Normal / ORM (R=AO, G=Rough,
#   B=Metal) ; feuillages en alphaMode MASK (le moteur active la découpe alpha).
#
# Lancement :
#   blender --background --python tools/asset_pipeline/decor/build_decor.py -- [--only ID] [--no-billboard]
#   ou, avec le module bpy : python tools/asset_pipeline/decor/build_decor.py [--only ID]
# Effet de bord : écrit <id>.gltf/.bin, <id>_LOD1/2.gltf/.bin, <id>_BB.gltf/.bin et
# T_Decor_BB_<id>.png dans game/data/meshes/props/.
import json
import math
import os
import random
import sys

import bpy
from mathutils import Matrix, Vector, noise

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, "..", "..", ".."))
PROPS = os.path.join(REPO, "game", "data", "meshes", "props")
SPECS = os.path.join(HERE, "decor_specs.json")


# =========================================================================== mesh
class Part:
    """Géométrie accumulée d'un matériau : sommets, faces, UV par coin, normales
    personnalisées optionnelles par coin (feuillages)."""

    def __init__(self, mat):
        self.mat = mat
        self.v, self.f, self.uv, self.cn = [], [], [], []
        self.smooth = True

    def quad(self, a, b, c, d, uvs, normal=None):
        i = len(self.v)
        self.v += [a, b, c, d]
        self.f.append((i, i + 1, i + 2, i + 3))
        self.uv.append(uvs)
        self.cn.append(None if normal is None else [normal] * 4)

    def tris(self):
        return sum(len(f) - 2 for f in self.f)


def tube(part, pts, radii, sides, v_scale=0.5, cap=False):
    """Tube le long d'une polyligne (anneaux orientés par transport parallèle)."""
    if len(pts) < 2:
        return
    base = len(part.v)
    up = Vector((0, 0, 1))
    prev_n = None
    length = 0.0
    rings = []
    for k, p in enumerate(pts):
        t = (pts[min(k + 1, len(pts) - 1)] - pts[max(k - 1, 0)]).normalized()
        if prev_n is None:
            n = (up.cross(t) if abs(t.dot(up)) < 0.95 else Vector((1, 0, 0)).cross(t)).normalized()
        else:
            n = (prev_n - t * prev_n.dot(t)).normalized()
        prev_n = n
        b = t.cross(n)
        if k > 0:
            length += (pts[k] - pts[k - 1]).length
        ring = []
        for s in range(sides):
            a = 2 * math.pi * s / sides
            ring.append(p + (n * math.cos(a) + b * math.sin(a)) * radii[k])
        rings.append((ring, length))
    for ring, _ in rings:
        part.v += ring
    circ_u = max(1.0, round(2 * math.pi * radii[0] / 0.6))
    for k in range(len(rings) - 1):
        for s in range(sides):
            s2 = (s + 1) % sides
            i0 = base + k * sides + s
            i1 = base + k * sides + s2
            i2 = base + (k + 1) * sides + s2
            i3 = base + (k + 1) * sides + s
            u0, u1 = s / sides * circ_u, (s + 1) / sides * circ_u
            v0, v1 = rings[k][1] * v_scale, rings[k + 1][1] * v_scale
            part.f.append((i0, i1, i2, i3))
            part.uv.append([(u0, v0), (u1, v0), (u1, v1), (u0, v1)])
            part.cn.append(None)


def card(part, center, size_w, size_h, normal, up_hint, outward, anchor="center"):
    """Carte de feuillage : quad orienté par `normal`, UV plein ; normales
    personnalisées `outward` (éclairage « volume » du houppier)."""
    n = normal.normalized()
    side = up_hint.cross(n)
    if side.length < 1e-4:
        side = Vector((1, 0, 0)).cross(n)
    side.normalize()
    upv = n.cross(side).normalized()
    hw, hh = side * size_w * 0.5, upv * size_h * 0.5
    c = center if anchor == "center" else center - upv * size_h * 0.5  # "top" : suspendue
    part.quad(c - hw - hh, c + hw - hh, c + hw + hh, c - hw + hh,
              [(0, 0), (1, 0), (1, 1), (0, 1)], outward.normalized())


# ======================================================================= matériaux
_MATS = {}


def _img(name, non_color):
    path = os.path.join(PROPS, name)
    img = bpy.data.images.load(path, check_existing=True)
    if non_color:
        img.colorspace_settings.name = "Non-Color"
    return img


def material(tex, alpha):
    """MI_Decor_<tex> : BaseColor (+alpha), Normal, ORM -> Roughness/Metallic."""
    key = (tex, alpha)
    if key in _MATS:
        return _MATS[key]
    m = bpy.data.materials.new(f"MI_Decor_{tex}")
    m.use_nodes = True
    m.use_backface_culling = False
    nt = m.node_tree
    nt.nodes.clear()
    out = nt.nodes.new("ShaderNodeOutputMaterial")
    bsdf = nt.nodes.new("ShaderNodeBsdfPrincipled")
    nt.links.new(bsdf.outputs["BSDF"], out.inputs["Surface"])
    tb = nt.nodes.new("ShaderNodeTexImage")
    tb.image = _img(f"T_Decor_{tex}_BaseColor.png", False)
    nt.links.new(tb.outputs["Color"], bsdf.inputs["Base Color"])
    if alpha:
        nt.links.new(tb.outputs["Alpha"], bsdf.inputs["Alpha"])
    nrm = os.path.join(PROPS, f"T_Decor_{tex}_Normal.png")
    if os.path.exists(nrm):
        tn = nt.nodes.new("ShaderNodeTexImage")
        tn.image = _img(f"T_Decor_{tex}_Normal.png", True)
        nm = nt.nodes.new("ShaderNodeNormalMap")
        nt.links.new(tn.outputs["Color"], nm.inputs["Color"])
        nt.links.new(nm.outputs["Normal"], bsdf.inputs["Normal"])
    orm = os.path.join(PROPS, f"T_Decor_{tex}_ORM.png")
    if os.path.exists(orm):
        to = nt.nodes.new("ShaderNodeTexImage")
        to.image = _img(f"T_Decor_{tex}_ORM.png", True)
        sep = nt.nodes.new("ShaderNodeSeparateColor")
        nt.links.new(to.outputs["Color"], sep.inputs["Color"])
        nt.links.new(sep.outputs["Green"], bsdf.inputs["Roughness"])
        nt.links.new(sep.outputs["Blue"], bsdf.inputs["Metallic"])
    _MATS[key] = m
    return m


def to_object(name, parts):
    """Assemble les Part en un objet maillage (un slot matériau par Part)."""
    verts, faces, uvs, mats, cns, smooth = [], [], [], [], [], []
    slots = []
    for p in parts:
        if not p.f:
            continue
        if p.mat not in slots:
            slots.append(p.mat)
        mi = slots.index(p.mat)
        off = len(verts)
        verts += [tuple(v) for v in p.v]
        for f, uv, cn in zip(p.f, p.uv, p.cn):
            faces.append(tuple(i + off for i in f))
            uvs.append(uv)
            mats.append(mi)
            cns.append(cn)
            smooth.append(p.smooth)
    me = bpy.data.meshes.new(name)
    me.from_pydata(verts, [], faces)
    me.update()
    for m in slots:
        me.materials.append(m)
    uvl = me.uv_layers.new(name="UVMap")
    li = 0
    for pi, poly in enumerate(me.polygons):
        poly.material_index = mats[pi]
        poly.use_smooth = smooth[pi]
        for k, l in enumerate(poly.loop_indices):
            uvl.data[l].uv = uvs[pi][k]
    if any(c is not None for c in cns):
        # Normales personnalisées : feuillage -> « outward » ; autres faces -> normale de face.
        loop_n = [None] * len(me.loops)
        for pi, poly in enumerate(me.polygons):
            for k, l in enumerate(poly.loop_indices):
                loop_n[l] = tuple(cns[pi][k]) if cns[pi] is not None else tuple(poly.normal)
        me.normals_split_custom_set(loop_n)
    ob = bpy.data.objects.new(name, me)
    bpy.context.scene.collection.objects.link(ob)
    return ob


# ======================================================================= squelettes
class Branch:
    def __init__(self, pts, radii, level):
        self.pts, self.radii, self.level = pts, radii, level


def grow(rng, start, direction, length, radius, level, cfg, out):
    """Branche récursive : courbure aléatoire + tropisme (haut ou retombée)."""
    seg = max(3, int(length / cfg["seg_len"]))
    pts, radii = [start.copy()], [radius]
    d = direction.normalized()
    p = start.copy()
    for k in range(1, seg + 1):
        t = k / seg
        jitter = Vector((rng.gauss(0, 1), rng.gauss(0, 1), rng.gauss(0, 1))) * cfg["wobble"]
        d = (d + jitter + Vector((0, 0, cfg["tropism"][min(level, len(cfg["tropism"]) - 1)]))).normalized()
        p = p + d * (length / seg)
        pts.append(p.copy())
        radii.append(radius * (1 - t * (1 - cfg["taper"])))
    out.append(Branch(pts, radii, level))
    if level >= cfg["levels"]:
        return
    n_child = cfg["children"][level]
    for c in range(n_child):
        t = rng.uniform(cfg["child_t"][0], cfg["child_t"][1])
        idx = min(len(pts) - 2, int(t * (len(pts) - 1)))
        base = pts[idx]
        pd = (pts[idx + 1] - pts[idx]).normalized()
        # axe perpendiculaire aléatoire autour du parent (phyllotaxie ~137°)
        ref = Vector((0, 0, 1)) if abs(pd.z) < 0.9 else Vector((1, 0, 0))
        perp = pd.cross(ref).normalized()
        rot = Matrix.Rotation(c * 2.399 + rng.uniform(-0.4, 0.4), 3, pd)
        perp = rot @ perp
        ang = math.radians(rng.uniform(*cfg["angle"][level]))
        cd = (pd * math.cos(ang) + perp * math.sin(ang)).normalized()
        clen = length * cfg["ratio"][level] * (1 - 0.35 * t) * rng.uniform(0.8, 1.15)
        crad = max(cfg["min_r"], radii[idx] * cfg["r_ratio"])
        grow(rng, base, cd, clen, crad, level + 1, cfg, out)


def leaf_anchors(branches, min_level, every):
    """Points d'accroche des cartes : extrémités + points le long des rameaux fins."""
    out = []
    for b in branches:
        if b.level < min_level or b.level == 9:   # 9 = racines : jamais de feuilles
            continue
        for k in range(1, len(b.pts)):
            if k == len(b.pts) - 1 or k % every == 0:
                out.append(b.pts[k])
    return out


# ============================================================== constructeurs
LOD_SIDES = {0: 8, 1: 5, 2: 4}


def build_branchy(spec, lod, rng_seed, cfg, leaf_tex, bark_tex, leaf_cfg):
    rng = random.Random(rng_seed)
    branches = []
    trunk_top = Vector((0, 0, cfg["trunk_h"]))
    # tronc : de 0 à trunk_h, puis les charpentières
    trunk_cfg = dict(cfg, levels=0)
    grow(rng, Vector((0, 0, -0.15)), Vector((rng.gauss(0, 0.04), rng.gauss(0, 0.04), 1)),
         cfg["trunk_h"] + 0.15, cfg["trunk_r"], 0,
         dict(trunk_cfg, tropism=[0.35], wobble=cfg.get("trunk_wobble", 0.03), taper=0.7), branches)
    tip = branches[0].pts[-1]
    rtop = branches[0].radii[-1]
    if cfg.get("leader", 0) > 0:   # flèche : prolongement du tronc dans le houppier
        grow(rng, tip, Vector((0, 0, 1)), cfg["leader"], rtop, 1, cfg, branches)
    for i in range(cfg["limbs"]):
        a = i * 2 * math.pi / cfg["limbs"] + rng.uniform(-0.3, 0.3)
        el = math.radians(rng.uniform(*cfg["limb_elev"]))
        d = Vector((math.cos(a) * math.cos(el), math.sin(a) * math.cos(el), math.sin(el)))
        h0 = tip - Vector((0, 0, rng.uniform(0, cfg.get("limb_spread", 0.0))))
        grow(rng, h0, d, cfg["limb_len"] * rng.uniform(0.85, 1.15), rtop * 0.75, 1, cfg, branches)
    if cfg.get("roots", 0):
        for i in range(cfg["roots"]):
            a = i * 2 * math.pi / cfg["roots"] + rng.uniform(-0.3, 0.3)
            d = Vector((math.cos(a), math.sin(a), -0.55))
            p0 = Vector((0, 0, 0.9))
            pts, radii = [p0], [cfg["trunk_r"] * 0.55]
            for k in range(1, 5):
                p0 = p0 + d.normalized() * (cfg["root_len"] / 4) + Vector((0, 0, -0.06 * k))
                p0.z = max(p0.z, -0.25)
                pts.append(p0.copy())
                radii.append(cfg["trunk_r"] * 0.55 * (1 - k / 5))
            branches.append(Branch(pts, radii, 9))

    max_level = {0: 99, 1: cfg["levels"] - 1, 2: 1}[lod]
    sides = LOD_SIDES[lod]
    bark = Part(material(bark_tex, False))
    for b in branches:
        lvl = 0 if b.level == 9 else b.level
        if lvl > max_level:
            continue
        s = sides if lvl <= 1 else max(3, sides - 3)
        step = 1 if lod == 0 else 2
        pts = b.pts[::step] if len(b.pts) > 3 else b.pts
        rad = b.radii[::step] if len(b.radii) > 3 else b.radii
        if pts[-1] != b.pts[-1]:
            pts = pts + [b.pts[-1]]
            rad = rad + [b.radii[-1]]
        if rad[0] < cfg["min_draw_r"][lod]:
            continue
        tube(bark, pts, rad, s)

    leaves = Part(material(leaf_tex, True))
    anchors = leaf_anchors(branches, leaf_cfg["min_level"], leaf_cfg["every"])
    target = int(leaf_cfg["count"][lod] * spec.get("_leaf_mult", 1.0))
    rng2 = random.Random(rng_seed + 7)
    rng2.shuffle(anchors)
    if not anchors:
        return [bark]
    centre = Vector((0, 0, cfg["crown_center_z"]))
    size = leaf_cfg["size"] * leaf_cfg["lod_size"][lod]
    for k in range(target):
        p = anchors[k % len(anchors)] + Vector((rng2.gauss(0, 1), rng2.gauss(0, 1), rng2.gauss(0, 0.7))) * leaf_cfg["jitter"]
        out = (p - centre)
        out = Vector((out.x, out.y, out.z * 0.6 + 0.4 * out.length)) if out.length > 1e-3 else Vector((0, 0, 1))
        if leaf_cfg.get("hanging"):
            n = Vector((rng2.uniform(-1, 1), rng2.uniform(-1, 1), 0)).normalized()
            card(leaves, p, size * 0.45, size * 1.6, n, Vector((0, 0, 1)), out + Vector((0, 0, out.length)), "top")
            continue
        n = (out.normalized() + Vector((rng2.gauss(0, 0.8), rng2.gauss(0, 0.8), rng2.gauss(0, 0.8)))).normalized()
        s = size * rng2.uniform(0.8, 1.2)
        card(leaves, p, s, s, n, Vector((0, 0, 1)), out + Vector((0, 0, 0.6 * out.length)))
    return [bark, leaves]


def P(spec):
    """Dimensions cibles tirées du registre (milieu de plage, variante par graine)."""
    rng = random.Random(spec["seed"] * 31)
    h = rng.uniform(spec["height_m"][0] * 0.95 + spec["height_m"][1] * 0.05, spec["height_m"][1] * 0.9 + spec["height_m"][0] * 0.1)
    w = (spec["width_m"][0] + spec["width_m"][1]) * 0.5
    return h, w, rng


def b_broadleaf(spec, lod):
    H, W, r = P(spec)
    cfg = dict(trunk_h=H * r.uniform(0.3, 0.36), trunk_r=H * 0.024, limbs=r.randint(5, 7),
               limb_elev=(48, 74), limb_len=H * 0.46, limb_spread=H * 0.1, leader=H * 0.3, levels=3,
               children=[0, 4, 3, 2], child_t=(0.3, 0.95), angle=[(0, 0), (30, 55), (30, 60), (30, 60)],
               ratio=[0, 0.55, 0.5, 0.5], r_ratio=0.62, min_r=0.02, taper=0.45, seg_len=1.4,
               wobble=0.14, tropism=[0.25, 0.12, 0.05, -0.02], crown_center_z=H * 0.62,
               min_draw_r={0: 0.022, 1: 0.04, 2: 0.08})
    leaf = dict(min_level=2, every=1, count={0: 720, 1: 260, 2: 70}, size=2.0, lod_size={0: 1, 1: 1.6, 2: 2.9}, jitter=0.6)
    return build_branchy(spec, lod, spec["seed"], cfg, "Leaves_Broad", "Bark_Broad", leaf)


def b_oak(spec, lod):
    H, W, r = P(spec)
    cfg = dict(trunk_h=H * 0.28, trunk_r=H * 0.05, limbs=r.randint(5, 7), limb_elev=(12, 34),
               limb_len=W * 0.48, limb_spread=H * 0.08, levels=3, children=[0, 4, 3, 2], child_t=(0.25, 0.95),
               angle=[(0, 0), (25, 50), (30, 60), (30, 60)], ratio=[0, 0.55, 0.5, 0.45], r_ratio=0.6,
               min_r=0.025, taper=0.4, seg_len=1.3, wobble=0.18, tropism=[0.2, 0.1, 0.04, 0.0],
               crown_center_z=H * 0.6, min_draw_r={0: 0.022, 1: 0.045, 2: 0.1})
    leaf = dict(min_level=2, every=2, count={0: 950, 1: 320, 2: 70}, size=2.0, lod_size={0: 1, 1: 1.6, 2: 2.9}, jitter=0.7)
    return build_branchy(spec, lod, spec["seed"], cfg, "Leaves_Oak", "Bark_Oak", leaf)


def b_oak_forest(spec, lod):
    """Chêne de futaie : fût plus haut que le chêne isolé, couronne large et noueuse."""
    H, W, r = P(spec)
    cfg = dict(trunk_h=H * 0.36, trunk_r=H * 0.034, limbs=r.randint(5, 6), limb_elev=(28, 55),
               limb_len=W * 0.5, limb_spread=H * 0.08, levels=3, children=[0, 4, 3, 2], child_t=(0.25, 0.95),
               angle=[(0, 0), (30, 55), (30, 60), (30, 60)], ratio=[0, 0.55, 0.5, 0.45], r_ratio=0.6,
               min_r=0.025, taper=0.45, seg_len=1.3, wobble=0.2, tropism=[0.3, 0.08, 0.03, 0.0],
               crown_center_z=H * 0.66, min_draw_r={0: 0.022, 1: 0.045, 2: 0.1})
    leaf = dict(min_level=2, every=1, count={0: 760, 1: 270, 2: 70}, size=2.0, lod_size={0: 1, 1: 1.6, 2: 2.9}, jitter=0.7)
    return build_branchy(spec, lod, spec["seed"], cfg, "Leaves_Oak", "Bark_Oak", leaf)


def b_beech(spec, lod):
    """Hêtre : fût lisse gris clair, charpentières montantes, couronne ovale dense."""
    H, W, r = P(spec)
    cfg = dict(trunk_h=H * 0.34, trunk_r=H * 0.022, limbs=r.randint(5, 7), limb_elev=(55, 78),
               limb_len=H * 0.44, limb_spread=H * 0.1, leader=H * 0.32, levels=3, children=[0, 4, 3, 2],
               child_t=(0.3, 0.95), angle=[(0, 0), (28, 50), (30, 55), (30, 60)], ratio=[0, 0.5, 0.5, 0.45],
               r_ratio=0.62, min_r=0.02, taper=0.45, seg_len=1.4, wobble=0.1, tropism=[0.35, 0.15, 0.05, -0.02],
               crown_center_z=H * 0.64, min_draw_r={0: 0.022, 1: 0.04, 2: 0.08})
    leaf = dict(min_level=2, every=1, count={0: 980, 1: 320, 2: 80}, size=1.9, lod_size={0: 1, 1: 1.6, 2: 2.9}, jitter=0.6)
    return build_branchy(spec, lod, spec["seed"], cfg, "Leaves_Beech", "Bark_Pale", leaf)


def b_poplar(spec, lod):
    """Peuplier d'Italie : silhouette en fuseau, rameaux dressés le long du tronc."""
    H, W, r = P(spec)
    cfg = dict(trunk_h=H * 0.12, trunk_r=H * 0.017, limbs=r.randint(7, 9), limb_elev=(76, 86),
               limb_len=H * 0.62, limb_spread=0.0, leader=H * 0.85, levels=2, children=[0, 5, 3],
               child_t=(0.15, 0.95), angle=[(0, 0), (12, 25), (15, 30)], ratio=[0, 0.3, 0.4], r_ratio=0.55,
               min_r=0.02, taper=0.35, seg_len=1.6, wobble=0.05, tropism=[0.5, 0.35, 0.2],
               crown_center_z=H * 0.55, min_draw_r={0: 0.022, 1: 0.045, 2: 0.08})
    leaf = dict(min_level=1, every=1, count={0: 760, 1: 260, 2: 70}, size=1.5, lod_size={0: 1, 1: 1.6, 2: 2.6}, jitter=0.35)
    return build_branchy(spec, lod, spec["seed"], cfg, "Leaves_Poplar", "Bark_Pale", leaf)


def b_twisted(spec, lod):
    H, W, r = P(spec)
    cfg = dict(trunk_h=H * 0.36, trunk_r=H * 0.045, limbs=r.randint(3, 5), limb_elev=(15, 45),
               limb_len=W * 0.5, limb_spread=H * 0.05, levels=3, children=[0, 3, 3, 2], child_t=(0.25, 0.95),
               angle=[(0, 0), (35, 70), (35, 70), (30, 60)], ratio=[0, 0.6, 0.5, 0.45], r_ratio=0.6,
               min_r=0.02, taper=0.4, seg_len=1.0, wobble=0.42, tropism=[0.35, 0.02, -0.05, -0.05],
               crown_center_z=H * 0.62, trunk_wobble=0.12, roots=r.randint(5, 7), root_len=H * 0.16, min_draw_r={0: 0.022, 1: 0.045, 2: 0.1})
    leaf = dict(min_level=2, every=3, count={0: 520, 1: 190, 2: 50}, size=1.9, lod_size={0: 1, 1: 1.6, 2: 2.8}, jitter=0.6)
    return build_branchy(spec, lod, spec["seed"], cfg, "Leaves_Oak", "Bark_Dark", leaf)


def b_willow(spec, lod):
    H, W, r = P(spec)
    cfg = dict(trunk_h=H * 0.3, trunk_r=H * 0.04, limbs=r.randint(5, 7), limb_elev=(35, 60), limb_len=H * 0.45,
               limb_spread=H * 0.05, levels=2, children=[0, 4, 3], child_t=(0.3, 0.95),
               angle=[(0, 0), (30, 55), (40, 70)], ratio=[0, 0.55, 0.5], r_ratio=0.6, min_r=0.02, taper=0.4,
               seg_len=1.2, wobble=0.14, tropism=[0.25, 0.0, -0.35], crown_center_z=H * 0.62,
               min_draw_r={0: 0.022, 1: 0.045, 2: 0.08})
    leaf = dict(min_level=1, every=1, count={0: 700, 1: 260, 2: 70}, size=1.8, lod_size={0: 1, 1: 1.5, 2: 2.4}, jitter=0.4, hanging=True)
    return build_branchy(spec, lod, spec["seed"], cfg, "Leaves_Willow", "Bark_Pale", leaf)


def b_conifer(spec, lod):
    H, W, r = P(spec)
    rng = random.Random(spec["seed"])
    bark = Part(material("Bark_Pine", False))
    leaves = Part(material("Leaves_Needle", True))
    trunk = [Vector((rng.gauss(0, 0.05) * k / 10, rng.gauss(0, 0.05) * k / 10, -0.15 + (H + 0.15) * k / 10)) for k in range(11)]
    tr = H * 0.018
    tube(bark, trunk, [tr * (1 - 0.9 * k / 10) for k in range(11)], LOD_SIDES[lod])
    base = H * rng.uniform(0.18, 0.28)
    R = W * 0.5
    spacing = {0: 0.55, 1: 1.1, 2: 2.2}[lod]
    z = base
    wh = 0
    while z < H - 0.4:
        rel = (z - base) / (H - base)
        rad = R * (1 - rel) ** 0.95 + 0.35
        n_br = {0: 6, 1: 4, 2: 3}[lod]
        for b in range(n_br):
            a = b * 2 * math.pi / n_br + wh * 0.7 + rng.uniform(-0.25, 0.25)
            d = Vector((math.cos(a), math.sin(a), 0))
            L = rad * rng.uniform(0.85, 1.05)
            droop = rng.uniform(0.15, 0.35)
            pts = [Vector((0, 0, z)), Vector((0, 0, z)) + d * L * 0.5 - Vector((0, 0, droop * 0.3)),
                   Vector((0, 0, z)) + d * L - Vector((0, 0, droop * L * 0.3)) + Vector((0, 0, 0.15))]
            if lod == 0:
                tube(bark, pts, [tr * (1 - rel) * 0.45 + 0.02, 0.025, 0.01], 3)
            # cartes : nappes d'aiguilles le long de la branche, inclinées
            k_cards = {0: 3, 1: 2, 2: 1}[lod]
            for c in range(k_cards):
                t = (c + 0.6) / k_cards
                p = pts[0].lerp(pts[2], t) + Vector((0, 0, 0.05))
                s = max(0.9, L * 0.75) * {0: 1, 1: 1.35, 2: 1.9}[lod] * (1 - 0.25 * t)
                n = (Vector((0, 0, 1)) * 0.75 + d * 0.35 + Vector((rng.gauss(0, 0.2), rng.gauss(0, 0.2), 0))).normalized()
                outward = d + Vector((0, 0, 0.6))
                card(leaves, p, s, s * 0.8, n, d, outward)
                if lod < 2:  # carte verticale pour épaissir la silhouette
                    card(leaves, p, s * 0.9, s * 0.6, d.cross(Vector((0, 0, 1))), Vector((0, 0, 1)), outward)
        z += spacing * rng.uniform(0.85, 1.15)
        wh += 1
    # cime
    card(leaves, Vector((0, 0, H - 0.6)), 1.4, 1.6, Vector((1, 0, 0)), Vector((0, 0, 1)), Vector((0, 0, 1)))
    card(leaves, Vector((0, 0, H - 0.6)), 1.4, 1.6, Vector((0, 1, 0)), Vector((0, 0, 1)), Vector((0, 0, 1)))
    return [bark, leaves]


def b_cypress(spec, lod):
    H, W, r = P(spec)
    rng = random.Random(spec["seed"])
    bark = Part(material("Bark_Oak", False))
    leaves = Part(material("Leaves_Cypress", True))
    trunk = [Vector((0, 0, -0.1)), Vector((0, 0, H * 0.4)), Vector((0, 0, H * 0.92))]
    tube(bark, trunk, [H * 0.016, H * 0.011, 0.03], LOD_SIDES[lod])
    R = W * 0.5
    count = {0: 640, 1: 220, 2: 60}[lod]
    size = {0: 1.5, 1: 2.3, 2: 3.8}[lod]
    for k in range(count):
        zr = rng.random() ** 0.85
        z = H * (0.06 + 0.9 * zr)
        prof = math.sin(math.pi * min(1.0, (zr + 0.08) / 1.08)) ** 0.6 * (1 - 0.35 * zr)
        rad = R * prof * rng.uniform(0.35, 0.95)
        a = rng.uniform(0, 2 * math.pi)
        d = Vector((math.cos(a), math.sin(a), 0))
        p = Vector((0, 0, z)) + d * rad
        n = (d + Vector((rng.gauss(0, 0.5), rng.gauss(0, 0.5), rng.gauss(0, 0.3)))).normalized()
        card(leaves, p, size * 0.75, size * 1.2, n, Vector((0, 0, 1)), d + Vector((0, 0, 0.3)))
    return [bark, leaves]


def _ring_cards(part, rng, n, radius, h, w, tilt=0.0, z0=0.0, bottom=True):
    """Cartes verticales disposées en étoile (plantes, herbes)."""
    for i in range(n):
        a = i * math.pi / n + rng.uniform(-0.2, 0.2)
        d = Vector((math.cos(a), math.sin(a), 0))
        off = Vector((rng.uniform(-radius, radius), rng.uniform(-radius, radius), 0))
        nrm = d.cross(Vector((0, 0, 1)))
        nrm = (nrm + Vector((0, 0, tilt))).normalized()
        c = Vector((0, 0, z0 + h * 0.5)) + off
        card(part, c, w, h, nrm, Vector((0, 0, 1)), Vector((0, 0, 1)) + nrm * 0.3)


def _plant(spec, lod, tex, n_by_lod, wfac=1.0, tilt=0.0, spread=0.12):
    H, W, r = P(spec)
    rng = random.Random(spec["seed"])
    part = Part(material(tex, True))
    _ring_cards(part, rng, n_by_lod[lod], spread * W, H, W * wfac, tilt)
    return [part]


def b_grass_short(spec, lod): return _plant(spec, lod, "Plant_Grass", {0: 4, 1: 3, 2: 2})
def b_grass_tall(spec, lod): return _plant(spec, lod, "Plant_Grass", {0: 5, 1: 3, 2: 2})
def b_grass_wispy_short(spec, lod): return _plant(spec, lod, "Plant_GrassDry", {0: 4, 1: 3, 2: 2})
def b_grass_wispy_tall(spec, lod): return _plant(spec, lod, "Plant_GrassDry", {0: 5, 1: 3, 2: 2})
def b_reed(spec, lod): return _plant(spec, lod, "Plant_Reed", {0: 6, 1: 3, 2: 2}, spread=0.18)
def b_wheat(spec, lod): return _plant(spec, lod, "Plant_Wheat", {0: 8, 1: 4, 2: 2}, spread=0.3)
def b_mullein(spec, lod): return _plant(spec, lod, "Plant_Mullein", {0: 3, 1: 2, 2: 1}, spread=0.03)


def b_fern(spec, lod):
    H, W, r = P(spec)
    rng = random.Random(spec["seed"])
    part = Part(material("Plant_Fern", True))
    n = {0: 6, 1: 3, 2: 2}[lod]
    for i in range(n):  # frondes inclinées vers l'extérieur
        a = i * 2 * math.pi / n + rng.uniform(-0.2, 0.2)
        d = Vector((math.cos(a), math.sin(a), 0))
        c = d * W * 0.22 + Vector((0, 0, H * 0.45))
        nrm = (d * -0.55 + Vector((0, 0, 1)) * 0.0 + d.cross(Vector((0, 0, 1))) * 0.8).normalized()
        card(part, c, W * 0.6, H, nrm, Vector((0, 0, 1)) + d * 0.5, Vector((0, 0, 1)))
    return [part]


def _blob(part, rng, center, radii, n, size, outward_c):
    for k in range(n):
        v = Vector((rng.gauss(0, 1), rng.gauss(0, 1), abs(rng.gauss(0, 1)) * 0.8))
        v = v.normalized() * (rng.random() ** 0.35)
        p = center + Vector((v.x * radii[0], v.y * radii[1], v.z * radii[2]))
        out = p - outward_c
        n_ = (out.normalized() + Vector((rng.gauss(0, 0.7), rng.gauss(0, 0.7), rng.gauss(0, 0.7)))).normalized()
        card(part, p, size, size, n_, Vector((0, 0, 1)), out + Vector((0, 0, 0.5)))


def _bush(spec, lod, tex):
    H, W, r = P(spec)
    rng = random.Random(spec["seed"])
    stems = Part(material("Bark_Dark", False))
    part = Part(material(tex, True))
    if lod == 0:
        for i in range(5):
            a = i * 1.26
            tube(stems, [Vector((0, 0, 0)), Vector((math.cos(a) * W * 0.2, math.sin(a) * W * 0.2, H * 0.55))], [0.03, 0.012], 3)
    n = {0: 70, 1: 26, 2: 10}[lod]
    s = {0: 0.75, 1: 1.1, 2: 1.6}[lod] * W / 1.8
    _blob(part, rng, Vector((0, 0, H * 0.25)), (W * 0.38, W * 0.38, H * 0.6), n, s, Vector((0, 0, 0)))
    return [stems, part]


def b_bush(spec, lod): return _bush(spec, lod, "Leaves_Bush")
def b_bush_flowers(spec, lod): return _bush(spec, lod, "Leaves_BushFlowers")


def b_lily(spec, lod):
    H, W, r = P(spec)
    rng = random.Random(spec["seed"])
    pads = Part(material("Plant_LilyPad", True))
    flw = Part(material("Plant_LilyFlower", True))
    n = {0: 12, 1: 7, 2: 4}[lod]
    for k in range(n):
        a = k * 2.399 + rng.uniform(-0.3, 0.3)
        rr = W * 0.42 * math.sqrt((k + 0.5) / n)
        c = Vector((math.cos(a) * rr, math.sin(a) * rr, 0.015 + 0.002 * k))
        s = rng.uniform(0.26, 0.4) * {0: 1, 1: 1.2, 2: 1.5}[lod]
        rot = rng.uniform(0, 2 * math.pi)
        card(pads, c, s, s, Vector((0, 0, 1)), Vector((math.cos(rot), math.sin(rot), 0)), Vector((0, 0, 1)))
    nf = {0: 2, 1: 1, 2: 1}[lod]
    for k in range(nf):
        a, rr = rng.uniform(0, 2 * math.pi), W * 0.25 * rng.random()
        c = Vector((math.cos(a) * rr, math.sin(a) * rr, 0.06))
        card(flw, c, 0.16, 0.16, Vector((0, 0, 1)), Vector((1, 0, 0)), Vector((0, 0, 1)))
        if lod == 0:
            for q in (Vector((1, 0, 0)), Vector((0, 1, 0))):
                card(flw, c + Vector((0, 0, 0.04)), 0.14, 0.1, q, Vector((0, 0, 1)), Vector((0, 0, 1)))
    return [pads, flw]


def _rock_mesh(part, rng, subdiv, scale, noise_amp, seed, flat_bottom=True, strata=0.0):
    """Icosphère déplacée par bruit, base aplatie, UV par projection dominante."""
    # icosphère par subdivisions
    t = (1 + 5 ** 0.5) / 2
    V = [Vector(v).normalized() for v in [(-1, t, 0), (1, t, 0), (-1, -t, 0), (1, -t, 0), (0, -1, t), (0, 1, t),
                                          (0, -1, -t), (0, 1, -t), (t, 0, -1), (t, 0, 1), (-t, 0, -1), (-t, 0, 1)]]
    F = [(0, 11, 5), (0, 5, 1), (0, 1, 7), (0, 7, 10), (0, 10, 11), (1, 5, 9), (5, 11, 4), (11, 10, 2), (10, 7, 6),
         (7, 1, 8), (3, 9, 4), (3, 4, 2), (3, 2, 6), (3, 6, 8), (3, 8, 9), (4, 9, 5), (2, 4, 11), (6, 2, 10),
         (8, 6, 7), (9, 8, 1)]
    for _ in range(subdiv):
        mid = {}
        nf = []

        def m(a, b):
            k = (min(a, b), max(a, b))
            if k not in mid:
                V.append(((V[a] + V[b]) * 0.5).normalized())
                mid[k] = len(V) - 1
            return mid[k]
        for a, b, c in F:
            ab, bc, ca = m(a, b), m(b, c), m(c, a)
            nf += [(a, ab, ca), (b, bc, ab), (c, ca, bc), (ab, bc, ca)]
        F = nf
    off = Vector((seed * 0.731, seed * 0.317, seed * 0.113))
    out = []
    for v in V:
        n1 = noise.fractal(v * 1.3 + off, 0.5, 2.0, 4)
        n2 = noise.noise(v * 4.0 + off) * 0.35
        d = 1.0 + noise_amp * (n1 + n2)
        p = Vector((v.x * scale[0], v.y * scale[1], v.z * scale[2])) * d
        if strata:
            p.z += strata * math.sin(p.z * 9.0 + n1 * 3) * 0.05 * scale[2]
        out.append(p)
    if flat_bottom:
        zmin_cut = -scale[2] * 0.25
        for p in out:
            if p.z < zmin_cut:
                p.z = zmin_cut + (p.z - zmin_cut) * 0.15
    zmin = min(p.z for p in out)
    for p in out:
        p.z -= zmin + 0.04 * scale[2]   # légèrement enterré
    base = len(part.v)
    part.v += out
    for a, b, c in F:
        pa, pb, pc = out[a], out[b], out[c]
        n = (pb - pa).cross(pc - pa)
        ax = max(range(3), key=lambda i: abs(n[i]))
        def uv(p):
            return ((p.y, p.z) if ax == 0 else (p.x, p.z) if ax == 1 else (p.x, p.y))
        part.f.append((base + a, base + b, base + c))
        part.uv.append([tuple(x * 0.5 for x in uv(pa)), tuple(x * 0.5 for x in uv(pb)), tuple(x * 0.5 for x in uv(pc))])
        part.cn.append(None)


def b_rock(spec, lod):
    H, W, r = P(spec)
    part = Part(material("Rock_Mossy" if spec["seed"] % 2 else "Rock_Grey", False))
    _rock_mesh(part, r, {0: 3, 1: 2, 2: 1}[lod], (W * 0.5, W * 0.4, H * 0.62), 0.22, spec["seed"])
    return [part]


def b_pebble_round(spec, lod):
    H, W, r = P(spec)
    part = Part(material("Rock_Grey", False))
    _rock_mesh(part, r, {0: 2, 1: 1, 2: 1}[lod], (W * 0.5, W * 0.38, H * 0.6), 0.12, spec["seed"])
    return [part]


def b_pebble_square(spec, lod):
    H, W, r = P(spec)
    part = Part(material("Rock_Ochre", False))
    _rock_mesh(part, r, {0: 2, 1: 1, 2: 1}[lod], (W * 0.5, W * 0.35, H * 0.55), 0.3, spec["seed"])
    return [part]


def b_standing_stone(spec, lod):
    H, W, r = P(spec)
    part = Part(material("Rock_Grey", False))
    _rock_mesh(part, r, {0: 3, 1: 2, 2: 1}[lod], (W * 0.5, W * 0.28, H * 0.55), 0.12, spec["seed"], strata=0.0)
    return [part]


def b_spire(spec, lod):
    """Aiguille rocheuse : colonne à profil effilé, bombements et strates."""
    H, W, r = P(spec)
    rng = random.Random(spec["seed"])
    part = Part(material("Rock_Red", False))
    sides, rings = {0: (20, 34), 1: (12, 17), 2: (8, 8)}[lod]
    off = Vector((spec["seed"] * 0.37, spec["seed"] * 0.11, 0))
    base = len(part.v)
    lean = Vector((rng.gauss(0, 0.06), rng.gauss(0, 0.06), 0))
    for k in range(rings + 1):
        t = k / rings
        z = -0.3 + (H + 0.3) * t
        # colonne : flancs presque verticaux, rétrécissement surtout au sommet
        prof = 0.55 + 0.45 * (1 - t) ** 1.6 - 0.35 * max(0.0, t - 0.82) / 0.18
        for s in range(sides):
            a = 2 * math.pi * s / sides
            v = Vector((math.cos(a), math.sin(a), 0))
            n1 = noise.fractal(Vector((v.x * 2.2, v.y * 2.2, t * 1.6)) + off, 0.65, 2.0, 5)
            fiss = 0.12 * noise.noise(Vector((v.x * 7, v.y * 7, t * 0.8)) + off)   # fissures verticales
            ledge = 0.05 * math.floor(noise.noise(Vector((0, 0, t * 6)) + off) * 3)  # corniches
            rr = W * 0.5 * prof * (1 + 0.32 * n1 + fiss + ledge)
            part.v.append(Vector((v.x * rr, v.y * rr * 0.82, z)) + lean * z)
    for k in range(rings):
        for s in range(sides):
            s2 = (s + 1) % sides
            i0, i1 = base + k * sides + s, base + k * sides + s2
            i2, i3 = base + (k + 1) * sides + s2, base + (k + 1) * sides + s
            u0, u1 = s / sides * 4, (s + 1) / sides * 4
            v0, v1 = k / rings * H / 6, (k + 1) / rings * H / 6
            part.f.append((i0, i1, i2, i3))
            part.uv.append([(u0, v0), (u1, v0), (u1, v1), (u0, v1)])
            part.cn.append(None)
    top = len(part.v)
    part.v.append(Vector((0, 0, H * 1.005)) + lean * H)
    for s in range(sides):
        s2 = (s + 1) % sides
        part.f.append((base + rings * sides + s, base + rings * sides + s2, top))
        part.uv.append([(0, 0), (1, 0), (0.5, 1)])
        part.cn.append(None)
    return [part]


BUILDERS = {k[2:]: v for k, v in dict(globals()).items() if k.startswith("b_") and callable(v)}


# ======================================================================= export
def reset():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    _MATS.clear()


def export(ob, path, alpha_mats):
    for o in bpy.context.scene.objects:
        o.select_set(o == ob)
    bpy.context.view_layer.objects.active = ob
    bpy.ops.export_scene.gltf(filepath=path, export_format="GLTF_SEPARATE", use_selection=True,
                              export_materials="EXPORT", export_keep_originals=True, export_yup=True,
                              export_apply=True, export_texcoords=True, export_normals=True)
    # Post-traitement : feuillages en MASK (le moteur découpe à 0,5), URI nus.
    with open(path, encoding="utf-8") as f:
        g = json.load(f)
    for m in g.get("materials", []):
        if m.get("name") in alpha_mats:
            m["alphaMode"] = "MASK"
            m["alphaCutoff"] = 0.5
        m["doubleSided"] = True
    for im in g.get("images", []):
        if "uri" in im:
            im["uri"] = os.path.basename(im["uri"].replace("\\", "/"))
    g.setdefault("asset", {})["generator"] = "LCDLLN atelier décors (build_decor.py)"
    with open(path, "w", encoding="utf-8") as f:
        json.dump(g, f, indent=1, ensure_ascii=False)


def normalize_dims(objs, target_h, width_range):
    """Met le LOD0 à la hauteur cible (facteur uniforme), ramène sa largeur X dans la
    cote (facteur horizontal) et borne le dessous à -0,3 m (racines, cartes pendantes) ;
    mêmes facteurs pour tous les LOD."""
    zs = [v.co.z for v in objs[0].data.vertices]
    f = target_h / max(1e-6, max(zs))
    xs = [v.co.x * f for v in objs[0].data.vertices]
    w = max(xs) - min(xs)
    lo, hi = width_range
    fx = 1.0 if lo <= w <= hi else (hi / w if w > hi else lo / w)
    for ob in objs:
        for v in ob.data.vertices:
            v.co = Vector((v.co.x * f * fx, v.co.y * f * fx, max(v.co.z * f, -0.3)))
        ob.data.update()
    return f


def render_billboard(ob, spec_id):
    """Rendu Cycles orthographique de face (fond transparent) -> texture du billboard."""
    sc = bpy.context.scene
    bb = [ob.matrix_world @ Vector(c) for c in ob.bound_box]
    mn = Vector((min(p.x for p in bb), min(p.y for p in bb), min(p.z for p in bb)))
    mx = Vector((max(p.x for p in bb), max(p.y for p in bb), max(p.z for p in bb)))
    w, h = max(mx.x - mn.x, mx.y - mn.y), mx.z - mn.z
    maxd = max(w, h) * 1.04
    res = 512
    cam_d = bpy.data.cameras.new("bbcam")
    cam_d.type = "ORTHO"
    cam_d.ortho_scale = maxd
    cam = bpy.data.objects.new("bbcam", cam_d)
    sc.collection.objects.link(cam)
    c = (mn + mx) * 0.5
    cam.location = (c.x, mn.y - 50, c.z)
    cam.rotation_euler = (math.radians(90), 0, 0)
    sc.camera = cam
    sun_d = bpy.data.lights.new("sun", "SUN")
    sun_d.energy = 3.2
    sun = bpy.data.objects.new("sun", sun_d)
    sun.rotation_euler = (math.radians(50), 0, math.radians(-30))
    sc.collection.objects.link(sun)
    world = bpy.data.worlds.new("w")
    world.use_nodes = True
    world.node_tree.nodes["Background"].inputs["Color"].default_value = (0.55, 0.6, 0.68, 1)
    world.node_tree.nodes["Background"].inputs["Strength"].default_value = 0.9
    sc.world = world
    sc.render.engine = "CYCLES"
    sc.cycles.device = "CPU"
    sc.cycles.samples = 24
    sc.cycles.use_denoising = False
    sc.cycles.transparent_max_bounces = 16
    sc.render.film_transparent = True
    sc.render.resolution_x = max(64, int(round(res * w * 1.04 / maxd)))
    sc.render.resolution_y = max(64, int(round(res * h * 1.04 / maxd)))
    sc.render.resolution_percentage = 100
    sc.render.image_settings.file_format = "PNG"
    sc.render.image_settings.color_mode = "RGBA"
    sc.view_settings.view_transform = "Standard"
    tex = f"T_Decor_BB_{spec_id}.png"
    sc.render.filepath = os.path.join(PROPS, tex)
    for o in sc.objects:
        o.hide_render = o.type == "MESH" and o != ob
    bpy.ops.render.render(write_still=True)
    # alpha binaire (découpe) pour éviter les franges
    img = bpy.data.images.load(sc.render.filepath)
    px = list(img.pixels)
    for i in range(3, len(px), 4):
        px[i] = 1.0 if px[i] > 0.45 else 0.0
    img.pixels = px
    img.save()
    # quad : mêmes extents que la caméra
    qw = maxd * sc.render.resolution_x / res
    qh = maxd * sc.render.resolution_y / res
    return tex, c, qw, qh


def billboard_object(spec_id, tex, c, qw, qh):
    m = bpy.data.materials.new(f"MI_Decor_BB_{spec_id}")
    m.use_nodes = True
    nt = m.node_tree
    bsdf = nt.nodes["Principled BSDF"] if "Principled BSDF" in nt.nodes else nt.nodes.new("ShaderNodeBsdfPrincipled")
    t = nt.nodes.new("ShaderNodeTexImage")
    t.image = bpy.data.images.load(os.path.join(PROPS, tex), check_existing=True)
    nt.links.new(t.outputs["Color"], bsdf.inputs["Base Color"])
    nt.links.new(t.outputs["Alpha"], bsdf.inputs["Alpha"])
    bsdf.inputs["Roughness"].default_value = 0.85
    part = Part(m)
    z0 = c.z - qh * 0.5
    for axis in (Vector((1, 0, 0)), Vector((0, 1, 0))):
        n = axis.cross(Vector((0, 0, 1)))
        a, b = Vector((c.x, c.y, 0)) - axis * qw * 0.5, Vector((c.x, c.y, 0)) + axis * qw * 0.5
        part.quad(a + Vector((0, 0, z0)), b + Vector((0, 0, z0)), b + Vector((0, 0, z0 + qh)), a + Vector((0, 0, z0 + qh)),
                  [(0, 0), (1, 0), (1, 1), (0, 1)], Vector((0, 0, 1)) + n * 0.25)
    return to_object(spec_id + "_BB", [part]), m.name


def build(spec, do_bb=True):
    reset()
    fn = BUILDERS[spec["builder"]]
    rng = random.Random(spec["seed"] * 31)
    h_target = rng.uniform(spec["height_m"][0] * 0.95 + spec["height_m"][1] * 0.05,
                           spec["height_m"][1] * 0.9 + spec["height_m"][0] * 0.1)
    budget = json.load(open(SPECS, encoding="utf-8"))["budgets"][spec["budget"]]
    spec = dict(spec)
    for attempt in range(6):  # budget adaptatif : moins de cartes si dépassement
        objs = [to_object(spec["id"] + ("" if lod == 0 else f"_LOD{lod}"), fn(spec, lod)) for lod in range(3)]
        tris = [sum(len(p.vertices) - 2 for p in o.data.polygons) for o in objs]
        if all(t <= b for t, b in zip(tris, budget)):
            break
        spec["_leaf_mult"] = spec.get("_leaf_mult", 1.0) * 0.9
        reset()
    normalize_dims(objs, h_target, spec["width_m"])
    alpha = {m.name for m in bpy.data.materials if any(l.to_socket.name == "Alpha" for l in m.node_tree.links)}
    report = {"id": spec["id"], "lods": []}
    for lod, ob in enumerate(objs):
        name = spec["id"] + ("" if lod == 0 else f"_LOD{lod}")
        export(ob, os.path.join(PROPS, name + ".gltf"), alpha)
        report["lods"].append(sum(len(p.vertices) - 2 for p in ob.data.polygons))
    if do_bb and spec.get("billboard"):
        tex, c, qw, qh = render_billboard(objs[0], spec["id"])
        bbo, mname = billboard_object(spec["id"], tex, c, qw, qh)
        export(bbo, os.path.join(PROPS, spec["id"] + "_BB.gltf"), {mname})
    return report


def main():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else sys.argv[1:]
    only = set()
    do_bb = "--no-billboard" not in argv
    if "--only" in argv:
        only = set(argv[argv.index("--only") + 1].split(","))
    specs = json.load(open(SPECS, encoding="utf-8"))["assets"]
    for s in specs:
        if only and s["id"] not in only and s["builder"] not in only:
            continue
        r = build(s, do_bb)
        print(f"[build_decor] {r['id']} tris LOD0/1/2 = {r['lods']}", flush=True)


main()
