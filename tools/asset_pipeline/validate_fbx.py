#!/usr/bin/env python3
"""Validation légère des fichiers FBX (conventions LCDLLN).

Vérifie les contraintes vérifiables sans SDK FBX : existence, extension, taille,
convention de nommage (snake_case, pas d'espaces). La validation profonde
(hiérarchie de bones, UV, normals) nécessite le SDK FBX et reste un TODO.

Exemples :
    python3 tools/asset_pipeline/validate_fbx.py game/data/models/characters/humains/base/male_body_base.fbx
    python3 tools/asset_pipeline/validate_fbx.py --dir game/data/models/characters/humains
"""

import argparse
import re
from pathlib import Path

MAX_SIZE_MB = 50
NAME_RE = re.compile(r"^[a-z0-9_]+\.fbx$")


def validate_fbx_file(fbx_path: Path) -> list:
    errors = []

    if not fbx_path.exists():
        return [f"Fichier introuvable : {fbx_path}"]

    if fbx_path.suffix.lower() != ".fbx":
        errors.append(f"Extension invalide : {fbx_path.suffix} (attendu .fbx)")

    size_mb = fbx_path.stat().st_size / (1024 * 1024)
    if size_mb > MAX_SIZE_MB:
        errors.append(f"Fichier trop volumineux : {size_mb:.2f} Mo (max {MAX_SIZE_MB} Mo)")

    if not NAME_RE.match(fbx_path.name):
        errors.append("Nom non conforme : attendu snake_case sans espaces "
                      "(ex. male_body_base.fbx) — cf. docs/CONVENTIONS_NAMING.md")

    # TODO (SDK FBX) : binaire FBX, hiérarchie humanoid_base, UV maps, normals,
    # vertices non assignés.

    if errors:
        print(f"[ERROR] {fbx_path.name} :")
        for e in errors:
            print(f"  - {e}")
    else:
        print(f"[OK] {fbx_path.name}")
    return errors


def main() -> None:
    parser = argparse.ArgumentParser(description="Valide des fichiers FBX.")
    parser.add_argument("path", help="Chemin d'un FBX ou d'un dossier")
    parser.add_argument("--dir", action="store_true",
                        help="Valide récursivement tous les FBX du dossier")
    args = parser.parse_args()

    path = Path(args.path)

    if args.dir or path.is_dir():
        fbx_files = sorted(path.rglob("*.fbx"))
        print(f"{len(fbx_files)} fichier(s) FBX trouvé(s)")
        total = sum(len(validate_fbx_file(f)) for f in fbx_files)
        print(f"\nValidation terminée : {total} erreur(s).")
        raise SystemExit(1 if total else 0)

    errors = validate_fbx_file(path)
    raise SystemExit(1 if errors else 0)


if __name__ == "__main__":
    main()
