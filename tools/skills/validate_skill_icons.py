#!/usr/bin/env python3
"""Valide la couverture des icones de competences de classe (Python standard uniquement).

Pour chaque fichier game/data/gameplay/class_skills/<classId>.json :
  1. chaque competence a un champ iconFile non vide ;
  2. l'icone game/data/icons/skills/<classId>/<iconFile> existe ;
  3. le motif source tools/skills/icon_motifs/<classId>/<branche>_<slug(base)>.png existe
     (meme regle de nommage que compose_skill_icons.py) ;
  4. la classe a exactement 18 competences de base (3 branches x 6).

Les fichiers PNG sont stockes dans Git LFS : en CI sans LFS, seuls les pointeurs sont
presents, ce qui suffit a verifier leur existence.

Les icones presentes dans un dossier de classe mais referencees par aucune competence
sont signalees en avertissement (non bloquant).

Usage : python3 tools/skills/validate_skill_icons.py
Codes de sortie : 0 conforme, 1 au moins une erreur.
"""
from __future__ import annotations

import json
import re
import sys
import unicodedata
from pathlib import Path

RACINE = Path(__file__).resolve().parents[2]
DOSSIER_COMPETENCES = RACINE / "game" / "data" / "gameplay" / "class_skills"
DOSSIER_ICONES = RACINE / "game" / "data" / "icons" / "skills"
DOSSIER_MOTIFS = RACINE / "tools" / "skills" / "icon_motifs"
COMPETENCES_DE_BASE = 18


def slug(texte: str) -> str:
    """Identifiant de fichier d'un nom (minuscules, ASCII, '_') ; identique a compose_skill_icons."""
    t = unicodedata.normalize("NFKD", texte).encode("ascii", "ignore").decode("ascii")
    return re.sub(r"[^a-z0-9]+", "_", t.lower()).strip("_")


def cle_motif(competence: dict) -> str:
    """Cle du motif partage : <branche>_<slug(partie du nom avant le double espace)>."""
    return f"{competence['branch']}_{slug(competence['name'].split('  ')[0].strip())}"


def valider_classe(fichier: Path) -> tuple[list[str], list[str]]:
    """Valide une classe. \return (erreurs, avertissements)."""
    erreurs: list[str] = []
    avertissements: list[str] = []
    donnees = json.loads(fichier.read_text(encoding="utf-8"))
    class_id = donnees["classId"]
    dossier = DOSSIER_ICONES / class_id
    references: set[str] = set()
    motifs: set[str] = set()
    for comp in donnees["skills"]:
        icone = comp.get("iconFile", "")
        if not icone:
            erreurs.append(f"{class_id}/{comp.get('id', '?')}: iconFile vide")
            continue
        references.add(icone)
        if not (dossier / icone).is_file():
            erreurs.append(f"{class_id}: icone absente {icone}")
        motifs.add(cle_motif(comp))
    for cle in sorted(motifs):
        if not (DOSSIER_MOTIFS / class_id / f"{cle}.png").is_file():
            erreurs.append(f"{class_id}: motif absent icon_motifs/{class_id}/{cle}.png")
    if len(motifs) != COMPETENCES_DE_BASE:
        erreurs.append(f"{class_id}: {len(motifs)} competences de base (attendu {COMPETENCES_DE_BASE})")
    if dossier.is_dir():
        orphelines = sorted(p.name for p in dossier.glob("*.png") if p.name not in references)
        if orphelines:
            avertissements.append(f"{class_id}: {len(orphelines)} icone(s) non referencee(s), ex. {orphelines[0]}")
    return erreurs, avertissements


def main() -> int:
    """Valide toutes les classes et affiche un bilan."""
    total_err: list[str] = []
    for fichier in sorted(DOSSIER_COMPETENCES.glob("*.json")):
        err, avert = valider_classe(fichier)
        total_err += err
        for a in avert:
            print(f"[AVERTISSEMENT] {a}")
    dossiers_classes = {f.stem for f in DOSSIER_COMPETENCES.glob("*.json")}
    for d in sorted(p for p in DOSSIER_ICONES.iterdir() if p.is_dir() and p.name != "branches"):
        if d.name not in dossiers_classes:
            print(f"[AVERTISSEMENT] dossier d'icones sans classe : {d.relative_to(RACINE)}")
    for e in total_err:
        print(f"[ERREUR] {e}")
    print(f"{len(dossiers_classes)} classes verifiees, {len(total_err)} erreur(s)")
    return 1 if total_err else 0


if __name__ == "__main__":
    sys.exit(main())
