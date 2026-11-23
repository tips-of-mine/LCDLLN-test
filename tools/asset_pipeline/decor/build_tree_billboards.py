# -*- coding: utf-8 -*-
# Billboards (<Nom>_BB.gltf) des arbres « dark fantasy » (generate_trees_blender.py).
#
# Rôle : ces arbres n'avaient pas de billboard ; au-delà de world.scatter.cull_large_m
# ils disparaissaient et, en deçà, le LOD0 complet restait affiché. Ce script importe
# le LOD0 de chaque arbre, en fait un rendu orthographique de face (fond transparent,
# même procédé que build_decor.render_billboard) et exporte deux plans croisés
# texturés : le client les affiche au-delà de world.scatter.billboard_m, jusqu'à
# world.scatter.cull_billboard_m (horizon boisé).
#
# Usage (Blender 5.x en mode fond) :
#   blender -b -P tools/asset_pipeline/decor/build_tree_billboards.py -- [--only DarkOak_1,...]
# ou avec le module bpy : python tools/asset_pipeline/decor/build_tree_billboards.py
#
# Effet de bord : écrit <Nom>_BB.gltf/.bin et T_Decor_BB_<Nom>.png dans
# game/data/meshes/props/. Reproductible (rendu Cycles CPU, échantillonnage fixe).

import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import bpy  # noqa: E402
import build_decor as bd  # noqa: E402

#: Arbres traités (mêmes noms que generate_trees_blender.TREES / check_trees.TREES).
TREES = ["DarkOak_1", "DarkOak_2", "DarkPine_1", "DarkPine_2",
         "GnarledDeadTree_1", "GnarledDeadTree_2", "MarshWillow_1", "MarshWillow_2"]


def import_tree(name):
    """Importe <Nom>.gltf (LOD0) et le réunit en un seul objet maillage.

    \\param name nom de l'arbre (sans extension)
    \\return l'objet Blender réuni (coordonnées Blender : Z vers le haut)
    """
    bd.reset()
    bpy.ops.import_scene.gltf(filepath=os.path.join(bd.PROPS, name + ".gltf"))
    meshes = [o for o in bpy.context.scene.objects if o.type == "MESH"]
    for o in bpy.context.scene.objects:
        o.select_set(o in meshes)
    bpy.context.view_layer.objects.active = meshes[0]
    if len(meshes) > 1:
        bpy.ops.object.join()
    ob = bpy.context.view_layer.objects.active
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    # Feuillage : l'import glTF relie déjà l'alpha de la texture (BLEND) ; le rendu
    # Cycles en tient compte, le fond reste transparent.
    return ob


def main():
    """Construit le billboard de chaque arbre (ou de la liste --only)."""
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else sys.argv[1:]
    only = set(argv[argv.index("--only") + 1].split(",")) if "--only" in argv else set()
    for name in TREES:
        if only and name not in only:
            continue
        ob = import_tree(name)
        tex, c, qw, qh = bd.render_billboard(ob, name)
        bbo, mname = bd.billboard_object(name, tex, c, qw, qh)
        bd.export(bbo, os.path.join(bd.PROPS, name + "_BB.gltf"), {mname})
        print("[build_tree_billboards]", name, "->", name + "_BB.gltf", "(%.1f x %.1f m)" % (qw, qh))


if __name__ == "__main__":
    main()
