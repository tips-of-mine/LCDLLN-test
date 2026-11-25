#!/usr/bin/env python3
"""Compose les icones de competences de classe (128 x 128 px, PNG RGBA).

Pour chaque competence de game/data/gameplay/class_skills/<classId>.json, le script
assemble :
  1. un cadre carre arrondi dont la couleur de bordure depend du palier de niveau
     (1-10 bronze, 11-20 argent, 21-30 or, 31-40 violet, 41-50 orange, 51-60 cyan) ;
  2. le motif central de la competence de base (fichier detoure, fond transparent) ;
  3. des points de rang en bas du cadre (1 a 10 selon le rang dans le palier).

Le motif est partage par toutes les declinaisons d'une competence de base :
la competence de base est la partie du nom precedant le double espace
(« Estoc  Funeste » -> « Estoc »). Le fichier motif attendu est :
    <motifs>/<classId>/<branche>_<slug(base)>.png
ex. tools/skills/icon_motifs/guerrier/single_estoc.png

Les icones sont ecrites sous le nom exact du champ iconFile de chaque competence :
    game/data/icons/skills/<classId>/<iconFile>
Le code client n'est pas concerne : il charge deja ces chemins (ClassSkillCatalog.cpp).

Usage :
    python3 tools/skills/compose_skill_icons.py --class guerrier
    python3 tools/skills/compose_skill_icons.py --all
    python3 tools/skills/compose_skill_icons.py --class guerrier --check   (aucune ecriture)

Codes de sortie : 0 succes, 1 motif manquant ou donnees invalides.
"""
from __future__ import annotations

import argparse
import json
import re
import sys
import unicodedata
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter

RACINE = Path(__file__).resolve().parents[2]
DOSSIER_COMPETENCES = RACINE / "game" / "data" / "gameplay" / "class_skills"
DOSSIER_ICONES = RACINE / "game" / "data" / "icons" / "skills"
DOSSIER_MOTIFS = RACINE / "tools" / "skills" / "icon_motifs"

TAILLE = 128          # cote de l'icone finale, en pixels
SUREECH = 4           # sur-echantillonnage pour l'anticrenelage du cadre
NIVEAUX_PAR_PALIER = 10

# Couleurs de bordure par palier (reprises des icones provisoires existantes).
COULEURS_PALIER = [
    (162, 130, 81),   # 1-10  bronze
    (176, 176, 183),  # 11-20 argent
    (212, 181, 69),   # 21-30 or
    (152, 85, 198),   # 31-40 violet
    (212, 126, 55),   # 41-50 orange
    (66, 190, 205),   # 51-60 cyan
]
FOND_CONTOUR = (16, 18, 14, 255)   # liseré sombre exterieur
FOND_INTERIEUR = (30, 29, 35, 255)  # fond du cadre


def slug(texte: str) -> str:
    """Transforme un nom de competence en identifiant de fichier (minuscules, ASCII, '_')."""
    t = unicodedata.normalize("NFKD", texte).encode("ascii", "ignore").decode("ascii")
    return re.sub(r"[^a-z0-9]+", "_", t.lower()).strip("_")


def nom_de_base(nom: str) -> str:
    """Retourne la competence de base : partie du nom avant le double espace."""
    return nom.split("  ")[0].strip()


def cle_motif(competence: dict) -> str:
    """Cle du motif partage par toutes les declinaisons : <branche>_<slug(base)>."""
    return f"{competence['branch']}_{slug(nom_de_base(competence['name']))}"


def palier_et_rang(niveau: int) -> tuple[int, int]:
    """Palier (0-5) et rang dans le palier (1-10) d'un niveau 1-60."""
    return (niveau - 1) // NIVEAUX_PAR_PALIER, (niveau - 1) % NIVEAUX_PAR_PALIER + 1


