#!/usr/bin/env python3
"""Génère les assets de personnalisation des personnages et leur catalogue.

Produit, pour les 5 races jouables (humains, elfes, nains, orcs, démons) et les
deux genres :

* les corps de race (dérivés des corps humains, même squelette) ;
* les têtes (3 visages, avec yeux, sourcils et oreilles), coiffures, barbes,
  moustaches, oreilles elfiques, défenses, cornes, queues et ailes, chacune en
  3 niveaux de détail (LOD0/1/2), au format glTF binaire (.glb) ;
* les textures de couleur (cheveux, yeux, corne, peaux orques et démoniaques) ;
* le catalogue ``game/data/configuration/customization/appearance_catalog.json``
  lu par le client (écran de création, avatar local, avatars des autres
  joueurs) et contrôlé en CI par ``validate_appearance.py``.

Toute la géométrie est paramétrique (numpy) : le script est déterministe et
peut être relancé à l'identique. Il n'a pas besoin de Blender ; Blender
(module ``bpy``) n'est utilisé que pour les rendus de contrôle
(``render_check.py``).

Lancement (depuis la racine du dépôt) ::

    python tools/asset_pipeline/character_appearance/build_character_appearance.py

Les fichiers existants sont réécrits. Licence : créations originales du
projet (CC0), tracées dans le catalogue (champ ``licence``).
"""

from __future__ import annotations

import hashlib
import json
import sys
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

from bodies import derive_body  # noqa: E402
from features import (HORN_MATERIAL, TUSK_MATERIAL, HORN_STYLES, TAIL_LENGTHS, TUSK_STYLES, WING_STYLES,  # noqa: E402
                      build_horns, build_tail, build_tusks, build_wings)
from geom import Mesh, merge  # noqa: E402
from glb_io import Primitive, blend_weights, load_skeleton, rigid_weights, write_part_glb  # noqa: E402
from hair import (BEARD_STYLES, HAIR_MATERIAL, HAIR_STYLES, MOUSTACHE_STYLES, build_beard_style,  # noqa: E402
                  build_brows, build_hair_style, build_moustache)
from heads import EAR_STYLES, HeadShape, build_ears, build_eye, build_head, eye_positions, face_variants  # noqa: E402
import textures  # noqa: E402

REPO = HERE.parents[2]
DATA = REPO / "game" / "data"
MODELS = DATA / "models" / "characters"
APPEARANCE_MODELS = MODELS / "appearance"
TEX = DATA / "textures" / "characters"
CATALOG = DATA / "configuration" / "customization" / "appearance_catalog.json"
REPORT = HERE / "appearance_report.json"

CATALOG_VERSION = 1
LICENCE = "CC0 — création originale du projet LCDLLN (générée par tools/asset_pipeline/character_appearance)"

GENDERS = ("male", "female")
GENDER_PREFIX = {"male": "Male", "female": "Female"}
# Position du point entre les yeux (espace Blender) pour les corps Ranger.
EYE_WORLD = {"male": np.array([0.0, -0.022, 1.705]), "female": np.array([0.0, -0.022, 1.652])}

# Budgets de triangles par catégorie (LOD0, LOD1, LOD2).
BUDGETS = {
    "face": (9500, 4500, 1800),
    "hair": (10500, 4200, 1600),
    "beard": (6000, 2400, 1000),
    "moustache": (1200, 500, 250),
    "ears": (1500, 700, 300),
    "horns": (2500, 1000, 400),
    "tusks": (800, 400, 200),
    "tail": (1500, 700, 300),
    "wings": (5000, 2000, 900),
}

