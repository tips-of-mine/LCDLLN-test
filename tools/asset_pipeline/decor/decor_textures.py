#!/usr/bin/env python3
# Atelier décors — génération procédurale des textures des décors (aucune source
# extérieure : toutes les images sont calculées ici, déterministes, CC0 de fait).
#
# Produit dans game/data/meshes/props/ des PNG nommés T_Decor_<Nom>_<Canal>.png :
#   BaseColor (sRGB, alpha = découpe pour les feuillages), Normal (tangent space),
#   ORM (R = AO, G = Roughness, B = Metallic).
#
# Lancement : python tools/asset_pipeline/decor/decor_textures.py [--only NOM]
# Effet de bord : écrit/écrase les PNG T_Decor_* listés dans TEXTURES.
import argparse
import math
import os

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

REPO = os.path.normpath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
PROPS = os.path.join(REPO, "game", "data", "meshes", "props")


# --------------------------------------------------------------------------- bruit
def fbm(h, w, seed, octaves=5, base=4, aspect=(1.0, 1.0), tile=True):
    """Bruit fractal [0,1] tuilable, h x w. aspect étire les cellules (y, x)."""
    rng = np.random.default_rng(seed)
    acc = np.zeros((h, w), np.float32)
    amp, tot = 1.0, 0.0
    for o in range(octaves):
        gy = max(2, int(base * (2 ** o) / aspect[0]))
        gx = max(2, int(base * (2 ** o) / aspect[1]))
        g = rng.random((gy, gx)).astype(np.float32)
        if tile:
            g = np.pad(g, ((0, 1), (0, 1)), mode="wrap")
        img = Image.fromarray(g).resize((w + (w // gx if tile else 0), h + (h // gy if tile else 0)),
                                        Image.BICUBIC)
        a = np.asarray(img, np.float32)[:h, :w]
        acc += amp * a
        tot += amp
        amp *= 0.5
    acc /= tot
    acc -= acc.min()
    return acc / max(1e-6, acc.max())


def normal_from_height(hgt, strength=4.0):
    dy, dx = np.gradient(hgt.astype(np.float32))
    nx, ny, nz = -dx * strength, -dy * strength, np.ones_like(hgt)
    n = np.sqrt(nx * nx + ny * ny + nz * nz)
    rgb = np.stack([nx / n, ny / n, nz / n], -1) * 0.5 + 0.5
    return (rgb * 255).clip(0, 255).astype(np.uint8)


def orm(hgt, rough=0.85, metal=0.0, ao_strength=0.6):
    ao = 1.0 - ao_strength * (1.0 - hgt)
    r = np.full_like(hgt, rough) + (hgt - 0.5) * 0.08
    m = np.full_like(hgt, metal)
    return (np.stack([ao, r, m], -1) * 255).clip(0, 255).astype(np.uint8)


def colorize(t, stops):
    """t [0,1] -> RGB par interpolation de paliers [(pos, (r,g,b)), ...]."""
    pos = np.array([s[0] for s in stops], np.float32)
    cols = np.array([s[1] for s in stops], np.float32)
    out = np.empty(t.shape + (3,), np.float32)
    for c in range(3):
        out[..., c] = np.interp(t, pos, cols[:, c])
    return out


def save(name, arr, mode=None):
    os.makedirs(PROPS, exist_ok=True)
    Image.fromarray(arr, mode).save(os.path.join(PROPS, name), optimize=True)


# --------------------------------------------------------------------- écorces
def bark(name, seed, stops, fissure=0.55, plates=False, size=(1024, 512)):
    h, w = size
    long = fbm(h, w, seed, 6, 6, aspect=(6.0, 0.35))         # sillons verticaux
    fine = fbm(h, w, seed + 1, 5, 16, aspect=(2.0, 1.0))
    if plates:  # plaques d'écorce de pin (cellules horizontales)
        pl = fbm(h, w, seed + 2, 3, 6, aspect=(0.7, 1.4))
        hgt = 0.5 * np.abs(np.sin(pl * 9.0)) + 0.35 * long + 0.15 * fine
    else:
        ridge = (1.0 - np.abs(long * 2.0 - 1.0)) ** 2.2       # crêtes nettes, fissures sombres
        hgt = fissure * ridge + (1 - fissure) * fine
    hgt = (hgt - hgt.min()) / (hgt.max() - hgt.min())
    col = colorize(hgt * 0.75 + fine * 0.25, stops)
    moss = fbm(h, w, seed + 3, 4, 3)
    mmask = np.clip((moss - 0.62) * 4.0, 0, 1)[..., None] * (1 - hgt[..., None]) * 0.7
    col = col * (1 - mmask) + np.array([88, 104, 58], np.float32) * mmask
    save(f"T_Decor_{name}_BaseColor.png", col.clip(0, 255).astype(np.uint8))
    save(f"T_Decor_{name}_Normal.png", normal_from_height(hgt, 6.0))
    save(f"T_Decor_{name}_ORM.png", orm(hgt, 0.88))


# ------------------------------------------------------------ cartes feuillage
def _leaf_poly(cx, cy, length, width, ang):
    pts = []
    for i in range(14):
        t = i / 13.0
        r = math.sin(math.pi * t) ** 0.8 * width
        x, y = t * length, r
        pts.append((x, y))
    for i in range(13, -1, -1):
        t = i / 13.0
        r = math.sin(math.pi * t) ** 0.8 * width
        pts.append((t * length, -r))
    ca, sa = math.cos(ang), math.sin(ang)
    return [(cx + x * ca - y * sa, cy + x * sa + y * ca) for x, y in pts]


def foliage_card(name, seed, palette, kind="broad", size=1024, count=900):
    """Carte RGBA : grappe de feuilles (broad), d'aiguilles (needle), d'écailles
    (scale), de lanières tombantes (willow). Fond transparent."""
    rng = np.random.default_rng(seed)
    S = size * 2  # super-échantillonnage
    img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    cx0, cy0 = S / 2, S / 2

    def col(shade):
        base = palette[rng.integers(len(palette))]
        v = rng.normal(0, 10, 3)
        return tuple(int(np.clip(base[k] * shade + v[k], 0, 255)) for k in range(3)) + (255,)

    if kind == "broad":
        # Rameaux depuis le bas, feuilles le long ; densité plus forte au centre.
        for _ in range(count):
            r = (rng.random() ** 0.6) * S * 0.46
            a = rng.random() * 2 * math.pi
            x, y = cx0 + r * math.cos(a), cy0 + r * math.sin(a) * 0.92
            L = rng.uniform(0.045, 0.075) * S
            shade = 0.62 + 0.45 * (1 - r / (S * 0.46)) * rng.uniform(0.6, 1.0)
            ang = rng.uniform(0, 2 * math.pi)
            d.polygon(_leaf_poly(x, y, L, L * 0.33, ang), fill=col(shade))
            ex, ey = x + math.cos(ang) * L * 0.85, y + math.sin(ang) * L * 0.85
            d.line([(x, y), (ex, ey)], fill=(40, 52, 26, 255), width=max(1, S // 900))
    elif kind == "needle":
        for _ in range(count // 3):
            # rameau : axe + aiguilles en arête de poisson
            r = (rng.random() ** 0.7) * S * 0.42
            a = rng.random() * 2 * math.pi
            x, y = cx0 + r * math.cos(a), cy0 + r * math.sin(a)
            ang = a + rng.normal(0, 0.5)
            L = rng.uniform(0.12, 0.2) * S
            shade = 0.6 + 0.4 * (1 - r / (S * 0.42))
            d.line([(x, y), (x + math.cos(ang) * L, y + math.sin(ang) * L)], fill=(70, 48, 30, 255), width=S // 500)
            for k in range(26):
                t = k / 26.0
                px, py = x + math.cos(ang) * L * t, y + math.sin(ang) * L * t
                for sgn in (-1, 1):
                    na = ang + sgn * 1.0 + rng.normal(0, 0.12)
                    nl = L * 0.16 * (1 - 0.5 * t)
                    d.line([(px, py), (px + math.cos(na) * nl, py + math.sin(na) * nl)],
                           fill=col(shade), width=max(2, S // 420))
    elif kind == "scale":  # cyprès : écailles serrées en touffes allongées
        for _ in range(count * 2):
            y = rng.random() * S
            half = S * 0.40 * math.sin(math.pi * y / S) ** 0.7
            x = cx0 + rng.normal(0, 0.5) * half
            if abs(x - cx0) > half:
                continue
            shade = 0.6 + 0.45 * (1 - abs(x - cx0) / max(1, half))
            rr = S * rng.uniform(0.006, 0.012)
            d.ellipse([x - rr, y - rr * 1.6, x + rr, y + rr * 1.6], fill=col(shade))
    elif kind == "willow":  # lanières pendantes
        for _ in range(count // 4):
            x = cx0 + rng.normal(0, 0.28) * S
            y = rng.uniform(0.02, 0.35) * S
            L = rng.uniform(0.45, 0.62) * S
            sway = rng.normal(0, 0.03) * S
            for k in range(30):
                t = k / 30.0
                px, py = x + sway * t * t, y + L * t
                shade = 0.65 + 0.35 * (1 - t)
                d.polygon(_leaf_poly(px, py, S * 0.03, S * 0.006, math.pi / 2 + rng.normal(0, 0.4)),
                          fill=col(shade))
    img = img.resize((size, size), Image.LANCZOS)
    a = np.asarray(img).copy()
    a[..., 3] = np.where(a[..., 3] > 110, 255, 0).astype(np.uint8)  # découpe nette
    save(f"T_Decor_{name}_BaseColor.png", a, "RGBA")
    hgt = a[..., 3].astype(np.float32) / 255.0
    hgt = np.asarray(Image.fromarray((hgt * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(3)),
                     np.float32) / 255.0
    save(f"T_Decor_{name}_Normal.png", normal_from_height(hgt, 2.5))
    save(f"T_Decor_{name}_ORM.png", orm(hgt, 0.7, 0.0, 0.45))


# ------------------------------------------------------- plantes (cartes RGBA)
def plant_card(name, seed, kind, size=512):
    rng = np.random.default_rng(seed)
    S = size * 2
    img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)

    def g(c, j=12):
        return tuple(int(np.clip(c[k] + rng.normal(0, j), 0, 255)) for k in range(3)) + (255,)

    if kind == "grass":       # brins depuis le bas
        for _ in range(220):
            x0 = rng.uniform(0.05, 0.95) * S
            hgt = rng.uniform(0.45, 0.98) * S
            bend = rng.normal(0, 0.12) * S
            w0 = S * rng.uniform(0.008, 0.016)
            pts_l, pts_r = [], []
            for k in range(12):
                t = k / 11
                x = x0 + bend * t * t
                y = S - hgt * t
                w = w0 * (1 - t)
                pts_l.append((x - w, y))
                pts_r.append((x + w, y))
            c = (rng.choice([96, 110, 124]), rng.choice([118, 132, 142]), 58)
            d.polygon(pts_l + pts_r[::-1], fill=g(c))
    elif kind == "dry_grass":
        for _ in range(200):
            x0 = rng.uniform(0.05, 0.95) * S
            hgt = rng.uniform(0.3, 0.9) * S
            bend = rng.normal(0, 0.18) * S
            c = (rng.choice([168, 182, 150]), rng.choice([150, 162, 136]), 92)
            d.line([(x0, S), (x0 + bend * 0.5, S - hgt * 0.6), (x0 + bend, S - hgt)], fill=g(c), width=S // 220)
    elif kind == "reed":      # massettes : tiges, feuilles rubanées, épis bruns
        for _ in range(60):
            x0 = rng.uniform(0.08, 0.92) * S
            hgt = rng.uniform(0.6, 0.98) * S
            lean = rng.normal(0, 0.05) * S
            d.line([(x0, S), (x0 + lean, S - hgt)], fill=g((104, 122, 64)), width=S // 260)
            if rng.random() < 0.45:
                ex, ey = x0 + lean * 0.88, S - hgt * 0.88
                d.rounded_rectangle([ex - S * 0.009, ey - S * 0.06, ex + S * 0.009, ey + S * 0.01],
                                    radius=S // 150, fill=g((92, 62, 38), 8))
        for _ in range(70):
            x0 = rng.uniform(0.05, 0.95) * S
            hgt = rng.uniform(0.4, 0.85) * S
            bend = rng.normal(0, 0.15) * S
            w0 = S * 0.012
            pl, pr = [], []
            for k in range(10):
                t = k / 9
                x, y, w = x0 + bend * t * t, S - hgt * t, w0 * (1 - t * 0.9)
                pl.append((x - w, y)); pr.append((x + w, y))
            d.polygon(pl + pr[::-1], fill=g((112, 134, 70)))
    elif kind == "fern":      # fronde
        cx = S / 2
        for f in range(7):
            ang = -math.pi / 2 + (f - 3) * 0.32 + rng.normal(0, 0.05)
            L = S * rng.uniform(0.62, 0.9)
            for k in range(34):
                t = k / 34
                px = cx + math.cos(ang) * L * t + (t * t) * S * 0.12 * math.copysign(1, f - 3 + 0.01)
                py = S - 4 + math.sin(ang) * L * t + t * t * S * 0.25
                pinna = S * 0.09 * math.sin(math.pi * min(1, t * 1.1)) + 2
                for sgn in (-1, 1):
                    pa = ang + sgn * 1.15
                    d.polygon(_leaf_poly(px, py, pinna, pinna * 0.22, pa), fill=g((78, 112, 52)))
    elif kind == "mullein":   # molène : rosette laineuse + hampe à fleurs jaunes
        cx = S / 2
        for _ in range(9):
            a = rng.uniform(math.pi * 1.05, math.pi * 1.95)
            d.polygon(_leaf_poly(cx, S * 0.98, S * 0.3, S * 0.07, a), fill=g((104, 128, 82), 8))
        top = S * rng.uniform(0.02, 0.08)
        d.line([(cx, S * 0.95), (cx, top)], fill=g((120, 130, 96)), width=S // 45)
        for _ in range(160):
            t = rng.uniform(0, 0.6)
            y = top + (S * 0.95 - top) * t
            x = cx + rng.normal(0, S * 0.04)
            r = S * 0.02
            d.ellipse([x - r, y - r, x + r, y + r], fill=g((214, 184, 52), 14))
    elif kind == "wheat":
        for _ in range(110):
            x0 = rng.uniform(0.04, 0.96) * S
            hgt = rng.uniform(0.75, 0.98) * S
            lean = rng.normal(0, 0.04) * S
            c = (rng.choice([196, 208, 184]), rng.choice([164, 174, 150]), 86)
            d.line([(x0, S), (x0 + lean, S - hgt)], fill=g(c, 8), width=S // 300)
            ex, ey = x0 + lean, S - hgt
            for k in range(9):
                for sgn in (-1, 1):
                    gx, gy = ex + sgn * S * 0.006, ey + k * S * 0.009
                    d.ellipse([gx - S * 0.005, gy - S * 0.008, gx + S * 0.005, gy + S * 0.008], fill=g(c, 8))
    elif kind == "lily":      # feuille ronde échancrée + fleur rouge (vue de dessus)
        cx = cy = S / 2
        R = S * 0.46
        d.pieslice([cx - R, cy - R, cx + R, cy + R], 18, 350, fill=g((72, 108, 58)))
        for k in range(24):
            a = math.radians(18 + k * 14)
            d.line([(cx, cy), (cx + math.cos(a) * R * 0.92, cy + math.sin(a) * R * 0.92)],
                   fill=(58, 90, 46, 255), width=S // 300)
    elif kind == "lily_flower":
        cx = cy = S / 2
        for ring, (n, L, c) in enumerate([(10, 0.44, (186, 64, 66)), (8, 0.32, (204, 86, 82)), (6, 0.2, (220, 120, 104))]):
            for k in range(n):
                a = k * 2 * math.pi / n + ring * 0.3
                d.polygon(_leaf_poly(cx, cy, S * L, S * L * 0.3, a), fill=g(c, 8))
        d.ellipse([cx - S * 0.06, cy - S * 0.06, cx + S * 0.06, cy + S * 0.06], fill=(222, 186, 70, 255))
    img = img.resize((size, size), Image.LANCZOS)
    a = np.asarray(img).copy()
    a[..., 3] = np.where(a[..., 3] > 110, 255, 0).astype(np.uint8)
    # Modulation peinte (taches, nuances) pour casser les aplats.
    shade = 0.82 + 0.3 * fbm(size, size, seed + 99, 5, 6)
    a[..., :3] = (a[..., :3].astype(np.float32) * shade[..., None]).clip(0, 255).astype(np.uint8)
    save(f"T_Decor_{name}_BaseColor.png", a, "RGBA")
    hgt = np.asarray(Image.fromarray(a[..., 3]).filter(ImageFilter.GaussianBlur(2)), np.float32) / 255.0
    save(f"T_Decor_{name}_Normal.png", normal_from_height(hgt, 2.0))
    save(f"T_Decor_{name}_ORM.png", orm(hgt, 0.75, 0.0, 0.3))


# ------------------------------------------------------------------- roches
def rock(name, seed, stops, size=1024, strata=0.0):
    h = w = size
    big = fbm(h, w, seed, 6, 3)
    fine = fbm(h, w, seed + 1, 5, 24)
    hgt = 0.7 * big + 0.3 * fine
    if strata > 0:
        yy = np.linspace(0, 1, h)[:, None]
        hgt = (1 - strata) * hgt + strata * (0.5 + 0.5 * np.sin(yy * 40 + big * 6))
    hgt = (hgt - hgt.min()) / (hgt.max() - hgt.min())
    col = colorize(hgt, stops)
    lich = fbm(h, w, seed + 5, 4, 6)
    lm = np.clip((lich - 0.7) * 5, 0, 1)[..., None] * 0.5
    col = col * (1 - lm) + np.array([128, 132, 96], np.float32) * lm
    save(f"T_Decor_{name}_BaseColor.png", col.clip(0, 255).astype(np.uint8))
    save(f"T_Decor_{name}_Normal.png", normal_from_height(hgt, 5.0))
    save(f"T_Decor_{name}_ORM.png", orm(hgt, 0.9, 0.0, 0.5))


GREEN_BROAD = [(84, 112, 54), (98, 126, 60), (72, 98, 46), (110, 132, 66), (90, 118, 70)]
GREEN_OAK = [(76, 100, 48), (88, 112, 52), (66, 90, 44), (98, 118, 58)]
GREEN_NEEDLE = [(56, 84, 52), (64, 92, 56), (48, 74, 48), (70, 96, 60)]
GREEN_CYPRESS = [(52, 78, 46), (60, 86, 50), (46, 68, 42)]
GREEN_WILLOW = [(118, 140, 72), (128, 148, 80), (106, 128, 66)]
GREEN_BEECH = [(86, 118, 50), (98, 130, 56), (76, 106, 46), (108, 136, 62)]
GREEN_POPLAR = [(112, 138, 62), (124, 148, 70), (100, 128, 58), (134, 154, 78)]
GREEN_BUSH = [(80, 108, 52), (92, 120, 58), (70, 96, 48), (102, 124, 64)]

TEXTURES = {
    "Bark_Broad": lambda: bark("Bark_Broad", 11, [(0, (58, 50, 42)), (0.5, (104, 94, 80)), (1, (146, 136, 118))]),
    "Bark_Oak": lambda: bark("Bark_Oak", 12, [(0, (48, 40, 32)), (0.5, (92, 80, 64)), (1, (130, 118, 98))], 0.75),
    "Bark_Pine": lambda: bark("Bark_Pine", 13, [(0, (62, 36, 24)), (0.5, (118, 70, 44)), (1, (160, 112, 78))], plates=True),
    "Bark_Pale": lambda: bark("Bark_Pale", 14, [(0, (92, 88, 80)), (0.5, (150, 144, 132)), (1, (196, 190, 176))], 0.4),
    "Bark_Dark": lambda: bark("Bark_Dark", 15, [(0, (40, 34, 30)), (0.5, (78, 68, 58)), (1, (112, 100, 86))], 0.8),
    "Leaves_Broad": lambda: foliage_card("Leaves_Broad", 21, GREEN_BROAD, "broad"),
    "Leaves_Oak": lambda: foliage_card("Leaves_Oak", 22, GREEN_OAK, "broad", count=1100),
    "Leaves_Beech": lambda: foliage_card("Leaves_Beech", 28, GREEN_BEECH, "broad", count=1000),
    "Leaves_Poplar": lambda: foliage_card("Leaves_Poplar", 29, GREEN_POPLAR, "broad", count=850),
    "Leaves_Needle": lambda: foliage_card("Leaves_Needle", 23, GREEN_NEEDLE, "needle"),
    "Leaves_Cypress": lambda: foliage_card("Leaves_Cypress", 24, GREEN_CYPRESS, "scale", count=1400),
    "Leaves_Willow": lambda: foliage_card("Leaves_Willow", 25, GREEN_WILLOW, "willow", count=700),
    "Leaves_Bush": lambda: foliage_card("Leaves_Bush", 26, GREEN_BUSH, "broad", count=1000),
    "Leaves_BushFlowers": lambda: foliage_card("Leaves_BushFlowers", 27, GREEN_BUSH + [(214, 206, 190), (226, 214, 120)], "broad", count=1000),
    "Plant_Grass": lambda: plant_card("Plant_Grass", 31, "grass"),
    "Plant_GrassDry": lambda: plant_card("Plant_GrassDry", 32, "dry_grass"),
    "Plant_Reed": lambda: plant_card("Plant_Reed", 33, "reed"),
    "Plant_Fern": lambda: plant_card("Plant_Fern", 34, "fern"),
    "Plant_Mullein": lambda: plant_card("Plant_Mullein", 35, "mullein"),
    "Plant_Wheat": lambda: plant_card("Plant_Wheat", 36, "wheat"),
    "Plant_LilyPad": lambda: plant_card("Plant_LilyPad", 37, "lily"),
    "Plant_LilyFlower": lambda: plant_card("Plant_LilyFlower", 38, "lily_flower"),
    "Rock_Grey": lambda: rock("Rock_Grey", 41, [(0, (82, 80, 76)), (0.5, (130, 126, 118)), (1, (172, 166, 156))]),
    "Rock_Ochre": lambda: rock("Rock_Ochre", 42, [(0, (112, 84, 56)), (0.5, (170, 132, 90)), (1, (206, 172, 128))], strata=0.25),
    "Rock_Red": lambda: rock("Rock_Red", 43, [(0, (104, 56, 42)), (0.5, (156, 88, 62)), (1, (196, 132, 96))], strata=0.35),
    "Rock_Mossy": lambda: rock("Rock_Mossy", 44, [(0, (70, 76, 58)), (0.5, (108, 112, 92)), (1, (150, 150, 128))]),
}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--only")
    a = ap.parse_args()
    for k, fn in TEXTURES.items():
        if a.only and a.only != k:
            continue
        fn()
        print("[decor_textures]", k)


if __name__ == "__main__":
    main()
