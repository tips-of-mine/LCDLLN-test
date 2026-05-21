#!/usr/bin/env python3
"""Traitement des assets de personnages : inbox -> game/data/models/characters.

Copie les FBX déposés dans ``tools/asset_pipeline/inbox/characters/<race>/<type>/``
vers ``game/data/models/characters/<race>/...`` en respectant l'arborescence du
système de customisation, puis déplace les sources traitées vers
``inbox/processed/characters/``.

Les ``<race>`` sont les ids existants (cf. game/data/races/races.json) :
humains, elfes, orcs, nains, morts_vivants, corrompus, divins, demons.

Exemples :
    python3 tools/asset_pipeline/process_character_assets.py --race humains --type heads --file male_head_01.fbx
    python3 tools/asset_pipeline/process_character_assets.py --race orcs
    python3 tools/asset_pipeline/process_character_assets.py --all
    python3 tools/asset_pipeline/process_character_assets.py --list
"""

import argparse
import shutil
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
INBOX_BASE = REPO / "tools" / "asset_pipeline" / "inbox" / "characters"
OUTPUT_BASE = REPO / "game" / "data" / "models" / "characters"
PROCESSED_BASE = REPO / "tools" / "asset_pipeline" / "inbox" / "processed" / "characters"


def _gender_from_name(filename: str) -> str:
    """Déduit le genre ('male'/'female') depuis le nom de fichier. 'male' par défaut."""
    return "female" if "female" in filename.lower() else "male"


def output_dir_for(race: str, asset_type: str, filename: str) -> Path:
    """Calcule le dossier de sortie selon le type d'asset."""
    if asset_type == "base_bodies":
        return OUTPUT_BASE / race / "base"
    if asset_type in ("heads", "hair"):
        return OUTPUT_BASE / race / asset_type / _gender_from_name(filename)
    if asset_type == "facial_hair":
        return OUTPUT_BASE / race / "facial_hair"
    if asset_type == "source_textures":
        return REPO / "game" / "data" / "textures" / "characters" / race
    # tusks, horns, tails, ears, scales, wings, halos, mutations, ...
    return OUTPUT_BASE / race / asset_type


def process_character_asset(race: str, asset_type: str, filename: str) -> bool:
    inbox_path = INBOX_BASE / race / asset_type / filename
    if not inbox_path.exists():
        print(f"[ERROR] Fichier introuvable : {inbox_path.relative_to(REPO)}")
        return False

    output_dir = output_dir_for(race, asset_type, filename)
    output_dir.mkdir(parents=True, exist_ok=True)
    output_path = output_dir / filename
    shutil.copy2(inbox_path, output_path)
    print(f"[OK] {inbox_path.relative_to(REPO)} -> {output_path.relative_to(REPO)}")

    processed_dir = PROCESSED_BASE / race / asset_type
    processed_dir.mkdir(parents=True, exist_ok=True)
    shutil.move(str(inbox_path), str(processed_dir / filename))
    return True


def process_all_race_assets(race: str) -> int:
    race_inbox = INBOX_BASE / race
    if not race_inbox.is_dir():
        print(f"[ERROR] Inbox de race introuvable : {race_inbox.relative_to(REPO)}")
        return 0

    count = 0
    for asset_type_dir in sorted(p for p in race_inbox.iterdir() if p.is_dir()):
        for fbx in sorted(asset_type_dir.glob("*.fbx")):
            if process_character_asset(race, asset_type_dir.name, fbx.name):
                count += 1
    return count


def list_races() -> list:
    if not INBOX_BASE.is_dir():
        return []
    return sorted(p.name for p in INBOX_BASE.iterdir() if p.is_dir())


def main() -> None:
    parser = argparse.ArgumentParser(description="Traite les assets de personnages depuis inbox.")
    parser.add_argument("--race", help="Id de race (ex. humains, orcs)")
    parser.add_argument("--type", help="Type d'asset (base_bodies, heads, hair, facial_hair, ...)")
    parser.add_argument("--file", help="Nom de fichier précis à traiter")
    parser.add_argument("--all", action="store_true", help="Traite tous les assets de toutes les races")
    parser.add_argument("--list", action="store_true", help="Liste les races disponibles dans l'inbox")
    args = parser.parse_args()

    if args.list:
        races = list_races()
        print("Races dans l'inbox :", ", ".join(races) if races else "(aucune)")
        return

    if args.all:
        total = 0
        for race in list_races():
            print(f"\n=== Race : {race} ===")
            total += process_all_race_assets(race)
        print(f"\n{total} asset(s) traité(s).")
    elif args.race and args.type and args.file:
        process_character_asset(args.race, args.type, args.file)
    elif args.race:
        n = process_all_race_assets(args.race)
        print(f"\n{n} asset(s) traité(s) pour '{args.race}'.")
    else:
        parser.print_help()


if __name__ == "__main__":
    main()