# --------------------------------------------------------------------------
# Palettes (couleurs naturelles, pas trop riches — direction artistique).
# Les teintes sont appliquées par le moteur à une texture neutre (cf. textures.py).
# --------------------------------------------------------------------------
HAIR_COLORS = {
    "noir": ("Noir", "#1C1611"),
    "brun_fonce": ("Brun foncé", "#3A2416"),
    "chatain": ("Châtain", "#6A4428"),
    "auburn": ("Auburn", "#7E3A1E"),
    "roux": ("Roux", "#A2552C"),
    "blond": ("Blond", "#B9965A"),
    "blond_cendre": ("Blond cendré", "#A89A7E"),
    "blond_pale": ("Blond pâle", "#D3C49C"),
    "gris": ("Gris", "#86837E"),
    "blanc": ("Blanc", "#D2CFC7"),
    "argent": ("Argenté", "#BFC3C6"),
    "rouge_sang": ("Rouge sombre", "#4E1714"),
}
EYE_COLORS = {
    "brun": ("Brun", "#5A3A22"),
    "noisette": ("Noisette", "#7A5A2E"),
    "vert": ("Vert", "#4E6B3A"),
    "bleu": ("Bleu", "#4C6E8E"),
    "gris": ("Gris", "#7C8488"),
    "ambre": ("Ambre", "#A8782A"),
    "or": ("Or", "#C09A3A"),
    "rouge": ("Rouge", "#8A2420"),
}
# Teintes de peau : teinte multiplicative appliquée par le moteur à la texture de
# peau d'origine (None = texture telle quelle). Hex = couleur cible de la peau.
SKIN_TONES = {
    "humains": [("claire", "Claire", None), ("halee", "Hâlée", "#A57650"), ("foncee", "Foncée", "#5E3A26")],
    "elfes": [("claire", "Claire", None), ("halee", "Hâlée", "#A57650"), ("foncee", "Foncée", "#5E3A26")],
    "nains": [("claire", "Claire", None), ("halee", "Hâlée", "#A57650"), ("foncee", "Foncée", "#5E3A26")],
    "orcs": [("vert_gris", "Vert-de-gris", "#7E8C62"), ("olive", "Olive sombre", "#5E6A3E")],
    "demons": [("rouge_sombre", "Rouge sombre", "#8A3A30"), ("brun_braise", "Brun de braise", "#6A3A2C"),
               ("cendre", "Cendre", "#77706C")],
}

# --------------------------------------------------------------------------
# Options par race et genre (validées par le propriétaire le 23/11/2026)
# --------------------------------------------------------------------------
RACE_OPTIONS = {
    "humains": {
        "male": {"hair": ["none", "cropped", "short", "swept", "medium", "long", "ponytail"],
                 "beard": ["none", "short_beard", "full_beard", "long_beard", "goatee", "chin_strap"],
                 "moustache": ["none", "thin", "thick", "handlebar", "drooping"],
                 "hairColor": ["noir", "brun_fonce", "chatain", "auburn", "roux", "blond", "blond_cendre", "gris"],
                 "eyeColor": ["brun", "noisette", "vert", "bleu", "gris"]},
        "female": {"hair": ["short", "bob", "long", "ponytail", "high_tail", "bun", "braid", "twin_braids"],
                   "hairColor": ["noir", "brun_fonce", "chatain", "auburn", "roux", "blond", "blond_cendre",
                                 "gris"],
                   "eyeColor": ["brun", "noisette", "vert", "bleu", "gris"]},
    },
    "elfes": {
        "male": {"hair": ["short", "swept", "medium", "long", "ponytail", "high_tail"],
                 "ears": ["ear_elf_short", "ear_elf_long", "ear_elf_swept"],
                 "hairColor": ["blond_pale", "blond", "argent", "blanc", "noir", "chatain", "roux"],
                 "eyeColor": ["vert", "bleu", "gris", "ambre", "noisette"]},
        "female": {"hair": ["long", "bob", "braid", "twin_braids", "high_tail", "bun"],
                   "ears": ["ear_elf_short", "ear_elf_long", "ear_elf_swept"],
                   "hairColor": ["blond_pale", "blond", "argent", "blanc", "noir", "chatain", "roux"],
                   "eyeColor": ["vert", "bleu", "gris", "ambre", "noisette"]},
    },
    "nains": {
        "male": {"hair": ["none", "short", "medium", "long", "braid", "topknot"],
                 "beard": ["full_beard", "long_beard", "braided_beard", "short_beard", "none"],
                 "moustache": ["thick", "handlebar", "drooping", "thin", "none"],
                 "hairColor": ["roux", "auburn", "brun_fonce", "chatain", "blond", "noir", "gris", "blanc"],
                 "eyeColor": ["brun", "noisette", "bleu", "gris", "vert"]},
        "female": {"hair": ["twin_braids", "braid", "bun", "long", "high_tail", "bob"],
                   "hairColor": ["roux", "auburn", "brun_fonce", "chatain", "blond", "noir", "gris", "blanc"],
                   "eyeColor": ["brun", "noisette", "bleu", "gris", "vert"]},
    },
    "orcs": {
        "male": {"hair": ["none", "mohawk", "shaved_tail", "high_tail", "long", "wild"],
                 "tusks": ["tusks_small", "tusks_long", "tusks_wide"],
                 "hairColor": ["noir", "brun_fonce", "gris", "blanc", "rouge_sang"],
                 "eyeColor": ["ambre", "rouge", "brun", "or"]},
        "female": {"hair": ["high_tail", "mohawk", "shaved_tail", "long", "wild", "twin_braids"],
                   "tusks": ["tusks_small", "tusks_long", "tusks_wide"],
                   "hairColor": ["noir", "brun_fonce", "gris", "blanc", "rouge_sang"],
                   "eyeColor": ["ambre", "rouge", "brun", "or"]},
    },
    "demons": {
        "male": {"horns": ["horns_ram", "horns_back", "horns_up", "horns_short"],
                 "tail": ["tail_short", "tail_medium", "tail_long"],
                 "wings": ["none", "wings_bat", "wings_spiked", "wings_small"],
                 "hair": ["none", "short", "swept", "medium", "long", "high_tail"],
                 "hairColor": ["noir", "rouge_sang", "gris", "blanc", "brun_fonce"],
                 "eyeColor": ["or", "rouge", "ambre", "gris"]},
        "female": {"horns": ["horns_ram", "horns_back", "horns_up", "horns_short"],
                   "tail": ["tail_short", "tail_medium", "tail_long"],
                   "wings": ["none", "wings_bat", "wings_spiked", "wings_small"],
                   "hair": ["long", "bob", "high_tail", "braid", "short", "swept"],
                   "hairColor": ["noir", "rouge_sang", "gris", "blanc", "brun_fonce"],
                   "eyeColor": ["or", "rouge", "ambre", "gris"]},
    },
}
DEFAULT_EARS = {"humains": "ear_human", "nains": "ear_dwarf", "orcs": "ear_orc", "demons": "ear_demon"}

