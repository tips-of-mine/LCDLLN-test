"""Tests des outils d'icones de competences (compose_skill_icons, validate_skill_icons).

Lancement : python3 -m unittest discover -s tools/skills/tests
Prerequis : Pillow (importe par compose_skill_icons).
"""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import compose_skill_icons as compose  # noqa: E402
import validate_skill_icons as valide  # noqa: E402


class TestNommage(unittest.TestCase):
    """La regle de nommage des motifs doit etre identique dans les deux outils."""

    CAS = ["Tourbillon d'Acier", "Brise-Garde", "Cur de Pierre", "Coup a la Gorge", "Égide"]

    def test_slug_identique(self):
        for nom in self.CAS:
            self.assertEqual(compose.slug(nom), valide.slug(nom))

    def test_slug_valeurs(self):
        self.assertEqual(compose.slug("Tourbillon d'Acier"), "tourbillon_d_acier")
        self.assertEqual(compose.slug("Égide"), "egide")

    def test_cle_motif_partagee_par_les_declinaisons(self):
        base = {"branch": "single", "name": "Estoc"}
        decl = {"branch": "single", "name": "Estoc  Maitrise 50"}
        self.assertEqual(compose.cle_motif(base), "single_estoc")
        self.assertEqual(compose.cle_motif(decl), compose.cle_motif(base))
        self.assertEqual(valide.cle_motif(decl), compose.cle_motif(decl))


class TestPaliers(unittest.TestCase):
    """Niveau 1-60 -> palier 0-5 (couleur du cadre) et rang 1-10 (points)."""

    def test_bornes(self):
        self.assertEqual(compose.palier_et_rang(1), (0, 1))
        self.assertEqual(compose.palier_et_rang(10), (0, 10))
        self.assertEqual(compose.palier_et_rang(11), (1, 1))
        self.assertEqual(compose.palier_et_rang(60), (5, 10))

    def test_six_couleurs(self):
        self.assertEqual(len(compose.COULEURS_PALIER), 6)


class TestComposition(unittest.TestCase):
    """Le cadre compose fait 128 px RGBA avec des coins transparents."""

    def test_cadre(self):
        img = compose.dessiner_cadre(0)
        self.assertEqual(img.size, (128, 128))
        self.assertEqual(img.mode, "RGBA")
        self.assertEqual(img.getpixel((0, 0))[3], 0)
        self.assertEqual(img.getpixel((64, 64))[:3], compose.FOND_INTERIEUR[:3])


class TestDepot(unittest.TestCase):
    """Les donnees du depot sont couvertes (motifs et icones presents)."""

    def test_couverture(self):
        for fichier in sorted(valide.DOSSIER_COMPETENCES.glob("*.json")):
            erreurs, _ = valide.valider_classe(fichier)
            self.assertEqual(erreurs, [], fichier.name)


if __name__ == "__main__":
    unittest.main()
