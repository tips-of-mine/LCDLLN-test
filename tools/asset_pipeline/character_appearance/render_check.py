"""Rendus de contrôle des personnages personnalisés (Blender en module ``bpy``).

Assemble un corps et ses pièces d'après le catalogue, applique les textures
choisies (peau, cheveux, yeux, corne) et enregistre des vues de face, de
trois-quarts et de dos. Sert au contrôle visuel hors moteur (Blender ne
fournit pas de capture du viewport exploitable dans notre chaîne).

Lancement (Blender 5.x en module Python, ou ``blender -b -P``) ::

    LCDLLN_RENDER='[{"race":"humains","gender":"male","choice":{"hair":2}}]' \\
    LCDLLN_RENDER_OUT=/tmp/rendu python -c "exec(open('render_check.py').read())"

Chaque entrée de ``LCDLLN_RENDER`` donne un indice par emplacement (défaut 0).
"""

import json
import math
import os
from pathlib import Path

import bpy
import mathutils

REPO = Path(os.environ.get("LCDLLN_REPO", Path(__file__).resolve().parents[3] if "__file__" in globals() else "."))
DATA = REPO / "game" / "data"
CAT = json.loads((DATA / "configuration" / "customization" / "appearance_catalog.json").read_text(encoding="utf-8"))


def _clear():
    for o in list(bpy.data.objects):
        bpy.data.objects.remove(o)


def _material(mat, image_path, tint="#FFFFFF", masked=False):
    mat.use_nodes = True
    nt = mat.node_tree
    for n in list(nt.nodes):
        nt.nodes.remove(n)
    out = nt.nodes.new("ShaderNodeOutputMaterial")
    bsdf = nt.nodes.new("ShaderNodeBsdfPrincipled")
    img = nt.nodes.new("ShaderNodeTexImage")
    img.image = bpy.data.images.load(str(image_path), check_existing=True)
    # Même règle que le moteur : albedo × teinte (masquée par 1 - alpha pour les yeux).
    t = tuple(int(tint[i:i + 2], 16) / 255.0 for i in (1, 3, 5)) + (1.0,)
    mix = nt.nodes.new("ShaderNodeMix")
    mix.data_type = "RGBA"
    mix.blend_type = "MULTIPLY"
    mix.inputs["B"].default_value = t
    nt.links.new(img.outputs["Color"], mix.inputs["A"])
    if masked:
        inv = nt.nodes.new("ShaderNodeMath")
        inv.operation = "SUBTRACT"
        inv.inputs[0].default_value = 1.0
        nt.links.new(img.outputs["Alpha"], inv.inputs[1])
        nt.links.new(inv.outputs[0], mix.inputs["Factor"])
    else:
        mix.inputs["Factor"].default_value = 1.0
    nt.links.new(mix.outputs["Result"], bsdf.inputs["Base Color"])
    bsdf.inputs["Roughness"].default_value = 0.75
    nt.links.new(bsdf.outputs[0], out.inputs[0])


def assemble(race, gender, choice):
    """Importe corps + pièces ; retourne la hauteur du sommet (m)."""
    entry = CAT["races"][race][gender]
    slots = entry["slots"]
    bpy.ops.import_scene.gltf(filepath=str(DATA / entry["body"]))
    for slot in ("face", "hair", "beard", "moustache", "ears", "horns", "tail", "wings", "tusks"):
        if slot not in slots:
            continue
        opt = slots[slot][min(choice.get(slot, 0), len(slots[slot]) - 1)]
        if opt["lods"]:
            bpy.ops.import_scene.gltf(filepath=str(DATA / opt["lods"][choice.get("lod", 0)]))
    for o in list(bpy.data.objects):
        if any(h in o.name for h in entry["hiddenBodyMeshes"]):
            bpy.data.objects.remove(o)
    skin = slots["skinTone"][choice.get("skinTone", 0)]
    hair = slots["hairColor"][choice.get("hairColor", 0)]
    eye = slots["eyeColor"][choice.get("eyeColor", 0)]
    outfit = DATA / "textures" / "characters" / "humains" / "T_Ranger_BaseColor.png"
    for mat in bpy.data.materials:
        base = mat.name.split(".")[0]
        if base.startswith("MI_Regular"):
            _material(mat, DATA / skin["basecolor"], skin["tint"])
        elif base == "MI_Hair":
            _material(mat, DATA / hair["texture"], hair["tint"])
        elif base == "MI_Eye":
            _material(mat, DATA / eye["texture"], eye["tint"], masked=True)
        elif base == "MI_Horn":
            _material(mat, DATA / CAT["hornTexture"])
        elif base == "MI_Tusk":
            _material(mat, DATA / CAT["tuskTexture"])
        elif base.startswith("MI_Ranger"):
            _material(mat, outfit)


def render(out_prefix, views, target_z, ortho, res):
    sc = bpy.context.scene
    sc.render.engine = "CYCLES"
    sc.cycles.samples = int(os.environ.get("LCDLLN_SAMPLES", "24"))
    sc.cycles.device = "CPU"
    sc.render.resolution_x = res
    sc.render.resolution_y = res
    w = bpy.data.worlds.new("w")
    sc.world = w
    w.use_nodes = True
    w.node_tree.nodes["Background"].inputs[0].default_value = (0.26, 0.28, 0.33, 1)
    w.node_tree.nodes["Background"].inputs[1].default_value = 0.7
    for rot, en in (((math.radians(55), 0, math.radians(150)), 3.0), ((math.radians(60), 0, math.radians(-60)), 1.2)):
        light = bpy.data.lights.new("sun", "SUN")
        light.energy = en
        lo = bpy.data.objects.new("sun", light)
        sc.collection.objects.link(lo)
        lo.rotation_euler = rot
    pos = {"front": (0, 4, target_z), "q": (2.6, 3.0, target_z + 0.2), "back": (0, -4, target_z),
           "side": (4, 0, target_z), "bq": (-2.6, -3.0, target_z + 0.2)}
    files = []
    for name in views:
        cam = bpy.data.cameras.new("c")
        cam.type = "ORTHO"
        cam.ortho_scale = ortho
        co = bpy.data.objects.new("cam", cam)
        sc.collection.objects.link(co)
        co.location = pos[name]
        d = mathutils.Vector((0, 0, target_z)) - mathutils.Vector(pos[name])
        co.rotation_euler = d.to_track_quat("-Z", "Y").to_euler()
        sc.camera = co
        sc.render.filepath = f"{out_prefix}_{name}.png"
        bpy.ops.render.render(write_still=True)
        files.append(sc.render.filepath)
    return files


def main():
    jobs = json.loads(os.environ["LCDLLN_RENDER"])
    out = os.environ["LCDLLN_RENDER_OUT"]
    for i, job in enumerate(jobs):
        _clear()
        assemble(job["race"], job["gender"], job.get("choice", {}))
        render(f"{out}_{i:02d}", job.get("views", ["front", "q"]), job.get("z", 1.62), job.get("ortho", 0.55),
               job.get("res", 420))


main()
