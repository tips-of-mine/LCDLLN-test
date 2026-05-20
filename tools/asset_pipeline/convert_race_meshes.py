"""
Convertit les FBX with-skin uploades par l'utilisateur dans
tools/asset_pipeline/inbox/<race>/ en .glb runtime dans
game/data/models/avatars/<race>/<race>.glb.

Convention :
- Un seul FBX with-skin par sous-dossier <race> (le mesh + skeleton de
  reference). Les clips d'animation restent partages dans inbox/ a plat
  (Standing Idle No Skin.fbx, Standard Walk.fbx, etc.) et sont
  retargetes runtime par Engine via SkinnedMeshLoader::LoadClipsAnimOnly.
- Si plusieurs .fbx sont presents dans inbox/<race>/, on prend le premier
  avec "no skin" exclu de son nom (les "*No Skin.fbx" Mixamo sont
  animation-only, pas de mesh).

Output:
- game/data/models/avatars/<race>/<race>.glb (binary glTF, --khr-materials-unlit,
  --skinning-weights 4).

Usage:
- python tools/asset_pipeline/convert_race_meshes.py
- Convertit toutes les races avec un dossier inbox/<race>/ non vide.
- Skip silencieusement si le .glb est plus recent que le .fbx source.
"""

import subprocess
import sys
from pathlib import Path

INBOX = Path("tools/asset_pipeline/inbox")
OUTPUT_ROOT = Path("game/data/models/avatars")
CONVERTER = Path("tools/asset_pipeline/bin/FBX2glTF.exe")

# Races MVP supportees. Les noms doivent correspondre aux ids dans
# races.json + aux sous-dossiers de inbox/ ET de game/data/models/avatars/.
RACES = ["nains", "orc"]


def find_with_skin_fbx(race_dir: Path) -> Path | None:
    """Cherche le premier .fbx 'with skin' (exclut les '*No Skin.fbx')."""
    for fbx in sorted(race_dir.glob("*.fbx")):
        if "no skin" not in fbx.name.lower():
            return fbx
    return None


def convert_one_race(race: str) -> bool:
    """Convertit le FBX with-skin d'une race en .glb runtime. Retourne True
    si le fichier produit est valide (existe non vide), False sinon."""
    race_inbox = INBOX / race
    if not race_inbox.is_dir():
        print(f"[{race}] inbox absent : {race_inbox} (skip)", file=sys.stderr)
        return False

    fbx = find_with_skin_fbx(race_inbox)
    if fbx is None:
        print(f"[{race}] aucun FBX with-skin dans {race_inbox} (skip)", file=sys.stderr)
        return False

    out_dir = OUTPUT_ROOT / race
    out_dir.mkdir(parents=True, exist_ok=True)
    out_stem = out_dir / race  # FBX2glTF ajoute .glb automatiquement

    # Skip si le .glb est plus recent que le .fbx source.
    glb_path = out_dir / f"{race}.glb"
    if glb_path.exists() and glb_path.stat().st_mtime >= fbx.stat().st_mtime:
        print(f"[{race}] {glb_path} a jour (skip)", file=sys.stderr)
        return True

    cmd = [
        str(CONVERTER),
        "--input", str(fbx),
        "--output", str(out_stem),
        "--binary",
        "--khr-materials-unlit",
        "--skinning-weights", "4",
    ]
    print(f"[{race}] converting {fbx.name} -> {glb_path}", file=sys.stderr)
    result = subprocess.run(cmd, capture_output=True, text=True, timeout=120, check=False)
    if result.returncode != 0 or not glb_path.exists():
        print(f"[{race}] ECHEC conversion : returncode={result.returncode}", file=sys.stderr)
        print(result.stdout, file=sys.stderr)
        print(result.stderr, file=sys.stderr)
        return False
    size_kb = glb_path.stat().st_size // 1024
    print(f"[{race}] OK -> {glb_path} ({size_kb} KB)", file=sys.stderr)
    return True


def main():
    if not CONVERTER.exists():
        print(f"FBX2glTF.exe absent : {CONVERTER}", file=sys.stderr)
        sys.exit(1)
    failed = []
    for race in RACES:
        if not convert_one_race(race):
            failed.append(race)
    if failed:
        print(f"\nRaces echouees : {failed}", file=sys.stderr)
        sys.exit(2)
    print("\nToutes les races MVP converties OK.", file=sys.stderr)


if __name__ == "__main__":
    main()
