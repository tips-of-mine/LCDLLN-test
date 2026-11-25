"""Textures d'interface de la carte du monde (touche M) — rendu Blender procédural.

Produit, dans le dossier de sortie, trois PNG RGBA destinés à
game/data/ui/map/ :

- map_roll.png      (1024 x 160) : rouleau de bois tourné complet, pommeaux
                     compris, fond transparent. Utilisé en « 3 tranches » par le
                     client : pommeau gauche (kRollCapFrac de la largeur),
                     corps étiré (fil du bois parallèle à l'axe, donc étirable
                     sans couture), pommeau droit.
- map_parchment.png (512 x 512)  : parchemin répétable sans couture (bruit 4D
                     enroulé sur un tore), fibres et taches légères. Fond de la
                     fenêtre et, assombri par le client, zones inexplorées.
- map_edge.png      (256 x 256)  : bord usé du parchemin (brun irrégulier sur
                     les bords, centre transparent), utilisé en 9 tranches.

Aucune source externe : géométrie et matériaux 100 % procéduraux (licence du
dépôt). Rendu Cycles, transformation de vue « Standard » (couleurs fidèles),
film transparent.

Usage (Blender 5.x, interface en français : les nœuds sont créés par
bl_idname, jamais par nom affiché) :
    blender --background --python map_parchment_blender.py -- <dossier_sortie>
ou, depuis la console Python de Blender :
    OUT_DIR = r"<dossier_sortie>"; exec(open(r"<ce_fichier>").read())
"""

import math
import os
import sys

import bpy

SCENE_NAME = "LCDLLN_UI_Map"
SAMPLES = 64


def _out_dir() -> str:
    """Dossier de sortie : argument après « -- », sinon variable OUT_DIR."""
    if "--" in sys.argv:
        args = sys.argv[sys.argv.index("--") + 1:]
        if args:
            return args[0]
    d = globals().get("OUT_DIR")
    if not d:
        raise SystemExit("dossier de sortie manquant (argument après -- ou OUT_DIR)")
    return d


def _new_scene(w: int, h: int) -> bpy.types.Scene:
    """Scène vierge dédiée (la scène de l'utilisateur n'est pas touchée)."""
    old = bpy.data.scenes.get(SCENE_NAME)
    if old is not None:
        for o in list(old.objects):
            bpy.data.objects.remove(o, do_unlink=True)
        bpy.data.scenes.remove(old)
    sc = bpy.data.scenes.new(SCENE_NAME)
    sc.render.engine = "CYCLES"
    sc.cycles.samples = SAMPLES
    sc.cycles.use_denoising = True
    sc.render.resolution_x = w
    sc.render.resolution_y = h
    sc.render.resolution_percentage = 100
    sc.render.film_transparent = True
    sc.render.image_settings.file_format = "PNG"
    sc.render.image_settings.color_mode = "RGBA"
    sc.render.image_settings.color_depth = "8"
    sc.view_settings.view_transform = "Standard"
    sc.view_settings.look = "None"
    world = bpy.data.worlds.new(SCENE_NAME + "_World")
    world.use_nodes = True
    bg = next(n for n in world.node_tree.nodes if n.bl_idname == "ShaderNodeBackground")
    bg.inputs[0].default_value = (0.55, 0.52, 0.48, 1.0)
    bg.inputs[1].default_value = 0.35
    sc.world = world
    return sc


def _link(sc: bpy.types.Scene, obj: bpy.types.Object) -> bpy.types.Object:
    sc.collection.objects.link(obj)
    return obj


def _camera_ortho(sc, scale: float, loc, rot):
    cam_data = bpy.data.cameras.new("cam_ui")
    cam_data.type = "ORTHO"
    cam_data.ortho_scale = scale
    cam = _link(sc, bpy.data.objects.new("cam_ui", cam_data))
    cam.location = loc
    cam.rotation_euler = rot
    sc.camera = cam
    return cam


def _node(nt, idname, x=0, y=0):
    n = nt.nodes.new(idname)
    n.location = (x, y)
    return n


