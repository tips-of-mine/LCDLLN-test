"""Lecture / écriture minimale de fichiers glTF binaires (.glb) pour les pièces
de personnalisation des personnages.

Rôle
----
Les pièces (têtes, cheveux, barbes, cornes, queues, ailes…) sont des maillages
skinnés qui partagent EXACTEMENT le squelette du corps de base (même liste
d'os, même ordre, mêmes matrices de liaison inverses). Le moteur les accepte
alors telles quelles dans ``ModularAvatar`` (contrôle de compatibilité par nom
et ordre des os) et les déforme avec les matrices d'os du corps : une pièce
liée à l'os ``Head`` suit la tête à chaque image d'animation.

Ce module ne dépend que de la bibliothèque standard et de numpy ; il est
utilisé par ``build_character_appearance.py`` (exécuté sous Blender) et par
les tests du dossier ``tests/``.

Repères
-------
* « Espace Blender » : mètres, Z vers le haut, +Y vers l'avant du personnage
  (repère obtenu en important le corps dans Blender).
* « Espace brut » : espace des sommets stockés dans le .glb du corps
  (Z vers le haut, unité = 1/100 m, avant = -Y). Conversion :
  ``brut = (-x, -y, z) / 100``. Le moteur applique ensuite ``importScale`` et
  ``importRotXDeg`` de ``races.json`` (même transformation que le corps).
"""

from __future__ import annotations

import json
import struct
from dataclasses import dataclass, field
from pathlib import Path

import numpy as np

GLB_MAGIC = 0x46546C67
CHUNK_JSON = 0x4E4F534A
CHUNK_BIN = 0x004E4942

COMPONENT_FLOAT = 5126
COMPONENT_USHORT = 5123
COMPONENT_UINT = 5125
COMPONENT_UBYTE = 5121


@dataclass
class Glb:
    """Contenu d'un fichier .glb : document JSON + tampon binaire unique."""

    doc: dict
    bin: bytearray

    # ------------------------------------------------------------------
    def accessor_array(self, index: int) -> np.ndarray:
        """Retourne les données d'un accesseur sous forme de tableau numpy (copie)."""
        acc = self.doc["accessors"][index]
        view = self.doc["bufferViews"][acc["bufferView"]]
        ncomp = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4, "MAT4": 16}[acc["type"]]
        dtype = {5126: np.float32, 5123: np.uint16, 5125: np.uint32, 5121: np.uint8}[acc["componentType"]]
        offset = view.get("byteOffset", 0) + acc.get("byteOffset", 0)
        stride = view.get("byteStride", 0)
        itemsize = np.dtype(dtype).itemsize * ncomp
        count = acc["count"]
        if stride and stride != itemsize:
            raw = np.frombuffer(self.bin, dtype=np.uint8, count=stride * (count - 1) + itemsize, offset=offset)
            rows = [np.frombuffer(raw[i * stride:i * stride + itemsize].tobytes(), dtype=dtype) for i in range(count)]
            return np.array(rows).reshape(count, ncomp)
        arr = np.frombuffer(self.bin, dtype=dtype, count=count * ncomp, offset=offset)
        return arr.reshape(count, ncomp).copy()

    def write_accessor_array(self, index: int, data: np.ndarray) -> None:
        """Réécrit EN PLACE les données d'un accesseur (même taille, même type, sans stride)."""
        acc = self.doc["accessors"][index]
        view = self.doc["bufferViews"][acc["bufferView"]]
        dtype = {5126: np.float32, 5123: np.uint16, 5125: np.uint32, 5121: np.uint8}[acc["componentType"]]
        payload = np.ascontiguousarray(data, dtype=dtype).tobytes()
        offset = view.get("byteOffset", 0) + acc.get("byteOffset", 0)
        if view.get("byteStride", 0) not in (0, data.shape[1] * np.dtype(dtype).itemsize):
            raise ValueError("accesseur entrelacé non pris en charge")
        self.bin[offset:offset + len(payload)] = payload
        if acc["type"] == "VEC3" and acc["componentType"] == COMPONENT_FLOAT:
            acc["min"] = [float(v) for v in data.min(axis=0)]
            acc["max"] = [float(v) for v in data.max(axis=0)]

    def save(self, path: Path) -> None:
        """Écrit le fichier .glb (JSON compact, tampons alignés sur 4 octets)."""
        write_glb(path, self.doc, bytes(self.bin))