def dessiner_cadre(palier: int) -> Image.Image:
    """Cadre vide (contour, bordure de palier, fond) en RGBA 128 px, anticrenele."""
    t = TAILLE * SUREECH
    img = Image.new("RGBA", (t, t), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    s = SUREECH
    d.rounded_rectangle([2 * s, 2 * s, t - 3 * s, t - 3 * s], radius=16 * s, fill=FOND_CONTOUR)
    d.rounded_rectangle([5 * s, 5 * s, t - 6 * s, t - 6 * s], radius=13 * s,
                        fill=COULEURS_PALIER[palier] + (255,))
    d.rounded_rectangle([8 * s, 8 * s, t - 9 * s, t - 9 * s], radius=10 * s, fill=FOND_INTERIEUR)
    return img.resize((TAILLE, TAILLE), Image.LANCZOS)


def dessiner_points(img: Image.Image, palier: int, rang: int) -> None:
    """Ajoute les points de rang (1 a 10) centres en bas du cadre (modifie img)."""
    t = TAILLE * SUREECH
    calque = Image.new("RGBA", (t, t), (0, 0, 0, 0))
    d = ImageDraw.Draw(calque)
    s = SUREECH
    ecart, rayon, y = 6 * s, 1.6 * s, 113 * s
    x0 = t / 2 - (rang - 1) * ecart / 2
    for i in range(rang):
        x = x0 + i * ecart
        d.ellipse([x - rayon - s, y - rayon - s, x + rayon + s, y + rayon + s], fill=FOND_CONTOUR)
        d.ellipse([x - rayon, y - rayon, x + rayon, y + rayon], fill=COULEURS_PALIER[palier] + (255,))
    img.alpha_composite(calque.resize((TAILLE, TAILLE), Image.LANCZOS))


def poser_motif(img: Image.Image, motif: Image.Image) -> None:
    """Pose le motif detoure au centre du cadre, avec une ombre portee douce (modifie img)."""
    zone = 92                        # cote maximal du motif en pixels
    m = motif.copy()
    m.thumbnail((zone, zone), Image.LANCZOS)
    x = (TAILLE - m.width) // 2
    y = 60 - m.height // 2           # legerement au-dessus du centre (place des points)
    ombre = Image.new("RGBA", m.size, (0, 0, 0, 0))
    ombre.putalpha(m.getchannel("A").point(lambda a: a * 0.6))
    ombre = ombre.filter(ImageFilter.GaussianBlur(2))
    img.alpha_composite(ombre, (x + 1, y + 2))
    img.alpha_composite(m, (x, y))


def composer_classe(class_id: str, verifier_seulement: bool) -> int:
    """Compose les icones d'une classe. \return nombre d'erreurs (motifs manquants)."""
    fichier = DOSSIER_COMPETENCES / f"{class_id}.json"
    donnees = json.loads(fichier.read_text(encoding="utf-8"))
    sortie = DOSSIER_ICONES / donnees["classId"]
    motifs: dict[str, Image.Image] = {}
    erreurs = 0
    for comp in donnees["skills"]:
        cle = cle_motif(comp)
        if cle not in motifs:
            chemin = DOSSIER_MOTIFS / donnees["classId"] / f"{cle}.png"
            if not chemin.is_file():
                print(f"[ERREUR] motif manquant : {chemin.relative_to(RACINE)}", file=sys.stderr)
                motifs[cle] = None
                erreurs += 1
            else:
                motifs[cle] = Image.open(chemin).convert("RGBA")
        if motifs[cle] is None or verifier_seulement:
            continue
        palier, rang = palier_et_rang(int(comp["tier"]))
        icone = dessiner_cadre(palier)
        poser_motif(icone, motifs[cle])
        dessiner_points(icone, palier, rang)
        sortie.mkdir(parents=True, exist_ok=True)
        icone.save(sortie / comp["iconFile"], optimize=True)
    print(f"{donnees['classId']}: {len(donnees['skills'])} competences, "
          f"{len(motifs)} motifs, {erreurs} manquant(s)")
    return erreurs


def main() -> int:
    """Point d'entree : analyse les arguments et compose la ou les classes demandees."""
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    g = p.add_mutually_exclusive_group(required=True)
    g.add_argument("--class", dest="class_id", help="classId a composer (ex. guerrier)")
    g.add_argument("--all", action="store_true", help="toutes les classes")
    p.add_argument("--check", action="store_true", help="verifie les motifs sans ecrire d'icone")
    a = p.parse_args()
    classes = [f.stem for f in sorted(DOSSIER_COMPETENCES.glob("*.json"))] if a.all else [a.class_id]
    return 1 if sum(composer_classe(c, a.check) for c in classes) else 0


if __name__ == "__main__":
    sys.exit(main())
