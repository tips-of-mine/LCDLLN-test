#!/usr/bin/env python3
# Injecte les references de textures dans les .gltf des arbres (Stylized Nature
# MegaKit). Les arbres sont exportes sans texture ; ce script ajoute images/textures/
# samplers et cable chaque materiau (Bark_* / Leaves_*) vers ses textures, presentes
# dans game/data/meshes/props/. alphaMode des feuillages laisse tel quel (BLEND) ->
# le moteur le lit et active la decoupe alpha (flag AlphaCutout).
#
# Idempotent : reconstruit entierement images/textures/samplers a chaque execution.
# Lancement : python tools/asset_pipeline/texture_trees.py
import json
import os
import glob

REPO = os.path.normpath(os.path.join(os.path.dirname(__file__), "..", ".."))
PROPS = os.path.join(REPO, "game", "data", "meshes", "props")

# Materiau glTF -> {base: BaseColor.png, normal: Normal.png (optionnel)}.
MAT_TEX = {
    "Bark_NormalTree":    {"base": "Bark_NormalTree.png",    "normal": "Bark_NormalTree_Normal.png"},
    "Bark_DeadTree":      {"base": "Bark_DeadTree.png",      "normal": "Bark_DeadTree_Normal.png"},
    "Bark_TwistedTree":   {"base": "Bark_TwistedTree.png",   "normal": "Bark_TwistedTree_Normal.png"},
    "Leaves_NormalTree":  {"base": "Leaves_NormalTree.png"},
    "Leaves_Pine":        {"base": "Leaf_Pine.png"},
    "Leaves_TwistedTree": {"base": "Leaves_TwistedTree.png"},
}

SPECIES = ["CommonTree", "Pine", "DeadTree", "TwistedTree"]


def process(path):
    with open(path, "r", encoding="utf-8") as f:
        d = json.load(f)
    mats = d.get("materials", [])
    if not mats:
        return False, "aucun materiau"

    # Verifie que les materiaux sont connus.
    for m in mats:
        if m.get("name") not in MAT_TEX:
            return False, f"materiau inconnu '{m.get('name')}'"

    # Collecte les URIs utilisees (ordre stable), construit images + textures.
    uri_to_img = {}
    images = []
    def img_index(uri):
        if uri not in uri_to_img:
            uri_to_img[uri] = len(images)
            images.append({"uri": uri})
        return uri_to_img[uri]

    # Sampler unique (filtres lineaires + mipmaps ; wrap REPEAT par defaut glTF).
    samplers = [{"magFilter": 9729, "minFilter": 9987}]
    textures = []
    img_to_tex = {}
    def tex_index(uri):
        ii = img_index(uri)
        if ii not in img_to_tex:
            img_to_tex[ii] = len(textures)
            textures.append({"sampler": 0, "source": ii})
        return img_to_tex[ii]

    for m in mats:
        spec = MAT_TEX[m["name"]]
        pbr = m.setdefault("pbrMetallicRoughness", {})
        pbr["baseColorTexture"] = {"index": tex_index(spec["base"])}
        if "normal" in spec:
            m["normalTexture"] = {"index": tex_index(spec["normal"])}

    d["images"] = images
    d["textures"] = textures
    d["samplers"] = samplers

    with open(path, "w", encoding="utf-8", newline="\n") as f:
        json.dump(d, f, separators=(",", ":"), ensure_ascii=False)
    return True, f"{len(images)} image(s), {len(textures)} texture(s)"


def main():
    files = []
    for sp in SPECIES:
        files += sorted(glob.glob(os.path.join(PROPS, f"{sp}_*.gltf")))
    ok = 0
    for path in files:
        done, msg = process(path)
        tag = "OK " if done else "SKIP"
        print(f"[texture_trees] {tag} {os.path.basename(path)} : {msg}")
        if done:
            ok += 1
    print(f"[texture_trees] {ok}/{len(files)} arbres textures")


main()