def read_glb(path: Path) -> Glb:
    """Lit un fichier .glb (un seul tampon binaire attendu)."""
    data = Path(path).read_bytes()
    magic, _version, _length = struct.unpack_from("<III", data, 0)
    if magic != GLB_MAGIC:
        raise ValueError(f"{path} n'est pas un .glb")
    offset = 12
    doc = None
    binary = bytearray()
    while offset < len(data):
        chunk_len, chunk_type = struct.unpack_from("<II", data, offset)
        chunk = data[offset + 8:offset + 8 + chunk_len]
        if chunk_type == CHUNK_JSON:
            doc = json.loads(chunk.decode("utf-8"))
        elif chunk_type == CHUNK_BIN:
            binary = bytearray(chunk)
        offset += 8 + chunk_len
    if doc is None:
        raise ValueError(f"{path} : bloc JSON absent")
    return Glb(doc=doc, bin=binary)


def write_glb(path: Path, doc: dict, binary: bytes) -> None:
    """Sérialise un document glTF + tampon binaire en .glb."""
    js = json.dumps(doc, separators=(",", ":"), ensure_ascii=False).encode("utf-8")
    js += b" " * ((4 - len(js) % 4) % 4)
    binary = bytes(binary) + b"\x00" * ((4 - len(binary) % 4) % 4)
    total = 12 + 8 + len(js) + (8 + len(binary) if binary else 0)
    out = bytearray(struct.pack("<III", GLB_MAGIC, 2, total))
    out += struct.pack("<II", len(js), CHUNK_JSON) + js
    if binary:
        out += struct.pack("<II", len(binary), CHUNK_BIN) + binary
    Path(path).parent.mkdir(parents=True, exist_ok=True)
    Path(path).write_bytes(bytes(out))


# ----------------------------------------------------------------------
# Conversion de repères
# ----------------------------------------------------------------------
def blender_to_raw(points: np.ndarray) -> np.ndarray:
    """Espace Blender (m, Z haut, +Y avant) -> espace brut du corps (cm/100… cf. en-tête)."""
    p = np.asarray(points, dtype=np.float64)
    return np.stack([-p[:, 0], -p[:, 1], p[:, 2]], axis=1) / 100.0


def blender_dir_to_raw(dirs: np.ndarray) -> np.ndarray:
    """Directions (normales) : même rotation que blender_to_raw, sans mise à l'échelle."""
    d = np.asarray(dirs, dtype=np.float64)
    return np.stack([-d[:, 0], -d[:, 1], d[:, 2]], axis=1)


def raw_to_blender(points: np.ndarray) -> np.ndarray:
    """Inverse de blender_to_raw."""
    p = np.asarray(points, dtype=np.float64) * 100.0
    return np.stack([-p[:, 0], -p[:, 1], p[:, 2]], axis=1)


# ----------------------------------------------------------------------
# Squelette de référence
# ----------------------------------------------------------------------
@dataclass
class SkeletonRef:
    """Squelette d'un corps de base, recopié tel quel dans chaque pièce."""

    joint_names: list
    nodes: list                 # nœuds du corps (copie profonde), index d'origine conservés
    armature_node: int          # nœud parent de la racine des os (« Armature »)
    joints: list                # indices de nœuds des os (ordre de skins[0])
    ibm: np.ndarray             # (n, 16) matrices de liaison inverses
    mesh_node_trs: dict = field(default_factory=dict)  # TRS des nœuds porteurs de maillage
    skeleton_node: int = -1     # valeur « skeleton » du skin d'origine

    def joint_index(self, name: str) -> int:
        """Indice d'un os dans la liste du skin (lève KeyError si absent)."""
        return self.joint_names.index(name)