BODY_NAMES = {"elfes": "Elfe", "nains": "Nain", "orcs": "Orc", "demons": "Demon"}

SLOT_LABELS = {
    "face": "Visage", "skinTone": "Teinte de peau", "hair": "Coiffure", "hairColor": "Couleur des cheveux",
    "eyeColor": "Couleur des yeux", "beard": "Barbe", "moustache": "Moustache", "ears": "Oreilles",
    "horns": "Cornes", "tail": "Queue", "wings": "Ailes", "tusks": "Défenses",
}
NONE_LABEL = {"hair": "Rasé", "beard": "Aucune", "moustache": "Aucune", "wings": "Sans ailes"}
EAR_LABELS = {"ear_elf_short": "Pointues", "ear_elf_long": "Longues", "ear_elf_swept": "Effilées vers l'arrière"}

LOD_HEAD = [(64, 52, 22, 14), (40, 30, 14, 10), (24, 18, 8, 6)]


def rel(p: Path) -> str:
    """Chemin relatif à game/data (convention des chemins d'assets du moteur)."""
    return p.relative_to(DATA).as_posix()


class Writer:
    """Écrit les pièces .glb avec déduplication par contenu."""

    def __init__(self) -> None:
        self.by_hash: dict = {}
        self.stats: dict = {}
        self.files = 0
        self.bytes = 0

    def write(self, path: Path, skel, prims: list, name: str, category: str, lod: int) -> str:
        tmp = path.with_suffix(".tmp.glb")
        st = write_part_glb(tmp, skel, prims, name)
        data = tmp.read_bytes()
        h = hashlib.sha1(data).hexdigest()
        if h in self.by_hash:
            tmp.unlink()
            return self.by_hash[h]
        tmp.replace(path)
        budget = BUDGETS[category][lod]
        if st["triangles"] > budget:
            raise SystemExit(f"{path.name} : {st['triangles']} triangles > budget {budget}")
        self.by_hash[h] = rel(path)
        self.stats[rel(path)] = st
        self.files += 1
        self.bytes += len(data)
        return rel(path)


