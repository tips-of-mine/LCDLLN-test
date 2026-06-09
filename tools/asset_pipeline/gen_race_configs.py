#!/usr/bin/env python3
"""Génère les fichiers de configuration de customisation par race.

« Aligné sur l'existant » : la source de vérité des races (id, displayName,
description, palettes de couleurs) reste ``game/data/races/races.json`` (celui
utilisé par le MVP de création de personnage, ``CharacterCreationPresenter``).

Ce script lit ``races.json`` et émet un fichier
``game/data/configuration/races/<id>.json`` par race, en y ajoutant la couche
de customisation (limites physiques, types de corps, têtes, cheveux, pilosité,
traits raciaux, morph targets, gameplay) consommée par
``CharacterCustomizationSystem`` (C++).

Ré-exécuter après toute modification de ``races.json`` ou des tables ci-dessous :

    python3 tools/asset_pipeline/gen_race_configs.py

Le script est idempotent : il réécrit intégralement les fichiers de sortie.
"""

import json
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
RACES_JSON = REPO / "game" / "data" / "races" / "races.json"
OUT_DIR = REPO / "game" / "data" / "configuration" / "races"

CONFIG_VERSION = "1.0.0"

# Liste de noms de features raciales connue côté C++ (probe). Garder synchronisé
# avec kKnownRacialFeatures dans CharacterCustomizationSystem.cpp.
# tusks, horns, tails, ears, scales, wings, halos, mutations

# Morph targets « visage » par défaut (silhouette humanoïde générique).
DEFAULT_FACE_MORPHS = [
    ("faceWidth", -1.0, 1.0, 0.0, "Largeur du visage"),
    ("jawWidth", -1.0, 1.0, 0.0, "Largeur de la mâchoire"),
    ("noseSize", -1.0, 1.0, 0.0, "Taille du nez"),
    ("cheekbones", -1.0, 1.0, 0.0, "Pommettes"),
    ("eyeSize", -0.5, 0.5, 0.0, "Taille des yeux"),
    ("lipThickness", -0.5, 0.5, 0.0, "Épaisseur des lèvres"),
]

# Table structurelle par race (clé = id existant dans races.json).
# Champs :
#   height            : (base_m, scale_min, scale_max, scale_default)
#   bodyMass          : (min, max, default)
#   proportions       : {legLength|shoulderWidth|torsoWidth: (min, max, default)}
#   collision         : (radius, height_m)
#   bodyTypes         : {male:[(id, displayName, description)], female:[...]}
#   headCount         : {male:N, female:M}
#   hair              : {male:[(id, displayName)], female:[...]}
#   facialHair        : [(id, displayName)]  (male uniquement)
#   features          : {featureName:[(id, displayName)]}
#   faceMorphs        : override optionnel de DEFAULT_FACE_MORPHS
#   movement          : {walk, run, sprint[, flight]}
#   combat            : dict combatStats
#   additionalAnims   : [str]
#   factions          : {allowed:[...], forbidden:[...]}
#   eyeEmissive       : bool (yeux lumineux)
PROP_NEUTRAL = {
    "legLength": (0.95, 1.05, 1.0),
    "shoulderWidth": (0.90, 1.10, 1.0),
    "torsoWidth": (0.95, 1.05, 1.0),
}