def load_skeleton(body_path: Path) -> SkeletonRef:
    """Extrait le squelette (skins[0]) d'un corps de base."""
    glb = read_glb(body_path)
    doc = glb.doc
    skin = doc["skins"][0]
    joints = list(skin["joints"])
    names = [doc["nodes"][j]["name"] for j in joints]
    ibm = glb.accessor_array(skin["inverseBindMatrices"])
    root_joint = joints[0]
    armature = next(i for i, n in enumerate(doc["nodes"]) if root_joint in n.get("children", []))
    mesh_trs = {}
    for n in doc["nodes"]:
        if "mesh" in n:
            mesh_trs = {k: n[k] for k in ("translation", "rotation", "scale") if k in n}
            break
    return SkeletonRef(joint_names=names, nodes=json.loads(json.dumps(doc["nodes"])),
                       armature_node=armature, joints=joints, ibm=ibm, mesh_node_trs=mesh_trs,
                       skeleton_node=skin.get("skeleton", joints[0]))


# ----------------------------------------------------------------------
# Écriture d'une pièce skinnée
# ----------------------------------------------------------------------
@dataclass
class Primitive:
    """Un sous-maillage d'une pièce, en espace Blender, avec un matériau nommé."""

    material: str
    positions: np.ndarray       # (n, 3) espace Blender
    normals: np.ndarray         # (n, 3) espace Blender
    uvs: np.ndarray             # (n, 2) convention glTF (v vers le bas)
    indices: np.ndarray         # (m,) uint32, triangles
    joints: np.ndarray          # (n, 4) indices d'os (ordre du squelette)
    weights: np.ndarray         # (n, 4) poids normalisés