def prim(material: str, m: Mesh, joints_weights) -> Primitive:
    j, w = joints_weights
    return Primitive(material, m.v, m.normals(), m.uv, m.f.ravel(), j, w)


def build_textures() -> dict:
    """Génère les textures (cheveux, yeux, corne, ivoire) et calcule les teintes.

    Retourne les chemins (relatifs à game/data) et, pour la peau, la texture de
    base du genre et la teinte multiplicative de chaque teinte de peau.
    """
    out = {"skin": {}}
    p = TEX / "appearance" / "T_Hair.png"
    textures.hair_texture(p)
    out["hair"] = rel(p)
    p = TEX / "appearance" / "T_Eye.png"
    textures.eye_texture(p)
    out["eye"] = rel(p)
    p = TEX / "appearance" / "T_Horn.png"
    textures.horn_texture(p)
    out["horn"] = rel(p)
    p = TEX / "appearance" / "T_Tusk.png"
    textures.horn_texture(p, base="#D8CCB0", tip="#B8A27E")
    out["tusk"] = rel(p)
    human = TEX / "humains"
    for gender in GENDERS:
        G = GENDER_PREFIX[gender]
        src = human / f"T_Regular_{G}_BaseColor.png"
        ref = textures.median_skin(src)
        for race, tones in SKIN_TONES.items():
            for tid, _name, target in tones:
                tint = "#FFFFFF" if target is None else textures.tint_for(target, ref)
                out["skin"][(race, gender, tid)] = (rel(src), tint)
    return out


def build_body(race: str, gender: str) -> str:
    """Corps de base de la race (chemin relatif à game/data)."""
    G = GENDER_PREFIX[gender]
    src = MODELS / "humains" / f"{G}_Ranger" / f"{G}_Ranger.glb"
    if race == "humains":
        return rel(src)
    dst = MODELS / race / f"{G}_{BODY_NAMES[race]}" / f"{G}_{BODY_NAMES[race]}.glb"
    derive_body(src, dst, race)
    return rel(dst)


