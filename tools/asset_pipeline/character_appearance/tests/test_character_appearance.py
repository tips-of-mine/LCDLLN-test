"""Tests de la chaîne de génération des pièces de personnalisation.

Lancement : python -m unittest discover -s tools/asset_pipeline/character_appearance/tests
Dépendances : numpy (pas de Blender).
"""

import sys
import tempfile
import unittest
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))

from geom import bezier, orient_outward, tube  # noqa: E402
from glb_io import (Primitive, blender_to_raw, load_skeleton, raw_to_blender, read_glb,  # noqa: E402
                    rigid_weights, write_part_glb)
from heads import build_head, face_variants  # noqa: E402

REPO = HERE.parents[3]
BODY = REPO / "game" / "data" / "models" / "characters" / "humains" / "Male_Ranger" / "Male_Ranger.glb"


class GlbIoTests(unittest.TestCase):
    def test_repere_aller_retour(self):
        p = np.array([[0.1, -0.2, 1.7], [0.0, 0.0, 0.0]])
        np.testing.assert_allclose(raw_to_blender(blender_to_raw(p)), p, atol=1e-9)

    @unittest.skipUnless(BODY.exists(), "corps humain absent")
    def test_piece_partage_le_squelette_du_corps(self):
        skel = load_skeleton(BODY)
        self.assertIn("Head", skel.joint_names)
        m = tube(bezier(np.array([[0, 0, 1.6], [0, 0.05, 1.7], [0, 0.1, 1.8]]), 8), 0.01, 6)
        j, w = rigid_weights(len(m.v), skel.joint_index("Head"))
        with tempfile.TemporaryDirectory() as d:
            path = Path(d) / "piece.glb"
            stats = write_part_glb(path, skel, [Primitive("MI_Horn", m.v, m.normals(), m.uv, m.f.ravel(), j, w)],
                                   "piece")
            self.assertGreater(stats["triangles"], 0)
            doc = read_glb(path).doc
            body_doc = read_glb(BODY).doc
            names = [doc["nodes"][k]["name"] for k in doc["skins"][0]["joints"]]
            body_names = [body_doc["nodes"][k]["name"] for k in body_doc["skins"][0]["joints"]]
            self.assertEqual(names, body_names)
            self.assertEqual(doc["materials"][0]["name"], "MI_Horn")
            weights = doc["accessors"][doc["meshes"][0]["primitives"][0]["attributes"]["WEIGHTS_0"]]
            self.assertTrue(weights.get("normalized"))


class GeometryTests(unittest.TestCase):
    def test_tube_oriente_vers_l_exterieur(self):
        path = np.stack([np.zeros(10), np.zeros(10), np.linspace(0, 1, 10)], axis=1)
        m = tube(path, 0.1, 8, cap_end=True)
        a, b, c = m.v[m.f[:, 0]], m.v[m.f[:, 1]], m.v[m.f[:, 2]]
        n = np.cross(b - a, c - a)
        ctr = (a + b + c) / 3 - np.array([0, 0, 0.5])
        self.assertTrue(np.all(np.einsum("ij,ij->i", n, ctr) > 0))

    def test_tete_fermee_et_orientee(self):
        shape = face_variants("humains", "male")[0]
        eye = np.array([0.0, -0.022, 1.705])
        m = build_head(shape, eye, 32, 24)
        flipped = orient_outward(m.flipped(), eye)
        np.testing.assert_array_equal(flipped.f, m.f)   # déjà orientée vers l'extérieur
        self.assertLess(m.v[:, 2].max(), eye[2] + 0.14)  # sommet du crâne plausible
        self.assertTrue(np.all((m.uv >= 0.0) & (m.uv <= 1.0)))

    def test_trois_visages_distincts(self):
        shapes = face_variants("orcs", "female")
        self.assertEqual(len(shapes), 3)
        self.assertNotEqual(shapes[0].jaw, shapes[1].jaw)


class TintTests(unittest.TestCase):
    def test_teinte_bornee(self):
        import textures
        self.assertEqual(textures.tint_for("#FFFFFF", (0.5, 0.5, 0.5)), "#FFFFFF")
        self.assertEqual(textures.tint_for("#404040", (0.5, 0.5, 0.5)), "#80" + "80" + "80")


if __name__ == "__main__":
    unittest.main()