def _render(sc, path: str) -> None:
    sc.render.filepath = path
    win = bpy.context.window
    prev = win.scene if win else None
    if win:
        win.scene = sc
    bpy.ops.render.render(write_still=True, scene=sc.name)
    if win and prev:
        win.scene = prev


# --------------------------------------------------------------------------
# Rouleau de bois
# --------------------------------------------------------------------------

BODY_LEN = 6.0       # longueur du corps (unités Blender, sans échelle physique)
BODY_R = 0.40        # rayon du corps
CAP_LEN = 0.62       # longueur d'un pommeau
VIEW_W = BODY_LEN + 2 * CAP_LEN + 0.40  # largeur cadrée (marge autour des pommeaux)


def _wood_material() -> bpy.types.Material:
    """Bois tourné ciré : fil parallèle à l'axe X (étirable sans couture)."""
    m = bpy.data.materials.new("ui_wood")
    m.use_nodes = True
    nt = m.node_tree
    for n in list(nt.nodes):
        nt.nodes.remove(n)
    out = _node(nt, "ShaderNodeOutputMaterial", 600, 0)
    bsdf = _node(nt, "ShaderNodeBsdfPrincipled", 300, 0)
    tc = _node(nt, "ShaderNodeTexCoord", -900, 0)
    sep = _node(nt, "ShaderNodeSeparateXYZ", -700, 0)
    comb = _node(nt, "ShaderNodeCombineXYZ", -500, 0)
    # Fil du bois : ne dépend que de (Y, Z) — la direction X est neutralisée,
    # sauf un très léger bruit basse fréquence pour casser la régularité.
    nt.links.new(tc.outputs["Object"], sep.inputs[0])
    nt.links.new(sep.outputs["Y"], comb.inputs["Y"])
    nt.links.new(sep.outputs["Z"], comb.inputs["Z"])
    wave = _node(nt, "ShaderNodeTexWave", -300, 100)
    wave.wave_type = "RINGS"
    wave.rings_direction = "X"
    wave.inputs["Scale"].default_value = 3.2
    wave.inputs["Distortion"].default_value = 6.0
    wave.inputs["Detail"].default_value = 3.0
    nt.links.new(comb.outputs[0], wave.inputs["Vector"])
    noise = _node(nt, "ShaderNodeTexNoise", -300, -150)
    noise.inputs["Scale"].default_value = 9.0
    noise.inputs["Detail"].default_value = 6.0
    nt.links.new(comb.outputs[0], noise.inputs["Vector"])
    mix = _node(nt, "ShaderNodeMath", -100, 0)
    mix.operation = "MULTIPLY_ADD"
    mix.inputs[1].default_value = 0.75
    nt.links.new(wave.outputs["Fac"], mix.inputs[0])
    nt.links.new(noise.outputs["Fac"], mix.inputs[2])
    ramp = _node(nt, "ShaderNodeValToRGB", 80, 0)
    els = ramp.color_ramp.elements
    els[0].position = 0.25
    els[0].color = (0.035, 0.015, 0.006, 1.0)  # brun foncé (veines)
    els[1].position = 0.95
    els[1].color = (0.12, 0.055, 0.022, 1.0)   # brun chaud (linéaire) — foncé le 25/11
    nt.links.new(mix.outputs[0], ramp.inputs["Fac"])
    nt.links.new(ramp.outputs["Color"], bsdf.inputs["Base Color"])
    bsdf.inputs["Roughness"].default_value = 0.45
    bsdf.inputs["Coat Weight"].default_value = 0.3
    bsdf.inputs["Coat Roughness"].default_value = 0.25
    nt.links.new(bsdf.outputs[0], out.inputs[0])
    return m


