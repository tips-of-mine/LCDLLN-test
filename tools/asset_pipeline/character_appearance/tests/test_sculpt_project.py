"""Tests de la projection des pièces sculptées (sculpt_project.py).

Lancement : python -m unittest discover -s tools/asset_pipeline/character_appearance/tests
Dépendances : numpy, scipy, Pillow (pas de Blender).
"""

import sys
import unittest
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))

import sculpt_project as sp  # noqa: E402
from heads import face_variants  # noqa: E402

CENTER = np.array([0.0, -0.032, 1.71])
EYE = np.array([0.0, -0.022, 1.705])


def sphere_points(radius: float, n: int = 200000, seed: int = 1) -> np.ndarray:
    rng = np.random.default_rng(seed)
    d = rng.normal(size=(n, 3))
    d /= np.linalg.norm(d, axis=1, keepdims=True)
    return CENTER + d * radius


class FieldTests(unittest.TestCase):
    def test_rayon_d_une_sphere(self):
        f = sp.Field.from_points(sphere_points(0.09), CENTER, "max").filled(3)
        th = np.linspace(-3.0, 3.0, 50)
        ph = np.linspace(-1.2, 1.2, 50)
        np.testing.assert_allclose(f.sample(th, ph), 0.09, atol=1e-4)

    def test_projection_du_visage_sur_la_peau(self):
        """Une tête portée sur une sphère : sommets du visage à la surface, cou
        (raccord au corps) inchangé."""
        skin = sp.Field.from_points(sphere_points(0.095), CENTER, "max").filled(3)
        m = sp.face_template(face_variants("humains", "male")[0], EYE, 33, 23)
        p = sp.project_face(m, skin, neck_z=1.555)
        self.assertEqual(p.f.shape, m.f.shape)
        top = p.v[:, 2] > 1.75
        r = np.linalg.norm(p.v[top] - CENTER, axis=1)
        np.testing.assert_allclose(r, 0.095, atol=2e-3)
        neck = m.v[:, 2] < 1.556
        np.testing.assert_allclose(p.v[neck], m.v[neck], atol=1e-9)

    def test_uv_uniques_du_gabarit(self):
        m = sp.face_template(face_variants("humains", "female")[0], EYE, 34, 24)
        self.assertGreaterEqual(m.uv.min(), 0.0)
        self.assertLessEqual(m.uv.max(), 1.0)
        # Couture à l'arrière : l'avant (u = 0,5) est au milieu de la texture.
        front = m.v[:, 1].argmax()
        self.assertAlmostEqual(m.uv[front, 0], 0.5, delta=0.05)


class HairTests(unittest.TestCase):
    def test_calotte_collee_au_crane_et_refermee_a_l_arriere(self):
        skull = sp.Field.from_points(sphere_points(0.09), CENTER, "max").filled(3)
        cap = sphere_points(0.097, seed=2)
        cap = cap[cap[:, 2] > CENTER[2] + 0.02]          # calotte au-dessus du front
        hair = sp.Field.from_points(cap, CENTER, "max")
        mask = hair.mask()
        lods = sp.hair_lods(hair.filled(3), mask, skull, [3000, 1200, 500])
        self.assertEqual(len(lods), 3)
        for budget, m in zip((3000, 1200, 500), lods):
            self.assertLessEqual(len(m.f), budget)
            r = np.linalg.norm(m.v - CENTER, axis=1)
            self.assertGreaterEqual(r.min(), 0.09)        # jamais sous le crâne
        # Bord de la calotte au ras du crâne (0,5 mm).
        m = lods[0]
        e = np.sort(np.concatenate([m.f[:, [0, 1]], m.f[:, [1, 2]], m.f[:, [2, 0]]]), axis=1)
        uniq, cnt = np.unique(e, axis=0, return_counts=True)
        border = np.unique(uniq[cnt == 1].ravel())
        # La couture (sommets dupliqués à ±180°) n'est pas un bord : on l'écarte.
        th, _ph, rb = sp.spherical(m.v[border], CENTER)
        edge = np.abs(np.abs(th) - np.pi) > 1e-3
        self.assertGreater(int(edge.sum()), 20)
        np.testing.assert_allclose(rb[edge], 0.0905, atol=1.5e-3)


if __name__ == "__main__":
    unittest.main()