def write_part_glb(path: Path, skel: SkeletonRef, primitives: list, mesh_name: str) -> dict:
    """Écrit une pièce skinnée partageant le squelette ``skel``.

    Le document contient : RootNode -> Armature (+ os, copiés du corps) et un
    nœud porteur du maillage avec le skin. Retourne des statistiques
    (sommets, triangles, boîte englobante en espace Blender).
    """
    binary = bytearray()
    buffer_views = []
    accessors = []

    def add(data: np.ndarray, ctype: int, atype: str, target: int | None, minmax: bool = False,
            normalized: bool = False) -> int:
        dtype = {COMPONENT_FLOAT: np.float32, COMPONENT_USHORT: np.uint16, COMPONENT_UINT: np.uint32,
                 COMPONENT_UBYTE: np.uint8}[ctype]
        payload = np.ascontiguousarray(data, dtype=dtype).tobytes()
        while len(binary) % 4:
            binary.append(0)
        view = {"buffer": 0, "byteOffset": len(binary), "byteLength": len(payload)}
        if target is not None:
            view["target"] = target
        buffer_views.append(view)
        binary.extend(payload)
        acc = {"bufferView": len(buffer_views) - 1, "componentType": ctype,
               "count": int(data.shape[0]), "type": atype}
        if normalized:
            acc["normalized"] = True
        if minmax:
            acc["min"] = [float(v) for v in np.min(data, axis=0)]
            acc["max"] = [float(v) for v in np.max(data, axis=0)]
        accessors.append(acc)
        return len(accessors) - 1

    materials = []
    material_index = {}
    gltf_prims = []
    total_v = total_t = 0
    bb_min = np.full(3, np.inf)
    bb_max = np.full(3, -np.inf)
    for prim in primitives:
        if prim.material not in material_index:
            material_index[prim.material] = len(materials)
            materials.append({"name": prim.material,
                              "pbrMetallicRoughness": {"baseColorFactor": [1, 1, 1, 1],
                                                       "metallicFactor": 0.0, "roughnessFactor": 1.0}})
        # Arrondi : rend identiques au bit près des pièces géométriquement identiques
        # (déduplication) sans perte visible (1/100 mm).
        pos = blender_to_raw(np.round(prim.positions, 5))
        nor = blender_dir_to_raw(np.round(prim.normals, 4))
        nor /= np.maximum(np.linalg.norm(nor, axis=1, keepdims=True), 1e-9)
        w = np.asarray(prim.weights, dtype=np.float64)
        w /= np.maximum(w.sum(axis=1, keepdims=True), 1e-9)
        # Poids quantifiés sur 8 bits normalisés (somme exacte = 255).
        wq = np.floor(w * 255.0 + 0.5).astype(np.int64)
        wq[:, 0] += 255 - wq.sum(axis=1)
        attrs = {
            "POSITION": add(pos, COMPONENT_FLOAT, "VEC3", 34962, minmax=True),
            "NORMAL": add(nor, COMPONENT_FLOAT, "VEC3", 34962),
            "TEXCOORD_0": add(np.round(prim.uvs, 5), COMPONENT_FLOAT, "VEC2", 34962),
            "JOINTS_0": add(prim.joints, COMPONENT_UBYTE, "VEC4", 34962),
            "WEIGHTS_0": add(wq, COMPONENT_UBYTE, "VEC4", 34962, normalized=True),
        }
        icomp = COMPONENT_USHORT if len(prim.positions) < 65536 else COMPONENT_UINT
        idx = add(np.asarray(prim.indices).reshape(-1, 1), icomp, "SCALAR", 34963)
        gltf_prims.append({"attributes": attrs, "indices": idx, "material": material_index[prim.material]})
        total_v += len(prim.positions)
        total_t += len(prim.indices) // 3
        bb_min = np.minimum(bb_min, prim.positions.min(axis=0))
        bb_max = np.maximum(bb_max, prim.positions.max(axis=0))

    ibm_acc = add(skel.ibm, COMPONENT_FLOAT, "MAT4", None)

    # Nœuds : on recopie les os et l'Armature avec leurs indices d'origine, en
    # remplaçant les nœuds de maillage du corps par des nœuds vides (aucun
    # maillage du corps n'est embarqué dans la pièce).
    nodes = json.loads(json.dumps(skel.nodes))
    for n in nodes:
        n.pop("mesh", None)
        n.pop("skin", None)
    root = next(i for i, n in enumerate(nodes) if skel.armature_node in n.get("children", []))
    mesh_node = {"name": mesh_name, "mesh": 0, "skin": 0}
    mesh_node.update(json.loads(json.dumps(skel.mesh_node_trs)))
    nodes.append(mesh_node)
    nodes[root]["children"] = [skel.armature_node, len(nodes) - 1]
    # Les anciens nœuds porteurs de maillage du corps deviennent orphelins : on
    # les retire de la scène (ils restent dans la liste pour garder les indices).
    doc = {
        "asset": {"version": "2.0", "generator": "LCDLLN build_character_appearance.py"},
        "scene": 0,
        "scenes": [{"name": "Root Scene", "nodes": [root]}],
        "nodes": nodes,
        "meshes": [{"name": mesh_name, "primitives": gltf_prims}],
        "skins": [{"joints": skel.joints, "inverseBindMatrices": ibm_acc, "skeleton": skel.skeleton_node}],
        "materials": materials,
        "accessors": accessors,
        "bufferViews": buffer_views,
        "buffers": [{"byteLength": (len(binary) + 3) // 4 * 4}],
    }
    write_glb(path, doc, bytes(binary))
    return {"vertices": total_v, "triangles": total_t,
            "bbox_min": [round(float(v), 4) for v in bb_min], "bbox_max": [round(float(v), 4) for v in bb_max]}


def rigid_weights(count: int, joint: int) -> tuple:
    """Poids rigides : tous les sommets liés à 100 % à l'os ``joint``."""
    joints = np.zeros((count, 4), dtype=np.uint16)
    joints[:, 0] = joint
    weights = np.zeros((count, 4), dtype=np.float32)
    weights[:, 0] = 1.0
    return joints, weights


def blend_weights(z: np.ndarray, joint_low: int, joint_high: int, z0: float, z1: float) -> tuple:
    """Poids répartis entre deux os selon la hauteur (z0 -> 100 % bas, z1 -> 100 % haut).

    Sert par exemple au cou : la base suit ``neck_01``, le haut suit ``Head``.
    """
    t = np.clip((np.asarray(z) - z0) / max(z1 - z0, 1e-6), 0.0, 1.0)
    joints = np.zeros((len(t), 4), dtype=np.uint16)
    joints[:, 0] = joint_high
    joints[:, 1] = joint_low
    weights = np.zeros((len(t), 4), dtype=np.float32)
    weights[:, 0] = t
    weights[:, 1] = 1.0 - t
    return joints, weights