def _lathe(name: str, profile, segments: int = 64) -> bpy.types.Object:
    """Solide de révolution autour de X à partir d'un profil [(x, r), ...]."""
    import bmesh
    me = bpy.data.meshes.new(name)
    bm = bmesh.new()
    rings = []
    for (x, r) in profile:
        ring = []
        for i in range(segments):
            a = 2 * math.pi * i / segments
            ring.append(bm.verts.new((x, r * math.cos(a), r * math.sin(a))))
        rings.append(ring)
    for k in range(len(rings) - 1):
        for i in range(segments):
            j = (i + 1) % segments
            bm.faces.new((rings[k][i], rings[k][j], rings[k + 1][j], rings[k + 1][i]))
    bm.faces.new(list(reversed(rings[0])))
    bm.faces.new(rings[-1])
    bm.to_mesh(me)
    bm.free()
    for p in me.polygons:
        p.use_smooth = True
    return bpy.data.objects.new(name, me)


def _cap_profile(x0: float, sign: float):
    """Profil d'un pommeau tourné (bague, gorge, boule, pointe)."""
    pts = [
        (0.00, BODY_R), (0.04, 0.47), (0.08, 0.47), (0.11, 0.36),
        (0.17, 0.33), (0.24, 0.42), (0.34, 0.50), (0.44, 0.46),
        (0.52, 0.34), (0.58, 0.18), (0.62, 0.02),
    ]
    return [(x0 + sign * dx, r) for (dx, r) in pts]


def render_roll(out_dir: str) -> str:
    sc = _new_scene(1024, 160)
    # Ambiance faible : bois sombre (retour du test visuel du 25/11/2026).
    for n in sc.world.node_tree.nodes:
        if n.bl_idname == "ShaderNodeBackground":
            n.inputs[1].default_value = 0.12
    wood = _wood_material()
    half = BODY_LEN / 2
    body = _lathe("roll_body", [(-half, BODY_R), (half, BODY_R)])
    capl = _lathe("roll_cap_l", list(reversed(_cap_profile(-half, -1.0))))
    capr = _lathe("roll_cap_r", _cap_profile(half, 1.0))
    for o in (body, capl, capr):
        o.data.materials.append(wood)
        _link(sc, o)
    # Caméra de face (regarde +Y). Lumières en bandes parallèles à l'axe : le
    # reflet est uniforme le long du rouleau, le corps reste étirable.
    _camera_ortho(sc, VIEW_W, (0.0, -10.0, 0.0), (math.radians(90), 0.0, 0.0))
    for name, size_y, energy, loc, rx in (
        ("key", 1.2, 420.0, (0.0, -3.0, 3.0), 45.0),
        ("rim", 2.0, 80.0, (0.0, -2.0, -3.0), -145.0),
    ):
        ld = bpy.data.lights.new(name, "AREA")
        ld.shape = "RECTANGLE"
        ld.size = 14.0
        ld.size_y = size_y
        ld.energy = energy
        lo = _link(sc, bpy.data.objects.new(name, ld))
        lo.location = loc
        lo.rotation_euler = (math.radians(rx), 0.0, 0.0)
    path = os.path.join(out_dir, "map_roll.png")
    _render(sc, path)
    return path


# --------------------------------------------------------------------------
# Parchemin répétable et bord usé (plans en émission, caméra du dessus)
# --------------------------------------------------------------------------

def _torus_coords(nt, scale: float, x=-900, y=0):
    """(u, v) -> (cos 2πu, sin 2πu, cos 2πv)·s et W = sin 2πv·s : bruit 4D
    périodique en u et v (texture répétable sans couture)."""
    tc = _node(nt, "ShaderNodeTexCoord", x, y)
    sep = _node(nt, "ShaderNodeSeparateXYZ", x + 180, y)
    nt.links.new(tc.outputs["UV"], sep.inputs[0])
    outs = {}
    for axis in ("X", "Y"):
        m = _node(nt, "ShaderNodeMath", x + 360, y)
        m.operation = "MULTIPLY"
        m.inputs[1].default_value = 2 * math.pi
        nt.links.new(sep.outputs[axis], m.inputs[0])
        for fn in ("COSINE", "SINE"):
            f = _node(nt, "ShaderNodeMath", x + 540, y)
            f.operation = fn
            nt.links.new(m.outputs[0], f.inputs[0])
            s = _node(nt, "ShaderNodeMath", x + 720, y)
            s.operation = "MULTIPLY"
            s.inputs[1].default_value = scale
            nt.links.new(f.outputs[0], s.inputs[0])
            outs[axis + fn] = s.outputs[0]
    comb = _node(nt, "ShaderNodeCombineXYZ", x + 900, y)
    nt.links.new(outs["XCOSINE"], comb.inputs["X"])
    nt.links.new(outs["XSINE"], comb.inputs["Y"])
    nt.links.new(outs["YCOSINE"], comb.inputs["Z"])
    return comb.outputs[0], outs["YSINE"]