RACE_SPECS = {
    "humains": {
        "height": (1.75, 0.90, 1.10, 1.0),
        "bodyMass": (-0.3, 0.5, 0.0),
        "proportions": PROP_NEUTRAL,
        "collision": (0.45, 1.75),
        "bodyTypes": {
            "male": [("base", "Normal", "Corpulence moyenne équilibrée"),
                     ("muscular", "Musclé", "Musculature développée"),
                     ("lean", "Élancé", "Silhouette fine et athlétique")],
            "female": [("base", "Normal", "Corpulence moyenne équilibrée"),
                       ("athletic", "Athlétique", "Silhouette tonique et musclée"),
                       ("curvy", "Généreuse", "Formes accentuées")],
        },
        "headCount": {"male": 5, "female": 5},
        "hair": {
            "male": [("short_01", "Court classique"), ("short_02", "Court décoiffé"),
                     ("medium_01", "Mi-long"), ("long_01", "Long"), ("bald", "Chauve")],
            "female": [("short_01", "Court"), ("medium_01", "Mi-long"),
                       ("long_01", "Long lisse"), ("long_02", "Long ondulé"),
                       ("braided_01", "Tresse"), ("ponytail_01", "Queue de cheval")],
        },
        "facialHair": [("none", "Aucune"), ("beard_full_01", "Barbe complète"),
                       ("beard_goatee_01", "Bouc"), ("beard_short_01", "Barbe courte"),
                       ("mustache_01", "Moustache")],
        "features": {},
        "movement": {"walk": 3.5, "run": 7.0, "sprint": 10.5},
        "combat": {"baseHealth": 100, "baseStamina": 100, "baseMana": 100},
        "additionalAnims": [],
        "factions": {"allowed": ["alliance_light", "neutral"], "forbidden": ["demons_faction"]},
    },
    "elfes": {
        "height": (1.80, 0.92, 1.08, 1.0),
        "bodyMass": (-0.4, 0.2, -0.1),
        "proportions": {
            "legLength": (0.98, 1.08, 1.03),
            "shoulderWidth": (0.88, 1.02, 0.95),
            "torsoWidth": (0.92, 1.02, 0.97),
        },
        "collision": (0.42, 1.80),
        "bodyTypes": {
            "male": [("base", "Élancé", "Silhouette élégante et athlétique"),
                     ("lean", "Svelte", "Constitution fine et agile")],
            "female": [("base", "Élancée", "Silhouette gracieuse"),
                       ("athletic", "Athlétique", "Musculature tonique de chasseresse")],
        },
        "headCount": {"male": 3, "female": 3},
        "hair": {
            "male": [("long_01", "Long lisse"), ("long_02", "Long ondulé"),
                     ("braided_01", "Tresses"), ("medium_01", "Mi-long")],
            "female": [("long_01", "Long lisse"), ("long_02", "Long ondulé"),
                       ("long_03", "Très long"), ("braided_01", "Tresses")],
        },
        "facialHair": [("none", "Aucune"), ("beard_thin_01", "Barbe fine")],
        "features": {"ears": [("pointed_short", "Oreilles courtes"),
                              ("pointed_medium", "Oreilles moyennes"),
                              ("pointed_long", "Oreilles longues")]},
        "faceMorphs": [
            ("faceWidth", -0.8, 0.5, -0.2, "Largeur du visage"),
            ("jawWidth", -0.8, 0.3, -0.3, "Largeur de la mâchoire"),
            ("cheekbones", -0.5, 1.0, 0.5, "Pommettes"),
            ("eyeSize", -0.3, 0.6, 0.2, "Taille des yeux"),
        ],
        "movement": {"walk": 3.8, "run": 7.5, "sprint": 11.3},
        "combat": {"baseHealth": 90, "baseStamina": 110, "baseMana": 120,
                   "bonusDexterity": 4, "bonusPerception": 3},
        "additionalAnims": [],
        "factions": {"allowed": ["alliance_light", "neutral"], "forbidden": ["demons_faction"]},
    },
    "orcs": {
        "height": (2.00, 1.05, 1.15, 1.10),
        "bodyMass": (0.2, 0.8, 0.5),
        "proportions": {
            "legLength": (0.95, 1.00, 0.97),
            "shoulderWidth": (1.10, 1.25, 1.15),
            "torsoWidth": (1.05, 1.15, 1.10),
        },
        "collision": (0.55, 2.00),
        "bodyTypes": {
            "male": [("base", "Guerrier", "Musculature développée"),
                     ("massive", "Massif", "Corpulence imposante"),
                     ("hulking", "Colosse", "Stature titanesque")],
            "female": [("base", "Guerrière", "Musculature athlétique"),
                       ("strong", "Puissante", "Force brute accentuée")],
        },
        "headCount": {"male": 3, "female": 2},
        "hair": {
            "male": [("mohawk_01", "Crête"), ("topknot_01", "Chignon guerrier"),
                     ("braids_01", "Tresses"), ("shaved", "Rasé")],
            "female": [("long_01", "Long"), ("braids_01", "Tresses")],
        },
        "facialHair": [("none", "Aucune"), ("beard_braided_01", "Barbe tressée")],
        "features": {"tusks": [("small", "Petites défenses"), ("medium", "Défenses moyennes"),
                               ("large", "Grandes défenses"), ("broken", "Défenses brisées")]},
        "faceMorphs": [
            ("faceWidth", 0.0, 1.0, 0.5, "Largeur du visage"),
            ("jawWidth", 0.3, 1.0, 0.7, "Largeur de la mâchoire"),
            ("browRidge", 0.0, 1.0, 0.5, "Arcade sourcilière"),
        ],
        "movement": {"walk": 3.2, "run": 6.5, "sprint": 9.8},
        "combat": {"baseHealth": 130, "baseStamina": 120, "baseMana": 70,
                   "bonusStrength": 5, "bonusEndurance": 3},
        "additionalAnims": ["orcs_specific"],
        "factions": {"allowed": ["dzorak_horde", "neutral"], "forbidden": ["alliance_light"]},
    },
    "nains": {
        "height": (1.30, 0.75, 0.85, 0.80),
        "bodyMass": (0.3, 0.7, 0.5),
        "proportions": {
            "legLength": (0.85, 0.95, 0.90),
            "shoulderWidth": (1.10, 1.20, 1.15),
            "torsoWidth": (1.05, 1.15, 1.10),
        },
        "collision": (0.40, 1.30),
        "bodyTypes": {
            "male": [("base", "Robuste", "Carrure typique naine"),
                     ("stout", "Trapu", "Corpulence solide"),
                     ("broad", "Massif", "Épaules larges et puissantes")],
            "female": [("base", "Robuste", "Carrure typique naine"),
                       ("stout", "Trapue", "Corpulence solide"),
                       ("broad", "Massive", "Épaules larges et puissantes")],
        },
        "headCount": {"male": 3, "female": 3},
        "hair": {
            "male": [("short_01", "Court"), ("medium_01", "Mi-long")],
            "female": [("short_01", "Court"), ("medium_01", "Mi-long"),
                       ("braided_01", "Tresses"), ("long_01", "Long")],
        },
        "facialHair": [("beard_braided_long", "Longue barbe tressée"),
                       ("beard_braided_short", "Barbe tressée courte"),
                       ("beard_forked", "Barbe fourchue"),
                       ("beard_full_massive", "Barbe massive"),
                       ("beard_full_medium", "Barbe fournie")],
        "features": {},
        "faceMorphs": [
            ("faceWidth", 0.0, 1.0, 0.6, "Largeur du visage"),
            ("noseSize", 0.0, 1.0, 0.5, "Taille du nez"),
        ],
        "movement": {"walk": 3.0, "run": 6.0, "sprint": 9.0},
        "combat": {"baseHealth": 120, "baseStamina": 130, "baseMana": 80,
                   "bonusEndurance": 5, "bonusStrength": 3,
                   "resistances": {"poison": 10, "physical": 5}},
        "additionalAnims": ["nains_specific"],
        "factions": {"allowed": ["alliance_light", "neutral"],
                     "forbidden": ["demons_faction", "dzorak_horde"]},
    },
    "morts_vivants": {
        "height": (1.75, 0.90, 1.10, 1.0),
        "bodyMass": (-0.5, 0.3, -0.1),
        "proportions": PROP_NEUTRAL,
        "collision": (0.45, 1.75),
        "bodyTypes": {
            "male": [("base", "Décharné", "Silhouette émaciée de revenant"),
                     ("gaunt", "Squelettique", "Chair desséchée sur les os")],
            "female": [("base", "Décharnée", "Silhouette émaciée de revenante"),
                       ("gaunt", "Squelettique", "Chair desséchée sur les os")],
        },
        "headCount": {"male": 3, "female": 3},
        "hair": {
            "male": [("short_01", "Court clairsemé"), ("bald", "Crâne nu")],
            "female": [("long_01", "Long décrépit"), ("bald", "Crâne nu")],
        },
        "facialHair": [("none", "Aucune")],
        "features": {},
        "movement": {"walk": 3.3, "run": 6.6, "sprint": 9.9},
        "combat": {"baseHealth": 110, "baseStamina": 100, "baseMana": 110,
                   "resistances": {"shadow": 20, "disease": 25}},
        "additionalAnims": [],
        "factions": {"allowed": ["neutral"], "forbidden": ["alliance_light"]},
    },
    "divins": {
        "height": (1.80, 0.95, 1.10, 1.02),
        "bodyMass": (-0.3, 0.3, 0.0),
        "proportions": PROP_NEUTRAL,
        "collision": (0.45, 1.80),
        "bodyTypes": {
            "male": [("base", "Élancé", "Silhouette noble et droite"),
                     ("radiant", "Rayonnant", "Carrure athlétique de champion")],
            "female": [("base", "Élancée", "Silhouette noble et droite"),
                       ("radiant", "Rayonnante", "Carrure athlétique de championne")],
        },
        "headCount": {"male": 3, "female": 3},
        "hair": {
            "male": [("short_01", "Court"), ("medium_01", "Mi-long"), ("long_01", "Long")],
            "female": [("long_01", "Long lisse"), ("long_02", "Long ondulé"),
                       ("braided_01", "Tresses")],
        },
        "facialHair": [("none", "Aucune"), ("beard_short_01", "Barbe courte")],
        "features": {"halos": [("none", "Aucun"), ("thin", "Halo discret"),
                               ("radiant", "Halo rayonnant")]},
        "movement": {"walk": 3.6, "run": 7.2, "sprint": 10.8},
        "combat": {"baseHealth": 115, "baseStamina": 100, "baseMana": 125,
                   "bonusWisdom": 4, "resistances": {"shadow": 20}},
        "additionalAnims": [],
        "factions": {"allowed": ["alliance_light", "neutral"], "forbidden": ["demons_faction"]},
        "eyeEmissive": True,
    },
    "demons": {
        "height": (1.85, 0.95, 1.12, 1.03),
        "bodyMass": (-0.2, 0.6, 0.2),
        "proportions": {
            "legLength": (0.95, 1.05, 1.0),
            "shoulderWidth": (0.95, 1.15, 1.05),
            "torsoWidth": (0.95, 1.08, 1.0),
        },
        "collision": (0.48, 1.85),
        "bodyTypes": {
            "male": [("base", "Athlétique", "Silhouette élancée et musclée"),
                     ("muscular", "Musclé", "Musculature accentuée")],
            "female": [("base", "Athlétique", "Silhouette élancée"),
                       ("athletic", "Athlétique tonique", "Musculature définie")],
        },
        "headCount": {"male": 3, "female": 2},
        "hair": {
            "male": [("short_01", "Court"), ("medium_01", "Mi-long")],
            "female": [("long_01", "Long"), ("medium_01", "Mi-long")],
        },
        "facialHair": [("none", "Aucune"), ("goatee_01", "Bouc")],
        "features": {
            "horns": [("none", "Aucune"), ("curved_01", "Cornes courbes"),
                      ("curved_02", "Cornes recourbées"), ("straight_01", "Cornes droites"),
                      ("straight_02", "Cornes pointues"), ("broken_01", "Cornes brisées"),
                      ("ram_style", "Cornes de bélier")],
            "tails": [("none", "Aucune"), ("spaded_long", "Queue longue à pointe"),
                      ("spaded_short", "Queue courte à pointe"), ("thin_long", "Queue fine longue")],
        },
        "faceMorphs": [
            ("faceWidth", -0.5, 0.8, 0.2, "Largeur du visage"),
            ("jawWidth", -0.5, 1.0, 0.3, "Largeur de la mâchoire"),
            ("cheekbones", -0.5, 1.0, 0.4, "Pommettes"),
        ],
        "movement": {"walk": 3.7, "run": 7.3, "sprint": 11.0},
        "combat": {"baseHealth": 110, "baseStamina": 90, "baseMana": 130,
                   "bonusIntelligence": 3, "bonusStrength": 2,
                   "resistances": {"fire": 20, "shadow": 15}},
        "additionalAnims": ["demons_specific"],
        "factions": {"allowed": ["demons_faction"], "forbidden": ["alliance_light", "dzorak_horde"]},
        "eyeEmissive": True,
    },
}


