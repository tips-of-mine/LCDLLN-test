#!/usr/bin/env python3
# Ajoute une colline gaussienne au heightmap demo_plains (format HAMP, 1025x1025 u16).
#
# Mapping terrain (defaults de TerrainRenderer, non surcharges dans config.json) :
#   world_size 1024, origin (-512,-512), 1 m/texel  -> texel = world + 512
#   height_scale 200 m  -> delta_normalise = metres / 200 ; valeur u16 = norm * 65535
#
# Idempotent : sauvegarde .r16h.bak au 1er run et repart TOUJOURS de la sauvegarde
# (donc rejouable sans empiler les collines ; preserve le bol d'eau et le relief).
#
# Lancement (Python systeme ou Python embarque de Blender) :
#   python tools/world_gen/raise_hill.py
import math
import os
import shutil
import struct

REPO = os.path.normpath(os.path.join(os.path.dirname(__file__), "..", ".."))
HM = os.path.join(REPO, "game", "data", "zones", "demo_plains", "terrain_height.r16h")
BAK = HM + ".bak"

HEIGHT_SCALE = 200.0
ORIGIN = -512.0           # m (origine X et Z du terrain)
HILL_WX, HILL_WZ = -60.0, -55.0   # centre monde (nord-ouest, loin du bol d'eau)
HILL_RADIUS_M = 40.0
HILL_HEIGHT_M = 15.0
HAMP_MAGIC = 0x504D4148   # 'HAMP'


def main():
    if not os.path.exists(BAK):
        shutil.copyfile(HM, BAK)        # 1er run : sauvegarde de reference
    with open(BAK, "rb") as f:          # repart toujours du heightmap d'origine
        data = f.read()

    magic, w, h = struct.unpack_from("<III", data, 0)
    if magic != HAMP_MAGIC:
        raise SystemExit("magic HAMP attendu, lu 0x%08X" % magic)
    hdr = 12
    heights = list(struct.unpack_from("<%dH" % (w * h), data, hdr))

    cx = HILL_WX - ORIGIN   # texel centre (1 m/texel)
    cz = HILL_WZ - ORIGIN
    r = HILL_RADIUS_M
    peak_dn = (HILL_HEIGHT_M / HEIGHT_SCALE) * 65535.0
    sigma = r / 2.0
    two_sigma2 = 2.0 * sigma * sigma

    x0, x1 = max(0, int(cx - r)), min(w - 1, int(cx + r))
    z0, z1 = max(0, int(cz - r)), min(h - 1, int(cz + r))
    changed = 0
    for z in range(z0, z1 + 1):
        for x in range(x0, x1 + 1):
            d2 = (x - cx) ** 2 + (z - cz) ** 2
            if d2 > r * r:
                continue
            add = peak_dn * math.exp(-d2 / two_sigma2)
            idx = z * w + x
            val = heights[idx] + add
            heights[idx] = max(0, min(65535, int(round(val))))
            changed += 1

    out = bytearray(data[:hdr])
    out += struct.pack("<%dH" % (w * h), *heights)
    with open(HM, "wb") as f:
        f.write(out)
    print("[raise_hill] colline %.0fm r=%.0fm en (%.0f,%.0f) -> %d texels modifies (grille %dx%d)"
          % (HILL_HEIGHT_M, HILL_RADIUS_M, HILL_WX, HILL_WZ, changed, w, h))


main()