def main() -> None:
    writer = Writer()
    tex = build_textures()
    catalog = {
        "_comment": ("Catalogue de personnalisation des personnages (généré par "
                     "tools/asset_pipeline/character_appearance/build_character_appearance.py — ne pas éditer "
                     "à la main). L'apparence d'un personnage est un indice par emplacement (slots), dans l'ordre "
                     "des options listées ici."),
        "version": CATALOG_VERSION,
        "licence": LICENCE,
        "slotOrder": list(SLOT_LABELS.keys()),
        "raceOrder": list(RACE_OPTIONS.keys()),
        "slotLabels": SLOT_LABELS,
        "materials": {"skin": ["MI_Regular_Male", "MI_Regular_Female"], "hair": HAIR_MATERIAL,
                      "eye": "MI_Eye", "horn": HORN_MATERIAL, "tusk": TUSK_MATERIAL},
        "hornTexture": tex["horn"],
        "tuskTexture": tex["tusk"],
        "budgets": {k: list(v) for k, v in BUDGETS.items()},
        "races": {},
    }

    for race, per_gender in RACE_OPTIONS.items():
        catalog["races"][race] = {}
        for gender in GENDERS:
            opts = per_gender[gender]
            G = GENDER_PREFIX[gender]
            body = build_body(race, gender)
            skel = load_skeleton(MODELS / "humains" / f"{G}_Ranger" / f"{G}_Ranger.glb")
            hj = skel.joint_index("Head")
            nj = skel.joint_index("neck_01")
            pj = skel.joint_index("pelvis")
            sj = skel.joint_index("spine_03")
            ew = EYE_WORLD[gender]
            outdir = APPEARANCE_MODELS / race / gender
            outdir.mkdir(parents=True, exist_ok=True)
            skin_mat = f"MI_Regular_{G}"
            shapes = face_variants(race, gender)
            envelope = shapes[1]          # visage le plus large : sert d'appui aux barbes
            # Crâne de référence des coiffures : traits du visage humains, gabarit de la
            # race (échelle, cou, front fuyant). Les coiffures identiques entre races
            # sont ainsi dédupliquées au bit près.
            human0 = face_variants("humains", gender)[0]
            cranium = HeadShape(**{k: getattr(shapes[0], k) for k in shapes[0].__dataclass_fields__})
            for k in ("nose", "nose_width", "brow", "jaw", "chin", "cheek", "lips"):
                setattr(cranium, k, getattr(human0, k))
            slots = {}

            # ---- Visages (tête + yeux + sourcils + oreilles de la race) ----
            face_opts = []
            for vi, shape in enumerate(shapes):
                lods = []
                for lod, (nu, nv, eu, ev) in enumerate(LOD_HEAD):
                    head = build_head(shape, ew, nu, nv)
                    prims = [prim(skin_mat, head, blend_weights(head.v[:, 2], nj, hj, ew[2] - 0.15, ew[2] - 0.10))]
                    if race in DEFAULT_EARS:
                        ears = build_ears(DEFAULT_EARS[race], shape, ew, [14, 9, 6][lod], [10, 7, 5][lod])
                        prims.append(prim(skin_mat, ears, rigid_weights(len(ears.v), hj)))
                    eyes = merge([build_eye(c, r, eu, ev) for c, r in eye_positions(shape, ew)])
                    prims.append(prim("MI_Eye", eyes, rigid_weights(len(eyes.v), hj)))
                    brows = build_brows(shape, ew, lod, 1.4 if race in ("nains", "orcs") else 1.0)
                    prims.append(prim(HAIR_MATERIAL, brows, rigid_weights(len(brows.v), hj)))
                    lods.append(writer.write(outdir / f"face_{vi + 1}_lod{lod}.glb", skel, prims, "face", "face", lod))
                face_opts.append({"id": f"face_{vi + 1}", "name": f"Visage {vi + 1}", "lods": lods})
            slots["face"] = face_opts

            # ---- Teintes de peau ----
            slots["skinTone"] = [{"id": tid, "name": name, "basecolor": tex["skin"][(race, gender, tid)][0],
                                  "tint": tex["skin"][(race, gender, tid)][1], "hex": hexc or "#C8906A"}
                                 for tid, name, hexc in SKIN_TONES[race]]

            # ---- Coiffures ----
            hair_opts = []
            for hid in opts["hair"]:
                if hid == "none":
                    hair_opts.append({"id": "none", "name": NONE_LABEL["hair"], "lods": []})
                    continue
                lods = []
                for lod in range(3):
                    m = build_hair_style(hid, cranium, ew, lod)
                    lods.append(writer.write(outdir / f"hair_{hid}_lod{lod}.glb", skel,
                                             [prim(HAIR_MATERIAL, m, rigid_weights(len(m.v), hj))], "hair", "hair", lod))
                hair_opts.append({"id": hid, "name": HAIR_STYLES[hid][0], "lods": lods})
            slots["hair"] = hair_opts
            slots["hairColor"] = [{"id": c, "name": HAIR_COLORS[c][0], "hex": HAIR_COLORS[c][1],
                                   "texture": tex["hair"], "tint": textures.tint_for(HAIR_COLORS[c][1], (0.80,) * 3)}
                                  for c in opts["hairColor"]]
            slots["eyeColor"] = [{"id": c, "name": EYE_COLORS[c][0], "hex": EYE_COLORS[c][1],
                                  "texture": tex["eye"], "tint": textures.tint_for(EYE_COLORS[c][1], (0.80,) * 3)}
                                 for c in opts["eyeColor"]]

            # ---- Pilosité faciale ----
            for slot, styles, builder, names in (("beard", BEARD_STYLES, build_beard_style, BEARD_STYLES),
                                                 ("moustache", MOUSTACHE_STYLES, build_moustache, MOUSTACHE_STYLES)):
                if slot not in opts:
                    continue
                lst = []
                for sid in opts[slot]:
                    if sid == "none":
                        lst.append({"id": "none", "name": NONE_LABEL[slot], "lods": []})
                        continue
                    lods = []
                    for lod in range(3):
                        m = builder(sid, envelope, ew, lod)
                        lods.append(writer.write(outdir / f"{slot}_{sid}_lod{lod}.glb", skel,
                                                 [prim(HAIR_MATERIAL, m, rigid_weights(len(m.v), hj))], slot, slot, lod))
                    lst.append({"id": sid, "name": names[sid][0], "lods": lods})
                slots[slot] = lst

            # ---- Oreilles elfiques ----
            if "ears" in opts:
                lst = []
                for eid in opts["ears"]:
                    lods = []
                    for lod in range(3):
                        m = build_ears(eid, shapes[0], ew, [16, 10, 6][lod], [10, 7, 5][lod])
                        lods.append(writer.write(outdir / f"{eid}_lod{lod}.glb", skel,
                                                 [prim(skin_mat, m, rigid_weights(len(m.v), hj))], "ears", "ears", lod))
                    lst.append({"id": eid, "name": EAR_LABELS[eid], "lods": lods})
                slots["ears"] = lst

            # ---- Défenses ----
            if "tusks" in opts:
                lst = []
                for tid in opts["tusks"]:
                    lods = []
                    for lod in range(3):
                        m = build_tusks(tid, shapes[0], ew, lod)
                        lods.append(writer.write(outdir / f"{tid}_lod{lod}.glb", skel,
                                                 [prim(TUSK_MATERIAL, m, rigid_weights(len(m.v), hj))], "tusks", "tusks", lod))
                    lst.append({"id": tid, "name": TUSK_STYLES[tid][0], "lods": lods})
                slots["tusks"] = lst

            # ---- Cornes, queue, ailes (démons) ----
            if "horns" in opts:
                lst = []
                for hid in opts["horns"]:
                    lods = []
                    for lod in range(3):
                        m = build_horns(hid, shapes[0], ew, lod)
                        lods.append(writer.write(outdir / f"{hid}_lod{lod}.glb", skel,
                                                 [prim(HORN_MATERIAL, m, rigid_weights(len(m.v), hj))], "horns", "horns", lod))
                    lst.append({"id": hid, "name": HORN_STYLES[hid][0], "lods": lods})
                slots["horns"] = lst
            if "tail" in opts:
                lst = []
                root = np.array([0.0, -0.135, 0.985 if gender == "male" else 0.975])
                for tid in opts["tail"]:
                    lods = []
                    for lod in range(3):
                        m = build_tail(tid, root, lod)
                        lods.append(writer.write(outdir / f"{tid}_lod{lod}.glb", skel,
                                                 [prim(skin_mat, m, rigid_weights(len(m.v), pj))], "tail", "tail", lod))
                    lst.append({"id": tid, "name": TAIL_LENGTHS[tid][0], "lods": lods})
                slots["tail"] = lst
            if "wings" in opts:
                lst = []
                root_l = np.array([0.075, -0.150, 1.375 if gender == "male" else 1.33])
                for wid in opts["wings"]:
                    if wid == "none":
                        lst.append({"id": "none", "name": NONE_LABEL["wings"], "lods": []})
                        continue
                    lods = []
                    for lod in range(3):
                        m = build_wings(wid, root_l, lod, 1.0 if gender == "male" else 0.93)
                        lods.append(writer.write(outdir / f"{wid}_lod{lod}.glb", skel,
                                                 [prim(skin_mat, m, rigid_weights(len(m.v), sj))], "wings", "wings", lod))
                    lst.append({"id": wid, "name": WING_STYLES[wid][0], "lods": lods})
                slots["wings"] = lst

            catalog["races"][race][gender] = {
                "body": body,
                "hiddenBodyMeshes": ["Head_Hood"],
                "slots": {k: slots[k] for k in SLOT_LABELS if k in slots},
            }
            print(f"[appearance] {race}/{gender} : {', '.join(f'{k}={len(v)}' for k, v in slots.items())}")

    catalog["stats"] = {"files": writer.files, "bytes": writer.bytes}
    # Rapport détaillé (triangles, boîte englobante par pièce) hors du catalogue
    # client : lu par validate_appearance.py en CI.
    REPORT.write_text(json.dumps({"parts": writer.stats}, indent=1, ensure_ascii=False) + "\n", encoding="utf-8")
    CATALOG.parent.mkdir(parents=True, exist_ok=True)
    CATALOG.write_text(json.dumps(catalog, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print(f"[appearance] {writer.files} pièces, {writer.bytes / 1e6:.1f} Mo ; catalogue : {rel(CATALOG)}")


if __name__ == "__main__":
    main()