def color_entries(prefix, hexes, race_id, tex_subdir, emissive=False):
    """Convertit une liste de hex en entrées id/displayName/hex/texture."""
    out = []
    for i, hx in enumerate(hexes, start=1):
        entry = {
            "id": f"{prefix}_{i:02d}",
            "displayName": f"Teinte {i}",
            "hex": hx,
            "texture": f"textures/characters/{race_id}/{tex_subdir}/{prefix}_{i:02d}.png",
        }
        if emissive:
            entry["emissive"] = True
        out.append(entry)
    return out


def skin_entries(hexes, race_id):
    out = []
    for i, hx in enumerate(hexes, start=1):
        base = f"textures/characters/{race_id}/skin/skin_{i:02d}"
        out.append({
            "id": f"skin_{i:02d}",
            "displayName": f"Peau {i}",
            "hex": hx,
            "diffuse": f"{base}_diffuse.png",
            "normal": f"{base}_normal.png",
            "orm": f"{base}_orm.png",
        })
    return out


def rng(t):
    return {"min": t[0], "max": t[1], "default": t[2]}


def build_race(race_id, race_src, spec):
    h = spec["height"]
    prop = spec["proportions"]
    coll = spec["collision"]

    def modules(kind, gender, items):
        return [{"id": iid, "model": f"models/characters/{race_id}/{kind}/{gender}/{iid}.fbx",
                 "displayName": dn} for (iid, dn) in items]

    cfg = {
        "version": CONFIG_VERSION,
        "raceId": race_id,
        "displayName": race_src.get("displayName", race_id),
        "description": race_src.get("description", ""),
        "physicalLimits": {
            "height": {
                "baseMeters": h[0],
                "scaleRange": {"min": h[1], "max": h[2], "default": h[3]},
            },
            "bodyMass": {"range": rng(spec["bodyMass"])},
            "proportions": {
                "legLength": {"range": {"min": prop["legLength"][0], "max": prop["legLength"][1]},
                              "default": prop["legLength"][2]},
                "shoulderWidth": {"range": {"min": prop["shoulderWidth"][0], "max": prop["shoulderWidth"][1]},
                                  "default": prop["shoulderWidth"][2]},
                "torsoWidth": {"range": {"min": prop["torsoWidth"][0], "max": prop["torsoWidth"][1]},
                               "default": prop["torsoWidth"][2]},
            },
        },
        "baseSkeleton": "humanoid_base",
        "collisionDefaults": {"radius": coll[0], "height": coll[1]},
        "genders": ["male", "female"],
        "bodyTypes": {
            g: [{"id": bid, "model": f"models/characters/{race_id}/base/{g}_body_{bid}.fbx",
                 "displayName": dn, "description": desc}
                for (bid, dn, desc) in spec["bodyTypes"][g]]
            for g in ("male", "female")
        },
        "heads": {
            g: [{"id": f"head_{i:02d}",
                 "model": f"models/characters/{race_id}/heads/{g}/head_{i:02d}.fbx",
                 "displayName": f"Visage {i}"}
                for i in range(1, spec["headCount"][g] + 1)]
            for g in ("male", "female")
        },
        "hairStyles": {g: modules("hair", g, spec["hair"][g]) for g in spec["hair"]},
        "facialHair": {
            "male": [{"id": iid, "model": f"models/characters/{race_id}/facial_hair/{iid}.fbx",
                      "displayName": dn} for (iid, dn) in spec["facialHair"]]
        },
        "skinTones": skin_entries(race_src.get("defaultSkinColors", []), race_id),
        "hairColors": color_entries("hair", race_src.get("defaultHairColors", []), race_id, "hair"),
        "eyeColors": color_entries("eye", race_src.get("defaultEyeColors", []), race_id, "eyes",
                                   emissive=spec.get("eyeEmissive", False)),
        "morphTargets": {
            "face": [{"name": n, "min": mn, "max": mx, "default": df, "displayName": dn}
                     for (n, mn, mx, df, dn) in spec.get("faceMorphs", DEFAULT_FACE_MORPHS)],
            "body": [{"name": "bodyMass", "min": spec["bodyMass"][0], "max": spec["bodyMass"][1],
                      "default": spec["bodyMass"][2], "displayName": "Corpulence"}],
        },
        "animationSet": "humanoid_base",
        "gameplay": {"movementSpeed": spec["movement"], "combatStats": spec["combat"]},
        "factionRestrictions": spec["factions"],
    }

    if spec.get("features"):
        cfg["racialFeatures"] = {
            feat: [{"id": iid, "model": f"models/characters/{race_id}/{feat}/{iid}.fbx",
                    "displayName": dn} for (iid, dn) in items]
            for feat, items in spec["features"].items()
        }

    if spec.get("additionalAnims"):
        cfg["additionalAnimations"] = spec["additionalAnims"]

    return cfg


def main():
    races = json.loads(RACES_JSON.read_text(encoding="utf-8"))["races"]
    by_id = {r["id"]: r for r in races}
    OUT_DIR.mkdir(parents=True, exist_ok=True)

    written = 0
    for race_id, spec in RACE_SPECS.items():
        if race_id not in by_id:
            print(f"[WARN] race '{race_id}' absente de races.json — ignorée")
            continue
        cfg = build_race(race_id, by_id[race_id], spec)
        out = OUT_DIR / f"{race_id}.json"
        out.write_text(json.dumps(cfg, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
        print(f"[OK] {out.relative_to(REPO)}")
        written += 1

    missing = set(by_id) - set(RACE_SPECS)
    if missing:
        print(f"[INFO] races dans races.json sans spec de customisation : {sorted(missing)}")
    print(f"\n{written} fichier(s) de configuration de race générés.")


if __name__ == "__main__":
    main()
