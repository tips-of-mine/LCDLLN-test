"""Tests des aides de trellis_import.py (coiffures attachées).

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
import trellis_import as ti  # noqa: E402
from geom import Mesh  # noqa: E402

CENTER = np.array([0.0, -0.032, 1.71])


class TiedHairHelpersTest(unittest.TestCase):
    def test_largest_region_drops_islands_and_joins_seam(self):
        m = np.zeros((40, 60), bool)
        m[10:20, :8] = True          # zone principale coupée par la couture
        m[10:20, -8:] = True
        m[30:32, 30:32] = True       # îlot
        out = ti.largest_region(m)
        self.assertTrue(out[15, 0] and out[15, -1])
        self.assertFalse(out[31, 31])

    def test_close_holes_fills_gap(self):
        m = np.zeros((40, 60), bool)
        m[5:35, 10:50] = True
        m[18:21, 28:31] = False      # trou
        self.assertTrue(ti.close_holes(m, 3)[19, 29])

    def test_seat_on_cap_touches_surface(self):
        cap = sp.Field(CENTER, np.full((sp.NP, sp.NT), 0.10))
        v = CENTER + np.array([[0.0, -0.15, 0.0], [0.0, -0.25, -0.05]])
        m = Mesh(v.copy(), np.array([[0, 1, 1]]), np.zeros((2, 2)))
        ti.seat_on_cap(m, cap, CENTER, sink=0.004)
        self.assertAlmostEqual(float(np.linalg.norm(m.v[0] - CENTER)), 0.096, places=3)

    def test_tied_extras_present_for_each_tied_style(self):
        from heads import face_variants
        shape = face_variants("humains", "female")[0]
        ew = ti.EYE_WORLD["female"]
        for option, extras in ti.TIED_EXTRAS.items():
            parts = ti.tied_extras(option, shape, ew, 2)
            self.assertEqual(len(parts), len(extras), option)
            self.assertTrue(all(len(p.f) > 0 for p in parts))


if __name__ == "__main__":
    unittest.main()
