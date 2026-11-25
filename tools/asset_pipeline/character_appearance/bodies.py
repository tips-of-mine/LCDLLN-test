"""Corps de race dérivés des corps humains (même squelette, mêmes animations).

Chaque corps de race est une copie du corps humain (Ranger) dont seuls les
sommets sont déplacés : épaississement (nains, orcs, démons) ou affinement
(elfes) autour des os, pondéré par les poids de skinning. Le squelette, les
matrices de liaison, les UV et les matériaux sont recopiés à l'identique :
les clips d'animation de la bibliothèque UE5 s'appliquent sans retargeting.

La taille globale de chaque race est réglée par ``importScale`` dans
``game/data/races/races.json`` (étalon nain : 1,30 m).
"""

from __future__ import annotations

from pathlib import Path

import numpy as np

from glb_io import read_glb

# Facteurs radiaux par famille d'os : (torse, bras, jambes, mains/pieds)
RACE_BUILD = {
    "nains": (1.30, 1.32, 1.30, 1.15),
    "orcs": (1.18, 1.24, 1.16, 1.12),
    "demons": (1.06, 1.08, 1.05, 1.03),
    "elfes": (0.93, 0.92, 0.94, 0.95),
}


def _family(name: str) -> int:
    n = name.lower()
    if any(k in n for k in ("hand", "index", "middle", "pinky", "ring", "thumb", "foot", "ball")):
        return 3
    if any(k in n for k in ("upperarm", "lowerarm", "clavicle")):
        return 1
    if any(k in n for k in ("thigh", "calf")):
        return 2
    return 0


def derive_body(src: Path, dst: Path, race: str) -> dict:
    """Écrit ``dst`` : corps ``src`` épaissi/affiné selon RACE_BUILD[race]."""
    glb = read_glb(src)
    doc = glb.doc
    skin = doc["skins"][0]
    ibm = glb.accessor_array(skin["inverseBindMatrices"]).reshape(-1, 4, 4)
    names = [doc["nodes"][j]["name"] for j in skin["joints"]]
    # Origine de chaque os en espace maillage : inverse de la matrice de liaison
    # inverse (stockée en colonne majeure -> transposer).
    origins = np.array([np.linalg.inv(m.T)[:3, 3] for m in ibm])
    # Segment de chaque os : de son origine à celle de son premier enfant.
    parent = {}
    for j_idx, node_index in enumerate(skin["joints"]):
        for child in doc["nodes"][node_index].get("children", []):
            if child in skin["joints"]:
                parent.setdefault(j_idx, skin["joints"].index(child))
    ends = np.array([origins[parent[j]] if j in parent else origins[j] for j in range(len(names))])
    factors = np.array([RACE_BUILD[race][_family(n)] for n in names])

    touched = set()
    stats = {"meshes": 0, "vertices": 0}
    for node in doc["nodes"]:
        if "mesh" not in node:
            continue
        node_skin = doc["skins"][node.get("skin", 0)]
        remap = [skin["joints"].index(j) if j in skin["joints"] else 0 for j in node_skin["joints"]]
        for prim in doc["meshes"][node["mesh"]]["primitives"]:
            pa = prim["attributes"]["POSITION"]
            if pa in touched:
                continue
            touched.add(pa)
            P = glb.accessor_array(pa).astype(np.float64)
            J = glb.accessor_array(prim["attributes"]["JOINTS_0"]).astype(np.int64)
            W = glb.accessor_array(prim["attributes"]["WEIGHTS_0"]).astype(np.float64)
            J = np.vectorize(lambda j: remap[j] if j < len(remap) else 0)(J)
            W = W / np.maximum(W.sum(axis=1, keepdims=True), 1e-9)
            out = np.zeros_like(P)
            for k in range(4):
                a = origins[J[:, k]]
                b = ends[J[:, k]]
                ab = b - a
                L2 = np.maximum(np.einsum("ij,ij->i", ab, ab), 1e-18)
                t = np.clip(np.einsum("ij,ij->i", P - a, ab) / L2, 0.0, 1.0)
                proj = a + ab * t[:, None]
                f = factors[J[:, k]]
                out += W[:, k:k + 1] * (proj + (P - proj) * f[:, None])
            glb.write_accessor_array(pa, out.astype(np.float32))
            stats["meshes"] += 1
            stats["vertices"] += len(P)
    doc.setdefault("asset", {})["generator"] = (doc["asset"].get("generator", "") +
                                                f" + LCDLLN bodies.py ({race})").strip()
    glb.save(dst)
    return stats