def _noise4d(nt, vec, w, scale, detail, rough, x, y):
    n = _node(nt, "ShaderNodeTexNoise", x, y)
    n.noise_dimensions = "4D"
    n.inputs["Scale"].default_value = scale
    n.inputs["Detail"].default_value = detail
    n.inputs["Roughness"].default_value = rough
    nt.links.new(vec, n.inputs["Vector"])
    nt.links.new(w, n.inputs["W"])
    return n.outputs["Fac"]


def _emission_plane(sc, name: str, mat: bpy.types.Material) -> None:
    bpy.ops.mesh.primitive_plane_add(size=2.0)
    p = bpy.context.active_object
    for c in list(p.users_collection):
        c.objects.unlink(p)
    _link(sc, p)
    p.name = name
    p.data.materials.append(mat)


def _parchment_material() -> bpy.types.Material:
    m = bpy.data.materials.new("ui_parchment")
    m.use_nodes = True
    nt = m.node_tree
    for n in list(nt.nodes):
        nt.nodes.remove(n)
    vec, w = _torus_coords(nt, 1.0)
    mottle = _noise4d(nt, vec, w, 1.6, 4.0, 0.55, 100, 200)    # marbrures larges
    grain = _noise4d(nt, vec, w, 18.0, 8.0, 0.65, 100, 0)      # grain fin
    stain = _noise4d(nt, vec, w, 0.9, 2.0, 0.4, 100, -200)     # taches rares
    ramp = _node(nt, "ShaderNodeValToRGB", 300, 200)
    e = ramp.color_ramp.elements
    e[0].position = 0.30
    e[0].color = (0.62, 0.50, 0.32, 1.0)   # parchemin ombré
    e[1].position = 0.70
    e[1].color = (0.80, 0.70, 0.50, 1.0)   # parchemin clair (linéaire)
    nt.links.new(mottle, ramp.inputs["Fac"])
    gmix = _node(nt, "ShaderNodeMix", 500, 100)
    gmix.data_type = "RGBA"
    gmix.blend_type = "OVERLAY"
    gmix.inputs["Factor"].default_value = 0.35
    nt.links.new(ramp.outputs["Color"], gmix.inputs["A"])
    nt.links.new(grain, gmix.inputs["B"])
    sramp = _node(nt, "ShaderNodeValToRGB", 300, -200)
    se = sramp.color_ramp.elements
    se[0].position = 0.62
    se[0].color = (1.0, 1.0, 1.0, 1.0)
    se[1].position = 0.78
    se[1].color = (0.80, 0.66, 0.46, 1.0)   # auréole brune
    nt.links.new(stain, sramp.inputs["Fac"])
    smix = _node(nt, "ShaderNodeMix", 700, 0)
    smix.data_type = "RGBA"
    smix.blend_type = "MULTIPLY"
    smix.inputs["Factor"].default_value = 1.0
    nt.links.new(gmix.outputs["Result"], smix.inputs["A"])
    nt.links.new(sramp.outputs["Color"], smix.inputs["B"])
    em = _node(nt, "ShaderNodeEmission", 900, 0)
    nt.links.new(smix.outputs["Result"], em.inputs["Color"])
    out = _node(nt, "ShaderNodeOutputMaterial", 1100, 0)
    nt.links.new(em.outputs[0], out.inputs[0])
    return m


