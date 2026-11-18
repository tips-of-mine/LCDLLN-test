#!/usr/bin/env python3
"""PORTAL.1 — garde-fou CI : l'affiche publique du portail ne cite aucun secret.

Usage : check_portal_map_secrets.py <portal_map.json | places/index.json> <secret_places.json>

Échoue (code 1) si un nom de ville ou de lieu de secret_places.json apparaît dans
les étiquettes de portal_map.json. secret_places.json absent = aucun secret dans
ce monde : succès.
"""
import json
import os
import sys


def main() -> int:
    if len(sys.argv) != 3:
        print(__doc__)
        return 2
    portal_path, secret_path = sys.argv[1], sys.argv[2]
    with open(portal_path, encoding="utf-8") as f:
        portal = json.load(f)
    # portal_map.json (cities, regions, pois) ou places/index.json (places, PORTAL.2).
    labels = {e.get("name", "") for key in ("cities", "regions", "pois", "places") for e in portal.get(key, [])}
    if not os.path.exists(secret_path):
        print(f"[OK] {portal_path} : {len(labels)} étiquette(s), aucun secret dans ce monde")
        return 0
    with open(secret_path, encoding="utf-8") as f:
        secret = json.load(f)
    secret_names = {e.get("name", "") for key in ("cities", "pois") for e in secret.get(key, [])}
    leaked = sorted(n for n in labels & secret_names if n)
    if leaked:
        print(f"[FAIL] {portal_path} cite des lieux secrets : {', '.join(leaked)}")
        return 1
    print(f"[OK] {portal_path} : {len(labels)} étiquette(s), aucun des {len(secret_names)} secret(s)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
