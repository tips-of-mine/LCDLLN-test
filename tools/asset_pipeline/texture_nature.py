#!/usr/bin/env python3
# Injecte les references de textures dans les .gltf des props NATURE de sous-bois
# (buissons, fougeres, fleurs, champignons, rochers, plantes, trefles) du pack
# Stylized Nature MegaKit. Meme principe que texture_trees.py : ajoute images/
# textures/samplers et cable chaque materiau vers sa texture (presente dans
# game/data/meshes/props/). alphaMode laisse tel quel -> le moteur active la
# decoupe alpha (flag AlphaCutout) pour les materiaux BLEND.
#
# Idempotent. Lancement : python tools/asset_pipeline/texture_nature.py
import json
import os

REPO = os.path.normpath(os.path.join(os.path.dirname(__file__), "..", ".."))
PROPS = os.path.join(REPO, "game", "data", "meshes", "props")

# Materiau glTF -> {base: BaseColor.png, normal?: Normal.png}. Toutes ces textures
# sont dans game/data/meshes/props/ (copiees depuis le pack).
MAT_TEX = {
    "Leaves":             {"base": "Leaves.png"},
    "Flowers":            {"base": "Flowers.png"},
    "Mushrooms":          {"base": "Mushrooms.png"},
    "Rocks":              {"base": "Rocks_Diffuse.png"},
    "PathRocks":          {"base": "PathRocks_Diffuse.png"},
    "Leaves_NormalTree":  {"base": "Leaves_NormalTree.png"},
    "Leaves_TwistedTree": {"base": "Leaves_TwistedTree.png"},
    "Leaves_Pine":        {"base": "Leaf_Pine.png"},
    "Bark_NormalTree":    {"base": "Bark_NormalTree.png",  "normal": "Bark_NormalTree_Normal.png"},
    "Bark_DeadTree":      {"base": "Bark_DeadTree.png",    "normal": "Bark_DeadTree_Normal.png"},
    "Bark_TwistedTree":   {"base": "Bark_TwistedTree.png", "normal": "Bark_TwistedTree_Normal.png"},
}

# Props decor de sous-bois a texturer.
DECOR = [
    "Bush_Common", "Bush_Common_Flowers", "Fern_1",
    "Flower_3_Group", "Flower_4_Group",
    "Mushroom_Common", "Mushroom_Laetiporus",
    "Rock_Medium_1", "Rock_Medium_2", "Rock_Medium_3",
    "Plant_1", "Plant_1_Big", "Plant_7",
    "Clover_1", "Clover_2",
]


def process(name):
    path = os.path.join(PROPS, name + ".gltf")
    if not os.path.exists(path):
        return False, "absent"
    with open(path, "r", encoding="utf-8") as f:
        d = json.load(f)
    mats = d.get("materials", [])
    if not mats:
        return False, "aucun materiau"
    for m in mats:
        if m.get("name") not in MAT_TEX:
            return False, f"materiau inconnu '{m.get('name')}'"

    uri_to_img, images = {}, []
    def img_index(uri):
        if uri not in uri_to_img:
            uri_to_img[uri] = len(images)
            images.append({"uri": uri})
        return uri_to_img[uri]

    samplers = [{"magFilter": 9729, "minFilter": 9987}]
    textures, img_to_tex = [], {}
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
    return True, f"{len(images)} image(s)"


def main():
    ok = 0
    for n in DECOR:
        done, msg = process(n)
        print(f"[texture_nature] {'OK ' if done else 'SKIP'} {n} : {msg}")
        if done:
            ok += 1
    print(f"[texture_nature] {ok}/{len(DECOR)} props decor textures")


main()
