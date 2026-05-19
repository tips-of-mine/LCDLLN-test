"""
Detecte le framerate de chaque FBX dans tools/asset_pipeline/inbox/.

Methode :
1. Convert le FBX en .glb temporaire via FBX2glTF
2. Parse le frame range depuis stdout : "Animation X: [A - B]" -> frames = B-A+1
3. Parse le .glb : header binaire + JSON chunk, recupere la duration de l'animation
4. Calcule fps = (frame_count - 1) / duration (frames 0-indexed)

Imprime un tableau trie par fps croissant, avec un highlight pour les non-60-fps.

Usage : python tools/asset_pipeline/detect_fbx_fps.py
"""

import json
import re
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

INBOX = Path("tools/asset_pipeline/inbox")
CONVERTER = Path("tools/asset_pipeline/bin/FBX2glTF.exe")

# Regex pour parser "Animation X: [A - B]" depuis le stdout FBX2glTF.
ANIM_RANGE_RE = re.compile(r"Animation\s+(.+?):\s+\[(\d+)\s+-\s+(\d+)\]")


def parse_glb_animation_duration(glb_path: Path) -> float:
    """Parse le .glb binaire, retourne la duration max sur toutes les animations."""
    with open(glb_path, "rb") as f:
        magic = f.read(4)
        if magic != b"glTF":
            return 0.0
        version, total_length = struct.unpack("<II", f.read(8))
        # Premier chunk = JSON
        chunk_length, chunk_type = struct.unpack("<II", f.read(8))
        if chunk_type != 0x4E4F534A:  # "JSON"
            return 0.0
        json_bytes = f.read(chunk_length)
        # Le chunk peut etre padded avec 0x20 (espaces).
        json_str = json_bytes.rstrip(b"\x20\x00").decode("utf-8")
        gltf = json.loads(json_str)

    if "animations" not in gltf or not gltf["animations"]:
        return 0.0

    # Pour chaque animation, trouve la duration max via les input accessors.
    max_duration = 0.0
    accessors = gltf.get("accessors", [])
    for anim in gltf["animations"]:
        for sampler in anim.get("samplers", []):
            input_idx = sampler.get("input")
            if input_idx is None or input_idx >= len(accessors):
                continue
            acc = accessors[input_idx]
            if "max" in acc and acc["max"]:
                t = acc["max"][0]
                if t > max_duration:
                    max_duration = t
    return max_duration


def detect_fps_for_fbx(fbx_path: Path) -> tuple[float | None, int, float, str]:
    """
    Retourne (fps, frame_count, duration, primary_clip_name) ou (None, 0, 0.0, "") si echec.
    """
    with tempfile.TemporaryDirectory() as tmp:
        tmp_out = Path(tmp) / "out"
        cmd = [
            str(CONVERTER),
            "--input", str(fbx_path),
            "--output", str(tmp_out),
            "--binary",
            "--khr-materials-unlit",
            "--skinning-weights", "4",
        ]
        try:
            result = subprocess.run(
                cmd, capture_output=True, text=True, timeout=60
            )
        except (subprocess.TimeoutExpired, FileNotFoundError):
            return None, 0, 0.0, ""

        # Parse "Animation X: [A - B]" — on prend l'anim avec le frame range non-vide
        # le plus large (ignore Take 001 = [UINT32_MAX, UINT32_MAX] = vide).
        clips = []
        for line in result.stdout.splitlines() + result.stderr.splitlines():
            m = ANIM_RANGE_RE.search(line)
            if m:
                name, a, b = m.group(1), int(m.group(2)), int(m.group(3))
                # Skip les "Take 001" vides ou les ranges absurdes.
                if a > 1_000_000_000 or b > 1_000_000_000 or b < a:
                    continue
                clips.append((name, a, b))

        if not clips:
            return None, 0, 0.0, ""

        # Prend le clip avec le plus grand frame range (le clip "principal").
        clips.sort(key=lambda x: x[2] - x[1], reverse=True)
        name, frame_start, frame_end = clips[0]
        frame_count = frame_end - frame_start + 1

        glb_path = tmp_out.with_suffix(".glb")
        if not glb_path.exists():
            return None, 0, 0.0, ""

        duration = parse_glb_animation_duration(glb_path)
        if duration <= 0.0 or frame_count <= 1:
            return None, frame_count, duration, name

        # fps = (frame_count - 1) / duration ; frames sont 0-indexed donc N frames
        # couvrent (N-1) intervalles.
        fps = (frame_count - 1) / duration
        return fps, frame_count, duration, name


def main():
    if not CONVERTER.exists():
        print(f"FBX2glTF.exe absent : {CONVERTER}", file=sys.stderr)
        sys.exit(1)

    fbx_files = sorted(INBOX.glob("*.fbx"))
    print(f"Detection fps sur {len(fbx_files)} fichiers FBX...\n")

    results = []
    for fbx in fbx_files:
        fps, frames, duration, clip_name = detect_fps_for_fbx(fbx)
        results.append((fbx.name, fps, frames, duration, clip_name))
        # Progress indicator.
        status = f"{fps:.2f} fps" if fps else "ECHEC"
        print(f"  [{status:>12}] {fbx.name}", file=sys.stderr)

    # Trie : non-60 d'abord (probleme), puis 60 (OK), puis ECHEC en dernier
    def sort_key(r):
        _, fps, _, _, _ = r
        if fps is None:
            return (3, 0)
        is_60 = 59.0 <= fps <= 61.0
        if is_60:
            return (1, fps)
        # Non-60 : tries par fps croissant
        return (0, fps)

    results.sort(key=sort_key)

    print("\n" + "=" * 90)
    print(f"{'FILE':<45} {'FPS':>8} {'FRAMES':>8} {'DURATION':>10} {'CLIP':<20}")
    print("=" * 90)

    not_60_count = 0
    for name, fps, frames, duration, clip in results:
        if fps is None:
            fps_str = "  ERR"
            flag = "[?]"
        else:
            fps_str = f"{fps:>6.2f}"
            is_60 = 59.0 <= fps <= 61.0
            flag = "    " if is_60 else "[!] "
            if not is_60:
                not_60_count += 1
        clip_short = (clip[:18] + "..") if len(clip) > 20 else clip
        print(f"{flag}{name:<41} {fps_str:>8} {frames:>8} {duration:>9.3f}s  {clip_short}")

    print("=" * 90)
    print(f"\n[!] = pas a 60 fps ({not_60_count} fichiers)")
    print(f"[?] = detection echouee")


if __name__ == "__main__":
    main()