def _edge_material() -> bpy.types.Material:
    """Bord usé : alpha fort près des 4 bords, irrégulier, nul au centre."""
    m = bpy.data.materials.new("ui_edge")
    m.use_nodes = True
    nt = m.node_tree
    for n in list(nt.nodes):
        nt.nodes.remove(n)
    tc = _node(nt, "ShaderNodeTexCoord", -900, 0)
    sep = _node(nt, "ShaderNodeSeparateXYZ", -720, 0)
    nt.links.new(tc.outputs["UV"], sep.inputs[0])
    # Distance au bord le plus proche : min(u, 1-u, v, 1-v).
    dist = None
    for axis in ("X", "Y"):
        inv = _node(nt, "ShaderNodeMath", -540, 0)
        inv.operation = "SUBTRACT"
        inv.inputs[0].default_value = 1.0
        nt.links.new(sep.outputs[axis], inv.inputs[1])
        mn = _node(nt, "ShaderNodeMath", -360, 0)
        mn.operation = "MINIMUM"
        nt.links.new(sep.outputs[axis], mn.inputs[0])
        nt.links.new(inv.outputs[0], mn.inputs[1])
        if dist is None:
            dist = mn.outputs[0]
        else:
            m2 = _node(nt, "ShaderNodeMath", -180, 0)
            m2.operation = "MINIMUM"
            nt.links.new(dist, m2.inputs[0])
            nt.links.new(mn.outputs[0], m2.inputs[1])
            dist = m2.outputs[0]
    vec, w = _torus_coords(nt, 1.0, -900, -400)
    rag = _noise4d(nt, vec, w, 6.0, 6.0, 0.6, -180, -300)
    jit = _node(nt, "ShaderNodeMath", 0, 0)
    jit.operation = "MULTIPLY_ADD"
    jit.inputs[1].default_value = 0.10          # irrégularité du bord
    nt.links.new(rag, jit.inputs[0])
    nt.links.new(dist, jit.inputs[2])
    a = _node(nt, "ShaderNodeMapRange", 200, 0)
    a.inputs["From Min"].default_value = 0.05
    a.inputs["From Max"].default_value = 0.30
    a.inputs["To Min"].default_value = 0.85
    a.inputs["To Max"].default_value = 0.0
    nt.links.new(jit.outputs[0], a.inputs["Value"])
    em = _node(nt, "ShaderNodeEmission", 400, 100)
    em.inputs["Color"].default_value = (0.20, 0.11, 0.045, 1.0)   # brun brûlé
    tr = _node(nt, "ShaderNodeBsdfTransparent", 400, -100)
    mix = _node(nt, "ShaderNodeMixShader", 600, 0)
    nt.links.new(a.outputs["Result"], mix.inputs[0])
    nt.links.new(tr.outputs[0], mix.inputs[1])
    nt.links.new(em.outputs[0], mix.inputs[2])
    out = _node(nt, "ShaderNodeOutputMaterial", 800, 0)
    nt.links.new(mix.outputs[0], out.inputs[0])
    return m


def _render_flat(out_dir: str, size: int, name: str, mat) -> str:
    sc = _new_scene(size, size)
    # Émission pure : aucune lumière d'environnement (couleurs exactes).
    for n in sc.world.node_tree.nodes:
        if n.bl_idname == "ShaderNodeBackground":
            n.inputs[1].default_value = 0.0
    sc.cycles.samples = 16
    # Filtre de pixel quasi nul : le plan couvre exactement le cadre, aucun
    # pixel de bord ne se mélange au fond (sinon un liseré apparaît au raccord
    # quand la texture est répétée).
    sc.cycles.filter_width = 0.01
    _emission_plane(sc, "plane_" + name, mat)
    _camera_ortho(sc, 2.0, (0.0, 0.0, 5.0), (0.0, 0.0, 0.0))
    path = os.path.join(out_dir, name + ".png")
    _render(sc, path)
    return path


def main() -> None:
    out_dir = _out_dir()
    os.makedirs(out_dir, exist_ok=True)
    produced = [
        render_roll(out_dir),
        _render_flat(out_dir, 512, "map_parchment", _parchment_material()),
        _render_flat(out_dir, 256, "map_edge", _edge_material()),
    ]
    for p in produced:
        print("[map_ui]", p)


main()
