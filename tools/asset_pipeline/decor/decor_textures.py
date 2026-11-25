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
    elif kind == "barley":    # orge : épis barbus inclinés, blond pâle
        for _ in range(120):
            x0 = rng.uniform(0.04, 0.96) * S
            hgt = rng.uniform(0.7, 0.95) * S
            lean = rng.normal(0, 0.05) * S
            c = (rng.choice([206, 214, 196]), rng.choice([190, 198, 176]), 120)
            ex, ey = x0 + lean, S - hgt
            d.line([(x0, S), (ex, ey)], fill=g(c, 8), width=S // 320)
            for k in range(10):
                gx, gy = ex + k * S * 0.004, ey + k * S * 0.008
                d.line([(gx, gy), (gx + S * 0.03, gy - S * 0.05)], fill=g(c, 8), width=max(1, S // 700))
                d.ellipse([gx - S * 0.004, gy - S * 0.006, gx + S * 0.004, gy + S * 0.006], fill=g(c, 8))
    elif kind == "corn":      # maïs : tiges, larges feuilles arquées, panicules
        for _ in range(7):
            x0 = rng.uniform(0.08, 0.92) * S
            d.line([(x0, S), (x0, S * 0.06)], fill=g((112, 140, 60)), width=S // 110)
            for k in range(7):
                y = S * (0.88 - k * 0.11)
                sgn = 1 if k % 2 else -1
                pl = []
                pr = []
                for q in range(12):
                    t = q / 11
                    x = x0 + sgn * t * S * 0.22
                    yy = y - math.sin(t * math.pi * 0.8) * S * 0.08 + t * t * S * 0.06
                    w = S * 0.018 * math.sin(math.pi * min(1, t * 1.05 + 0.05))
                    pl.append((x, yy - w)); pr.append((x, yy + w))
                d.polygon(pl + pr[::-1], fill=g((92, 132, 52)))
            d.polygon([(x0, S * 0.02), (x0 - S * 0.03, S * 0.1), (x0 + S * 0.03, S * 0.1)], fill=g((206, 184, 110), 8))
            if rng.random() < 0.8:
                d.ellipse([x0 + S * 0.005, S * 0.45, x0 + S * 0.035, S * 0.58], fill=g((196, 170, 92), 8))
    elif kind == "sunflower":
        for _ in range(6):
            x0 = rng.uniform(0.1, 0.9) * S
            top = S * rng.uniform(0.08, 0.2)
            d.line([(x0, S), (x0, top)], fill=g((100, 128, 56)), width=S // 120)
            for k in range(4):
                y = S * (0.85 - k * 0.15)
                for sgn in (-1, 1):
                    d.polygon(_leaf_poly(x0, y, S * 0.12, S * 0.05, math.pi / 2 + sgn * 1.2 + math.pi), fill=g((86, 120, 50)))
            R = S * 0.07
            for k in range(20):
                a = k * 2 * math.pi / 20
                d.polygon(_leaf_poly(x0, top, R * 1.7, R * 0.35, a), fill=g((232, 184, 40), 10))
            d.ellipse([x0 - R, top - R, x0 + R, top + R], fill=g((96, 66, 34), 8))
    elif kind == "rapeseed":  # colza : tiges ramifiées, grappes jaune vif
        for _ in range(70):
            x0 = rng.uniform(0.04, 0.96) * S
            hgt = rng.uniform(0.6, 0.95) * S
            lean = rng.normal(0, 0.05) * S
            d.line([(x0, S), (x0 + lean, S - hgt)], fill=g((112, 140, 72)), width=S // 300)
            for _ in range(10):
                fx = x0 + lean + rng.normal(0, S * 0.03)
                fy = S - hgt + rng.normal(0, S * 0.04)
                r = S * 0.012
                d.ellipse([fx - r, fy - r, fx + r, fy + r], fill=g((234, 206, 52), 10))
        for _ in range(40):
            x0 = rng.uniform(0.04, 0.96) * S
            d.polygon(_leaf_poly(x0, S, S * 0.25, S * 0.05, -math.pi / 2 + rng.normal(0, 0.4)), fill=g((96, 128, 70)))
    elif kind == "vine":      # vigne palissée : feuillage lobé + grappes
        for _ in range(420):
            x = rng.uniform(0.0, 1.0) * S
            y = rng.uniform(0.05, 0.75) * S
            r = S * rng.uniform(0.018, 0.03)
            c = (rng.choice([84, 96, 108]), rng.choice([118, 130, 138]), 56)
            for k in range(5):
                a = -math.pi / 2 + (k - 2) * 0.6
                d.ellipse([x + math.cos(a) * r * 0.6 - r * 0.6, y + math.sin(a) * r * 0.6 - r * 0.6,
                           x + math.cos(a) * r * 0.6 + r * 0.6, y + math.sin(a) * r * 0.6 + r * 0.6], fill=g(c))
        for _ in range(26):
            x, y = rng.uniform(0.05, 0.95) * S, rng.uniform(0.45, 0.7) * S
            for k in range(14):
                gx, gy = x + rng.normal(0, S * 0.008), y + k * S * 0.004 + rng.normal(0, S * 0.005)
                d.ellipse([gx - S * 0.006, gy - S * 0.006, gx + S * 0.006, gy + S * 0.006], fill=g((88, 52, 82), 8))
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
    elif kind == "ivy":       # lierre : tiges pendantes, feuilles trilobées sombres
        for _ in range(26):
            x = rng.uniform(0.05, 0.95) * S
            y = 0.0
            for k in range(40):
                nx = x + rng.normal(0, 0.012) * S
                ny = y + S / 40
                d.line([(x, y), (nx, ny)], fill=(70, 58, 40, 255), width=max(1, S // 400))
                if rng.random() < 0.7:
                    c = (rng.choice([54, 62, 70]), rng.choice([84, 96, 104]), 44)
                    for lobe in (-0.6, 0.0, 0.6):
                        d.polygon(_leaf_poly(nx, ny, S * 0.045, S * 0.02, rng.uniform(0, 6.28) + lobe), fill=g(c, 10))
                x, y = nx, ny
                if y > S * rng.uniform(0.6, 1.0):
                    break
    elif kind == "daisy":     # marguerites : tiges, feuilles, capitules blancs à cœur jaune
        for _ in range(26):
            x0 = rng.uniform(0.08, 0.92) * S
            hgt = rng.uniform(0.5, 0.92) * S
            x1 = x0 + rng.normal(0, 0.04) * S
            d.line([(x0, S), (x1, S - hgt)], fill=(84, 118, 56, 255), width=max(2, S // 220))
            for k in range(3):
                yy = S - hgt * rng.uniform(0.1, 0.5)
                d.polygon(_leaf_poly(x0 + (x1 - x0) * (S - yy) / hgt, yy, S * 0.05, S * 0.012, rng.choice([-0.6, 3.7])), fill=g((92, 128, 60), 10))
            R = S * rng.uniform(0.028, 0.04)
            for k in range(14):
                a = k * 2 * math.pi / 14
                d.polygon(_leaf_poly(x1, S - hgt, R * 1.6, R * 0.32, a), fill=g((236, 234, 222), 6))
            d.ellipse([x1 - R * 0.45, S - hgt - R * 0.45, x1 + R * 0.45, S - hgt + R * 0.45], fill=(226, 184, 52, 255))
        for _ in range(60):          # herbe du pied
            x0 = rng.uniform(0.05, 0.95) * S
            hh = rng.uniform(0.1, 0.35) * S
            d.line([(x0, S), (x0 + rng.normal(0, 0.02) * S, S - hh)], fill=g((98, 128, 60), 12), width=max(1, S // 300))
    elif kind in ("iris", "garden_lily"):   # massifs de jardin : iris violets / lis blancs
        petal = (112, 82, 168) if kind == "iris" else (238, 232, 214)
        heart = (226, 196, 80) if kind == "iris" else (214, 120, 58)
        for _ in range(60):          # feuillage en lames
            x0 = rng.uniform(0.05, 0.95) * S
            hh = rng.uniform(0.3, 0.7) * S
            bend = rng.normal(0, 0.06) * S
            w0 = S * 0.012
            d.polygon([(x0 - w0, S), (x0 + w0, S), (x0 + bend, S - hh)], fill=g((78, 116, 58), 12))
        for _ in range(14):
            x0 = rng.uniform(0.1, 0.9) * S
            hh = rng.uniform(0.6, 0.95) * S
            x1 = x0 + rng.normal(0, 0.03) * S
            d.line([(x0, S), (x1, S - hh)], fill=(84, 116, 56, 255), width=max(2, S // 260))
            R = S * rng.uniform(0.035, 0.05)
            for k in range(6 if kind == "iris" else 6):
                a = k * 2 * math.pi / 6 + (0.0 if kind == "iris" else 0.5)
                d.polygon(_leaf_poly(x1, S - hh, R * (1.8 if k % 2 else 1.4), R * 0.5, a), fill=g(petal, 10))
            d.ellipse([x1 - R * 0.3, S - hh - R * 0.3, x1 + R * 0.3, S - hh + R * 0.3], fill=heart + (255,))
    elif kind == "duckweed":  # lentilles d'eau : nappe de petites frondes, bord découpé
        cx = cy = S / 2
        for _ in range(5200):
            r = S * 0.48 * math.sqrt(rng.random())
            a = rng.uniform(0, 6.28)
            x, y = cx + r * math.cos(a), cy + r * math.sin(a)
            if rng.random() > 0.35 + 0.65 * (1 - r / (S * 0.48)) ** 0.5:
                continue
            q = S * rng.uniform(0.006, 0.011)
            d.ellipse([x - q, y - q * 0.8, x + q, y + q * 0.8], fill=g((96, 136, 52), 14))
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
    "Plant_Barley": lambda: plant_card("Plant_Barley", 39, "barley"),
    "Plant_Corn": lambda: plant_card("Plant_Corn", 40, "corn"),
    "Plant_Sunflower": lambda: plant_card("Plant_Sunflower", 45, "sunflower"),
    "Plant_Rapeseed": lambda: plant_card("Plant_Rapeseed", 46, "rapeseed"),
    "Plant_Vine": lambda: plant_card("Plant_Vine", 47, "vine"),
    "Plant_LilyPad": lambda: plant_card("Plant_LilyPad", 37, "lily"),
    "Plant_LilyFlower": lambda: plant_card("Plant_LilyFlower", 38, "lily_flower"),
    "Rock_Grey": lambda: rock("Rock_Grey", 41, [(0, (82, 80, 76)), (0.5, (130, 126, 118)), (1, (172, 166, 156))]),
    "Rock_Ochre": lambda: rock("Rock_Ochre", 42, [(0, (112, 84, 56)), (0.5, (170, 132, 90)), (1, (206, 172, 128))], strata=0.25),
    "Rock_Red": lambda: rock("Rock_Red", 43, [(0, (104, 56, 42)), (0.5, (156, 88, 62)), (1, (196, 132, 96))], strata=0.35),
    "Rock_Mossy": lambda: rock("Rock_Mossy", 44, [(0, (70, 76, 58)), (0.5, (108, 112, 92)), (1, (150, 150, 128))]),
}


# --------------------------------------------------------------- matériaux (props)
def planks(name, seed, stops, plank_w=0.18, size=1024, dark_seam=0.45):
    """Planches verticales (texture 1 m x 1 m) : fil du bois, joints, nœuds."""
    h = w = size
    grain = fbm(h, w, seed, 6, 4, aspect=(8.0, 0.25))
    fine = fbm(h, w, seed + 1, 4, 32)
    n = max(1, int(round(1.0 / plank_w)))
    xs = np.arange(w)[None, :] / w * n
    plank_id = np.floor(xs).astype(np.int32)
    seam = np.abs(xs - np.round(xs))
    rng = np.random.default_rng(seed)
    tone = rng.uniform(0.85, 1.12, n + 1)[plank_id]
    hgt = 0.6 * grain + 0.25 * fine + 0.15
    hgt = np.where(seam < 0.03, hgt * dark_seam, hgt)
    col = colorize(np.clip(grain * 0.8 + fine * 0.2, 0, 1), stops) * tone[..., None]
    col = np.where((seam < 0.03)[..., None], col * dark_seam, col)
    save(f"T_Decor_{name}_BaseColor.png", col.clip(0, 255).astype(np.uint8))
    save(f"T_Decor_{name}_Normal.png", normal_from_height(hgt.astype(np.float32), 4.0))
    save(f"T_Decor_{name}_ORM.png", orm(hgt.astype(np.float32), 0.78))


def metal(name, seed, base, rough=0.55, metallic=0.85, size=512):
    h = w = size
    n = fbm(h, w, seed, 5, 8)
    rust = np.clip((fbm(h, w, seed + 1, 4, 5) - 0.74) * 2.5, 0, 1)
    col = np.array(base, np.float32) * (0.82 + 0.3 * n)[..., None]
    col = col * (1 - rust[..., None]) + np.array([110, 62, 38], np.float32) * rust[..., None]
    save(f"T_Decor_{name}_BaseColor.png", col.clip(0, 255).astype(np.uint8))
    save(f"T_Decor_{name}_Normal.png", normal_from_height(n, 1.5))
    o = orm(n, rough, metallic, 0.2)
    o[..., 1] = np.clip(o[..., 1].astype(np.float32) + rust * 90, 0, 255).astype(np.uint8)
    o[..., 2] = np.clip(o[..., 2].astype(np.float32) * (1 - rust), 0, 255).astype(np.uint8)
    save(f"T_Decor_{name}_ORM.png", o)


def flat(name, seed, base, rough=0.85, var=0.18, aspect=(1.0, 1.0), stripes=None, size=512, metallic=0.0):
    """Matériau uni bruité (tissu, enduit, cire, céramique, cuir...), rayures option."""
    h = w = size
    n = fbm(h, w, seed, 5, 6, aspect=aspect)
    col = np.array(base, np.float32) * (1 - var / 2 + var * n)[..., None]
    if stripes:
        c2, nb = stripes
        band = (np.floor(np.arange(w)[None, :] / w * nb) % 2 == 1)
        col = np.where(band[..., None], np.array(c2, np.float32) * (1 - var / 2 + var * n)[..., None], col)
    save(f"T_Decor_{name}_BaseColor.png", col.clip(0, 255).astype(np.uint8))
    save(f"T_Decor_{name}_Normal.png", normal_from_height(n, 1.0))
    save(f"T_Decor_{name}_ORM.png", orm(n, rough, metallic, 0.25))


def thatch(name, seed, size=1024):
    h = w = size
    straw = fbm(h, w, seed, 6, 6, aspect=(6.0, 0.2))   # fibres dans le sens de la pente (v)
    rows = 0.5 + 0.5 * np.sin(np.arange(h)[:, None] / h * 2 * np.pi * 6 + straw * 2)  # bottes en rangs
    hgt = 0.65 * straw + 0.35 * rows
    col = colorize(hgt, [(0, (92, 74, 44)), (0.5, (150, 122, 74)), (1, (196, 170, 112))])
    save(f"T_Decor_{name}_BaseColor.png", col.clip(0, 255).astype(np.uint8))
    save(f"T_Decor_{name}_Normal.png", normal_from_height(hgt, 5.0))
    save(f"T_Decor_{name}_ORM.png", orm(hgt, 0.95))


def shingles(name, seed, stops, rows=8, cols=6, size=1024):
    h = w = size
    y = np.arange(h)[:, None] / h * rows
    x = np.arange(w)[None, :] / w * cols + (np.floor(y) % 2) * 0.5
    fy, fx = y - np.floor(y), x - np.floor(x)
    rng = np.random.default_rng(seed)
    tone = rng.uniform(0.8, 1.15, (rows + 1, cols + 2))[np.floor(y).astype(int) % (rows + 1), np.floor(x).astype(int) % (cols + 2)]
    grain = fbm(h, w, seed, 4, 10, aspect=(4.0, 0.5))
    hgt = fy * 0.8 + 0.2 * grain
    hgt = np.where((fx < 0.04) | (fx > 0.96), hgt * 0.3, hgt)
    col = colorize(np.clip(grain, 0, 1), stops) * tone[..., None] * (0.7 + 0.3 * fy)[..., None]
    save(f"T_Decor_{name}_BaseColor.png", col.clip(0, 255).astype(np.uint8))
    save(f"T_Decor_{name}_Normal.png", normal_from_height(hgt.astype(np.float32), 5.0))
    save(f"T_Decor_{name}_ORM.png", orm(hgt.astype(np.float32), 0.86))


def stonewall(name, seed, stops, size=1024, rows=7):
    """Moellons / pierre sèche : pierres irrégulières en assises, joints sombres."""
    h = w = size
    rng = np.random.default_rng(seed)
    img = np.zeros((h, w), np.float32)
    ids = np.zeros((h, w), np.int32)
    y0, k = 0, 0
    while y0 < h:
        rh = int(h / rows * rng.uniform(0.7, 1.3))
        x0 = -int(rng.uniform(0, 0.5) * w / 4)
        while x0 < w:
            sw = int(w / 4 * rng.uniform(0.6, 1.5))
            yy, xx = np.ogrid[y0:min(h, y0 + rh), max(0, x0):min(w, x0 + sw)]
            cy, cx = y0 + rh / 2, x0 + sw / 2
            d = ((yy - cy) / (rh / 2)) ** 4 + ((xx - cx) / (sw / 2)) ** 4
            img[y0:min(h, y0 + rh), max(0, x0):min(w, x0 + sw)] = np.clip(1.1 - d, 0, 1) ** 0.4
            ids[y0:min(h, y0 + rh), max(0, x0):min(w, x0 + sw)] = k
            k += 1
            x0 += sw
        y0 += rh
    tone = rng.uniform(0.8, 1.15, k + 1)[ids]
    n = fbm(h, w, seed + 1, 5, 12)
    hgt = img * (0.8 + 0.2 * n)
    col = colorize(n, stops) * tone[..., None] * (0.35 + 0.65 * img)[..., None]
    save(f"T_Decor_{name}_BaseColor.png", col.clip(0, 255).astype(np.uint8))
    save(f"T_Decor_{name}_Normal.png", normal_from_height(hgt, 6.0))
    save(f"T_Decor_{name}_ORM.png", orm(hgt, 0.9, 0.0, 0.6))


TEXTURES.update({
    "Wood_Plank": lambda: planks("Wood_Plank", 51, [(0, (96, 70, 46)), (0.5, (138, 104, 70)), (1, (176, 140, 98))]),
    "Wood_Dark": lambda: planks("Wood_Dark", 52, [(0, (54, 40, 28)), (0.5, (82, 62, 44)), (1, (112, 88, 64))], 0.22),
    "Wood_Weathered": lambda: planks("Wood_Weathered", 53, [(0, (88, 82, 72)), (0.5, (124, 116, 100)), (1, (160, 150, 130))], 0.2),
    "Iron": lambda: metal("Iron", 54, (74, 72, 70)),
    "Brass": lambda: metal("Brass", 55, (176, 140, 74), 0.4, 0.95),
    "Thatch": lambda: thatch("Thatch", 56),
    "Shingle": lambda: shingles("Shingle", 57, [(0, (64, 52, 42)), (0.5, (98, 82, 64)), (1, (130, 112, 88))]),
    "Shingle_Slate": lambda: shingles("Shingle_Slate", 58, [(0, (52, 60, 72)), (0.5, (76, 88, 104)), (1, (104, 116, 132))], 9, 7),
    "Stone_Wall": lambda: stonewall("Stone_Wall", 59, [(0, (90, 86, 78)), (0.5, (134, 128, 116)), (1, (170, 164, 150))]),
    "Plaster": lambda: flat("Plaster", 60, (206, 194, 168), 0.9, 0.12),
    "Cloth_White": lambda: flat("Cloth_White", 61, (214, 206, 186), 0.9, 0.14, (1.0, 8.0)),
    "Cloth_Red": lambda: flat("Cloth_Red", 62, (142, 46, 38), 0.9, 0.18, (1.0, 8.0)),
    "Cloth_Ochre": lambda: flat("Cloth_Ochre", 63, (196, 150, 72), 0.9, 0.16, (1.0, 8.0)),
    "Cloth_Striped": lambda: flat("Cloth_Striped", 64, (196, 178, 140), 0.9, 0.14, (1.0, 8.0), stripes=((150, 52, 42), 8)),
    "Canvas": lambda: flat("Canvas", 65, (182, 168, 136), 0.92, 0.2, (1.0, 6.0)),
    "Leather": lambda: flat("Leather", 66, (98, 64, 40), 0.7, 0.25),
    "Fur": lambda: flat("Fur", 67, (122, 98, 72), 0.98, 0.4, (3.0, 1.0)),
    "Wax": lambda: flat("Wax", 68, (226, 214, 180), 0.55, 0.08),
    "Ceramic": lambda: flat("Ceramic", 69, (160, 92, 58), 0.7, 0.18),
    "Glass_Green": lambda: flat("Glass_Green", 70, (88, 120, 96), 0.15, 0.1),
    "Glass_Amber": lambda: flat("Glass_Amber", 71, (150, 96, 40), 0.15, 0.1),
    "Glass_Clear": lambda: flat("Glass_Clear", 72, (176, 190, 196), 0.1, 0.06),
    "Bone": lambda: flat("Bone", 73, (214, 200, 168), 0.7, 0.2),
    "Straw": lambda: thatch("Straw", 74),
    "Paper": lambda: flat("Paper", 75, (214, 196, 156), 0.85, 0.12),
    "Book_Covers": lambda: flat("Book_Covers", 76, (110, 52, 42), 0.75, 0.2, stripes=((60, 76, 96), 6)),
    "Rope": lambda: flat("Rope", 77, (150, 124, 84), 0.95, 0.3, (6.0, 0.5)),
})


# ------------------------------------------------------- enseignes des services
# Ordre = case de l'atlas T_Decor_Sign_Pictos (8 x 4 cases de 256 px, ligne par
# ligne depuis le haut). Les identifiants sont ceux des services du lore
# (villes[].services) ; le nom affiché vient des catalogues de langue du client
# (clé service.<id>), au survol ou au clic du panneau : aucun texte dans la texture.
SIGN_SERVICES = ["auberge", "ecurie", "cimetiere", "forgeron", "potions", "armes", "armures", "temple",
                 "guilde", "infirmerie", "marche", "banque", "prison", "ecole_magie", "bibliotheque", "port",
                 "arene"]


def _rot(pts, ang, cx=0.5, cy=0.5):
    ca, sa = math.cos(ang), math.sin(ang)
    return [(cx + x * ca - y * sa, cy + x * sa + y * ca) for x, y in pts]


def _picto(name, S):
    """Masque L (S x S) du pictogramme `name`, tracé en coordonnées unitaires."""
    img = Image.new("L", (S, S), 0)
    d = ImageDraw.Draw(img)
    F, B = 255, 0

    def P(pts, c=F):
        d.polygon([(x * S, y * S) for x, y in pts], fill=c)

    def E(x0, y0, x1, y1, c=F, w=0):
        if w:
            d.ellipse([x0 * S, y0 * S, x1 * S, y1 * S], outline=c, width=int(w * S))
        else:
            d.ellipse([x0 * S, y0 * S, x1 * S, y1 * S], fill=c)

    def R(x0, y0, x1, y1, c=F):
        d.rectangle([x0 * S, y0 * S, x1 * S, y1 * S], fill=c)

    def L(x0, y0, x1, y1, w, c=F):
        d.line([x0 * S, y0 * S, x1 * S, y1 * S], fill=c, width=int(w * S))
        for x, y in ((x0, y0), (x1, y1)):
            E(x - w / 2, y - w / 2, x + w / 2, y + w / 2, c)

    def A(x0, y0, x1, y1, a0, a1, w, c=F):
        d.arc([x0 * S, y0 * S, x1 * S, y1 * S], a0, a1, fill=c, width=int(w * S))

    def sword(ang, length=0.62):
        k = length / 0.62
        o = 0.10                       # croisement à mi-lame
        loc = [(-0.03, -0.36), (0.0, -0.44), (0.03, -0.36), (0.03, 0.10), (-0.03, 0.10)]
        P(_rot([(x * k, (y + o) * k) for x, y in loc], ang))
        P(_rot([(-0.12 * k, (0.10 + o) * k), (0.12 * k, (0.10 + o) * k), (0.12 * k, (0.145 + o) * k), (-0.12 * k, (0.145 + o) * k)], ang))
        P(_rot([(-0.022 * k, (0.145 + o) * k), (0.022 * k, (0.145 + o) * k), (0.022 * k, (0.27 + o) * k), (-0.022 * k, (0.27 + o) * k)], ang))
        cx, cy = _rot([(0.0, (0.30 + o) * k)], ang)[0]
        E(cx - 0.035, cy - 0.035, cx + 0.035, cy + 0.035)

    def spear(ang):
        P(_rot([(-0.016, -0.30), (0.016, -0.30), (0.016, 0.40), (-0.016, 0.40)], ang))
        P(_rot([(-0.05, -0.30), (0.0, -0.44), (0.05, -0.30), (0.0, -0.26)], ang))

    if name == "auberge":              # chope moussante
        R(0.30, 0.34, 0.62, 0.80)
        E(0.30, 0.76, 0.62, 0.84)
        E(0.54, 0.40, 0.80, 0.70, F, 0.06)
        for x in (0.27, 0.37, 0.47, 0.55):
            E(x, 0.22, x + 0.13, 0.36)
        for y in (0.46, 0.66):
            R(0.30, y, 0.62, y + 0.018, B)
    elif name == "ecurie":             # fer à cheval
        A(0.22, 0.18, 0.78, 0.80, 140, 400, 0.10)
        for x, y in ((0.27, 0.66), (0.73, 0.66)):
            R(x - 0.065, y + 0.03, x + 0.065, y + 0.09)
        for a in (160, 200, 240, 300, 340, 380):
            r = math.radians(a)
            x, y = 0.5 + 0.235 * math.cos(r), 0.49 + 0.26 * math.sin(r)
            E(x - 0.016, y - 0.016, x + 0.016, y + 0.016, B)
    elif name == "cimetiere":          # stèle
        R(0.33, 0.40, 0.67, 0.80)
        E(0.33, 0.22, 0.67, 0.58)
        R(0.20, 0.80, 0.80, 0.86)
        for y in (0.46, 0.53, 0.60):
            R(0.40, y, 0.60, y + 0.022, B)
    elif name == "forgeron":           # enclume et marteau
        P([(0.10, 0.47), (0.28, 0.43), (0.84, 0.43), (0.84, 0.55), (0.68, 0.57), (0.61, 0.66), (0.72, 0.74),
           (0.72, 0.82), (0.28, 0.82), (0.28, 0.74), (0.39, 0.66), (0.34, 0.57), (0.26, 0.55)])
        L(0.56, 0.34, 0.80, 0.12, 0.045)
        P(_rot([(-0.11, -0.05), (0.11, -0.05), (0.11, 0.05), (-0.11, 0.05)], math.radians(-42), 0.53, 0.30))
    elif name == "potions":            # fiole ronde
        E(0.28, 0.40, 0.72, 0.84)
        R(0.44, 0.22, 0.56, 0.46)
        R(0.40, 0.20, 0.60, 0.25)
        R(0.45, 0.12, 0.55, 0.20)
        A(0.33, 0.45, 0.67, 0.79, 200, 250, 0.03, B)
        R(0.30, 0.58, 0.70, 0.60, B)
    elif name == "armes":              # deux épées croisées
        sword(math.radians(40))
        sword(math.radians(-40))
    elif name == "armures":            # heaume
        R(0.30, 0.32, 0.70, 0.80)
        E(0.30, 0.16, 0.70, 0.48)
        R(0.34, 0.44, 0.47, 0.49, B)
        R(0.53, 0.44, 0.66, 0.49, B)
        for x in (0.40, 0.46, 0.54, 0.60):
            for y in (0.60, 0.67):
                E(x - 0.012, y - 0.012, x + 0.012, y + 0.012, B)
    elif name == "temple":             # soleil rayonnant
        E(0.36, 0.36, 0.64, 0.64)
        for k in range(12):
            a = 2 * math.pi * k / 12
            a0, a1 = a - 0.11, a + 0.11
            P([(0.5 + 0.19 * math.cos(a0), 0.5 + 0.19 * math.sin(a0)),
               (0.5 + 0.36 * math.cos(a), 0.5 + 0.36 * math.sin(a)),
               (0.5 + 0.19 * math.cos(a1), 0.5 + 0.19 * math.sin(a1))])
    elif name == "guilde":             # bannière sur hampe
        L(0.32, 0.16, 0.32, 0.86, 0.035)
        E(0.29, 0.10, 0.35, 0.16)
        P([(0.34, 0.20), (0.74, 0.20), (0.74, 0.60), (0.54, 0.51), (0.34, 0.60)])
        P([(0.54, 0.27), (0.61, 0.36), (0.54, 0.45), (0.47, 0.36)], B)
    elif name == "infirmerie":         # mortier et pilon
        L(0.52, 0.48, 0.72, 0.15, 0.06)
        E(0.67, 0.10, 0.77, 0.20)
        d.pieslice([0.24 * S, 0.26 * S, 0.76 * S, 0.80 * S], 0, 180, fill=F)
        R(0.22, 0.49, 0.78, 0.55)
        R(0.40, 0.78, 0.60, 0.85)
    elif name == "marche":             # balance
        L(0.50, 0.24, 0.50, 0.80, 0.035)
        P([(0.36, 0.86), (0.64, 0.86), (0.58, 0.80), (0.42, 0.80)])
        L(0.18, 0.28, 0.82, 0.28, 0.03)
        E(0.47, 0.17, 0.53, 0.23)
        for cx in (0.20, 0.80):
            L(cx, 0.28, cx - 0.09, 0.56, 0.012)
            L(cx, 0.28, cx + 0.09, 0.56, 0.012)
            d.pieslice([(cx - 0.11) * S, 0.50 * S, (cx + 0.11) * S, 0.64 * S], 0, 180, fill=F)
    elif name == "banque":             # piles de pièces
        for x0, n in ((0.22, 5), (0.52, 3)):
            for k in range(n):
                y = 0.78 - k * 0.075
                R(x0, y - 0.035, x0 + 0.26, y + 0.035)
                E(x0, y + 0.0, x0 + 0.26, y + 0.07)
                E(x0, y - 0.07, x0 + 0.26, y)
                R(x0 + 0.02, y + 0.034, x0 + 0.24, y + 0.040, B)
    elif name == "prison":             # fenêtre à barreaux
        R(0.22, 0.20, 0.78, 0.82)
        R(0.28, 0.26, 0.72, 0.76, B)
        for x in (0.34, 0.44, 0.56, 0.66):
            R(x - 0.018, 0.26, x + 0.018, 0.76)
        R(0.28, 0.49, 0.72, 0.53)
    elif name == "ecole_magie":        # boule de cristal sur pied
        E(0.28, 0.16, 0.72, 0.60)
        A(0.34, 0.22, 0.66, 0.54, 200, 260, 0.025, B)
        for cx, cy, r in ((0.58, 0.32, 0.05), (0.46, 0.44, 0.03)):
            P([(cx, cy - r), (cx + r * 0.25, cy - r * 0.25), (cx + r, cy), (cx + r * 0.25, cy + r * 0.25),
               (cx, cy + r), (cx - r * 0.25, cy + r * 0.25), (cx - r, cy), (cx - r * 0.25, cy - r * 0.25)], B)
        P([(0.36, 0.56), (0.64, 0.56), (0.70, 0.78), (0.74, 0.84), (0.26, 0.84), (0.30, 0.78)])
    elif name == "bibliotheque":       # livre ouvert
        P([(0.49, 0.36), (0.30, 0.28), (0.14, 0.30), (0.14, 0.72), (0.30, 0.70), (0.49, 0.78)])
        P([(0.51, 0.36), (0.70, 0.28), (0.86, 0.30), (0.86, 0.72), (0.70, 0.70), (0.51, 0.78)])
        for k in range(4):
            y = 0.40 + k * 0.08
            L(0.20, y - 0.02, 0.43, y + 0.03, 0.012, B)
            L(0.57, y + 0.03, 0.80, y - 0.02, 0.012, B)
    elif name == "port":               # ancre
        E(0.45, 0.10, 0.55, 0.20, F, 0.03)
        L(0.50, 0.21, 0.50, 0.80, 0.05)
        L(0.35, 0.30, 0.65, 0.30, 0.04)
        A(0.20, 0.40, 0.80, 0.86, 15, 165, 0.05)
        for s in (-1, 1):
            x = 0.5 + s * 0.29
            P([(x - 0.06, 0.60), (x + 0.06, 0.60), (x, 0.50)] if s < 0 else [(x - 0.06, 0.60), (x + 0.06, 0.60), (x, 0.50)])
    elif name == "arene":              # lances croisées et rondache
        spear(math.radians(35))
        spear(math.radians(-35))
        E(0.32, 0.38, 0.68, 0.74)
        E(0.36, 0.42, 0.64, 0.70, B, 0.02)
        E(0.46, 0.52, 0.54, 0.60, B)
    else:
        raise KeyError(name)
    return img


def sign_pictos(name="Sign_Pictos", seed=81, cols=8, rows=4, tile=256, ss=4):
    """Atlas des enseignes : bois teinté sombre, filet peint, pictogramme crème usé."""
    h, w = rows * tile, cols * tile
    grain = fbm(h, w, seed, 6, 4, aspect=(0.25, 8.0))      # fil horizontal
    fine = fbm(h, w, seed + 1, 4, 32)
    wear = fbm(h, w, seed + 2, 4, 64)
    wood = colorize(np.clip(grain * 0.8 + fine * 0.2, 0, 1), [(0, (40, 30, 22)), (0.5, (66, 50, 36)), (1, (92, 72, 52))])
    icon = np.zeros((h, w), np.float32)
    frame = np.zeros((h, w), np.float32)
    for i, svc in enumerate(SIGN_SERVICES):
        r, c = divmod(i, cols)
        m = _picto(svc, tile * ss).resize((tile, tile), Image.LANCZOS)
        icon[r * tile:(r + 1) * tile, c * tile:(c + 1) * tile] = np.asarray(m, np.float32) / 255.0
    yy, xx = np.mgrid[0:h, 0:w]
    ty, tx = (yy % tile) / tile, (xx % tile) / tile
    edge = np.minimum(np.minimum(tx, 1 - tx), np.minimum(ty, 1 - ty))
    frame = ((edge > 0.035) & (edge < 0.06)).astype(np.float32)
    keep = np.clip((wear - 0.16) * 8.0, 0, 1)                 # écaillage de la peinture
    paint_icon = icon * keep
    paint_frame = frame * keep
    col = wood.copy()
    col = col * (1 - paint_frame[..., None]) + np.array([150, 112, 58], np.float32) * paint_frame[..., None]
    cream = colorize(fine, [(0, (196, 176, 132)), (1, (232, 214, 172))])
    col = col * (1 - paint_icon[..., None]) + cream * paint_icon[..., None]
    col *= (0.88 + 0.12 * fine)[..., None]
    hgt = 0.45 * grain + 0.15 * fine + 0.25 * paint_icon + 0.15 * paint_frame
    save(f"T_Decor_{name}_BaseColor.png", col.clip(0, 255).astype(np.uint8))
    save(f"T_Decor_{name}_Normal.png", normal_from_height(hgt.astype(np.float32), 3.0))
    o = orm(hgt.astype(np.float32), 0.82)
    o[..., 1] = np.clip(o[..., 1].astype(np.float32) - paint_icon * 40, 0, 255).astype(np.uint8)
    save(f"T_Decor_{name}_ORM.png", o)


TEXTURES["Sign_Pictos"] = sign_pictos


# ------------------------------------------------------------------ atelier T2
def cracked_ground(name="Ground_Cracked", seed=91, size=1024):
    """Sol craquelé (désert, Oracle) : plaques de Voronoï séparées de fissures, bord
    déchiqueté en alpha (posé à plat sur le terrain, mode MASK)."""
    h = w = size
    rng = np.random.default_rng(seed)
    pts = rng.random((90, 2)) * size
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float32)
    d1 = np.full((h, w), 1e9, np.float32)
    d2 = np.full((h, w), 1e9, np.float32)
    for px, py in pts:
        for ox in (-size, 0, size):
            for oy in (-size, 0, size):
                d = np.hypot(xx - px - ox, yy - py - oy)
                d2 = np.where(d < d1, d1, np.minimum(d2, d))
                d1 = np.minimum(d1, d)
    edge = d2 - d1
    crack = np.clip(1.0 - edge / 5.0, 0, 1)
    n = fbm(h, w, seed + 1, 5, 12)
    plate = np.clip(edge / 40.0, 0, 1) ** 0.5
    hgt = 0.55 * plate + 0.25 * n - 0.5 * crack
    hgt = (hgt - hgt.min()) / (hgt.max() - hgt.min())
    col = colorize(n, [(0, (150, 118, 82)), (0.5, (184, 150, 108)), (1, (210, 182, 140))])
    col = col * (1 - 0.7 * crack[..., None]) * (0.85 + 0.15 * plate[..., None])
    r = np.hypot(xx - w / 2, yy - h / 2) / (w / 2)
    edge_n = fbm(h, w, seed + 2, 4, 6, tile=False)
    alpha = ((r + (edge_n - 0.5) * 0.35) < 0.92).astype(np.float32) * 255
    rgba = np.concatenate([col.clip(0, 255), alpha[..., None]], -1).astype(np.uint8)
    save(f"T_Decor_{name}_BaseColor.png", rgba, "RGBA")
    save(f"T_Decor_{name}_Normal.png", normal_from_height(hgt, 5.0))
    save(f"T_Decor_{name}_ORM.png", orm(hgt, 0.95, 0.0, 0.5))


def checked(name, seed, c1, c2, n=8, size=512):
    """Tissu à carreaux (nappe) : deux couleurs, trame bruitée."""
    h = w = size
    t = fbm(h, w, seed, 5, 24, aspect=(1.0, 1.0))
    ix = (np.arange(w)[None, :] * n // w) % 2
    iy = (np.arange(h)[:, None] * n // h) % 2
    a = np.array(c1, np.float32)
    b = np.array(c2, np.float32)
    mix = ((ix + iy) % 2 == 1).astype(np.float32)[..., None]
    half = ((ix ^ iy) == 0).astype(np.float32)[..., None] * 0.0
    col = (a * (1 - mix) + b * mix + half) * (0.9 + 0.2 * t)[..., None]
    save(f"T_Decor_{name}_BaseColor.png", col.clip(0, 255).astype(np.uint8))
    save(f"T_Decor_{name}_Normal.png", normal_from_height(t, 1.0))
    save(f"T_Decor_{name}_ORM.png", orm(t, 0.9, 0.0, 0.2))


TEXTURES.update({
    "Leaves_Ivy": lambda: plant_card("Leaves_Ivy", 95, "ivy"),
    "Plant_Daisy": lambda: plant_card("Plant_Daisy", 96, "daisy"),
    "Plant_Duckweed": lambda: plant_card("Plant_Duckweed", 97, "duckweed", 1024),
    "Floor_Checker": lambda: checked("Floor_Checker", 98, (176, 168, 150), (70, 66, 62), 4),
    "Ground_Cracked": cracked_ground,
    "Cloth_Checked": lambda: checked("Cloth_Checked", 92, (206, 196, 172), (150, 52, 42)),
    "Water_Still": lambda: flat("Water_Still", 93, (58, 82, 88), 0.08, 0.12),
    "Stone_Carved": lambda: flat("Stone_Carved", 94, (164, 156, 140), 0.85, 0.16),
})



def bricks(name, seed, base, mortar=(150, 140, 124), rows=16, cols=6, size=1024):
    """Briques rouges en appareil alterné (mur de maison de Dis, T4 pl.23)."""
    h = w = size
    rng = np.random.default_rng(seed)
    y = np.arange(h)[:, None] / h * rows
    row = np.floor(y).astype(np.int32)
    x = np.arange(w)[None, :] / w * cols + (row % 2) * 0.5
    bx = np.floor(x).astype(np.int32)
    fy, fx = y - row, x - bx
    joint = ((fy < 0.1) | (fx < 0.05)).astype(np.float32)
    ids = (row * 97 + bx * 13) % 997
    tone = rng.uniform(0.78, 1.15, 1000)[ids]
    n = fbm(h, w, seed + 1, 5, 24)
    col = np.array(base, np.float32) * (tone * (0.85 + 0.3 * n))[..., None]
    col = col * (1 - joint[..., None]) + np.array(mortar, np.float32) * (0.9 + 0.2 * n)[..., None] * joint[..., None]
    hgt = (1 - joint) * (0.8 + 0.2 * n)
    save(f"T_Decor_{name}_BaseColor.png", col.clip(0, 255).astype(np.uint8))
    save(f"T_Decor_{name}_Normal.png", normal_from_height(hgt.astype(np.float32), 4.0))
    save(f"T_Decor_{name}_ORM.png", orm(hgt.astype(np.float32), 0.88, 0.0, 0.5))


def carpet_diamond(name="Carpet_Diamond", seed=104, size=1024):
    """Tapis à losanges (T4 pl.19) : champ rouge sombre, losanges ocre, bordure."""
    h = w = size
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float32) / size
    u = (xx * 6) % 1.0 - 0.5
    v = (yy * 12) % 1.0 - 0.5
    lz = (np.abs(u) + np.abs(v) * 1.0) < 0.42
    inner = (np.abs(u) + np.abs(v)) < 0.22
    edge = np.minimum(np.minimum(xx, 1 - xx), np.minimum(yy, 1 - yy)) < 0.04
    n = fbm(h, w, seed, 5, 32)
    col = np.zeros((h, w, 3), np.float32) + np.array([118, 36, 34], np.float32)
    col[lz] = [188, 140, 66]
    col[inner] = [70, 46, 60]
    col[edge] = [52, 40, 46]
    col *= (0.88 + 0.2 * n)[..., None]
    save(f"T_Decor_{name}_BaseColor.png", col.clip(0, 255).astype(np.uint8))
    save(f"T_Decor_{name}_Normal.png", normal_from_height(n, 0.8))
    save(f"T_Decor_{name}_ORM.png", orm(n, 0.95, 0.0, 0.2))


TEXTURES.update({
    "Plant_Iris": lambda: plant_card("Plant_Iris", 99, "iris"),
    "Plant_GardenLily": lambda: plant_card("Plant_GardenLily", 100, "garden_lily"),
    "Hedge": lambda: flat("Hedge", 101, (68, 94, 46), 0.92, 0.38, (1.0, 1.0)),
    "Brick_Red": lambda: bricks("Brick_Red", 102, (150, 70, 52)),
    "Cloth_Blue": lambda: flat("Cloth_Blue", 103, (58, 76, 120), 0.9, 0.16, (1.0, 8.0)),
    "Carpet_Diamond": carpet_diamond,
    "Mirror": lambda: flat("Mirror", 105, (190, 196, 200), 0.05, 0.05, metallic=0.9),
})


def parchment_map(name="Parchment_Map", seed=108, size=1024):
    """Carte imaginaire sur parchemin (T6 pl.36, 48) : côtes et courbes de niveau à
    l'encre, rivières, points de villes, cadre. Aucune géographie de la BD, aucun texte."""
    h = w = size
    n = fbm(h, w, seed, 5, 3, tile=False)
    m = fbm(h, w, seed + 7, 4, 24)
    col = np.zeros((h, w, 3), np.float32) + np.array([208, 186, 140], np.float32)
    col *= (0.86 + 0.18 * m)[..., None]
    ink = np.array([62, 44, 30], np.float32)
    land = n > 0.5
    col[land] *= 0.93
    for lvl, wd in ((0.5, 0.008), (0.6, 0.004), (0.7, 0.004)):
        line = np.abs(n - lvl) < wd
        col[line] = ink
    rng = np.random.default_rng(seed)
    for _ in range(3):
        x, y = rng.uniform(0.2, 0.8, 2) * size
        a = rng.uniform(0, 6.28)
        for _ in range(400):
            a += rng.normal(0, 0.15)
            x, y = x + 1.5 * np.cos(a), y + 1.5 * np.sin(a)
            if not (2 <= x < size - 2 and 2 <= y < size - 2):
                break
            col[int(y) - 1:int(y) + 1, int(x) - 1:int(x) + 1] = [70, 80, 96]
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float32)
    for _ in range(9):
        cx, cy = rng.uniform(0.12, 0.88, 2) * size
        col[(xx - cx) ** 2 + (yy - cy) ** 2 < 36] = [120, 30, 26]
    edge = np.minimum(np.minimum(xx, w - 1 - xx), np.minimum(yy, h - 1 - yy)) / size
    col[(edge > 0.03) & (edge < 0.036)] = ink
    col *= (0.75 + 0.25 * np.clip(edge * 10, 0, 1))[..., None]
    save(f"T_Decor_{name}_BaseColor.png", col.clip(0, 255).astype(np.uint8))
    save(f"T_Decor_{name}_Normal.png", normal_from_height(m, 0.5))
    save(f"T_Decor_{name}_ORM.png", orm(m, 0.9, 0.0, 0.15))


TEXTURES.update({
    "Cloth_Green": lambda: flat("Cloth_Green", 106, (52, 92, 58), 0.9, 0.16, (1.0, 8.0)),
    "Wood_Painted": lambda: planks("Wood_Painted", 107, [(0, (44, 78, 66)), (0.5, (60, 102, 86)), (1, (84, 124, 104))], 0.3),
    "Parchment_Map": parchment_map,
})


def stained_rose(name="Glass_Rose", seed=109, size=1024):
    """Rosace de vitrail (T8 pl.33) : secteurs rayonnants de verres colorés, plombs
    sombres, médaillon central à pétales ; motif géométrique, sans figure ni symbole."""
    h = w = size
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float32) / size - 0.5
    r = np.hypot(xx, yy) * 2
    a = (np.arctan2(yy, xx) + np.pi) / (2 * np.pi)
    pal = np.array([[120, 28, 34], [30, 50, 112], [176, 136, 44], [34, 86, 58], [36, 40, 90], [96, 30, 60]], np.float32)
    sec = np.floor(a * 16).astype(int)
    ring = np.digitize(r, [0.18, 0.42, 0.7, 0.95])
    idx = (sec + ring * 3) % 6
    col = pal[idx]
    lead = (np.abs((a * 16) % 1 - 0.5) > 0.47) & (r > 0.18)
    for rr in (0.18, 0.42, 0.7, 0.95):
        lead |= np.abs(r - rr) < 0.012
    for k in range(12):   # médaillons circulaires de l'anneau médian
        ca = 2 * np.pi * (k + 0.5) / 12
        d = np.hypot(xx - 0.28 * np.cos(ca), yy - 0.28 * np.sin(ca))
        lead |= np.abs(d - 0.055) < 0.006
        col[d < 0.05] = [190, 160, 90] if k % 2 else [70, 110, 160]
    petal = (r < 0.18) & (np.cos(a * 2 * np.pi * 8) * 0.06 + 0.12 > r)
    col[petal] = [230, 200, 120]
    col[lead] = [30, 28, 30]
    col[r > 1.0] = [70, 66, 60]
    n = fbm(h, w, seed, 4, 32)
    col *= (0.85 + 0.25 * n)[..., None]
    save(f"T_Decor_{name}_BaseColor.png", col.clip(0, 255).astype(np.uint8))
    save(f"T_Decor_{name}_Normal.png", normal_from_height(lead.astype(np.float32) * 0.5 + 0.1 * n, 2.0))
    save(f"T_Decor_{name}_ORM.png", orm(n, 0.2, 0.0, 0.2))


TEXTURES.update({
    "Glass_Rose": stained_rose,
    "Earth": lambda: flat("Earth", 110, (104, 84, 62), 0.95, 0.3),
})


def crop_top(name, seed, leaf, ripe, rows, accent=None, size=512):
    """Dessus de parcelle vu de loin (cultures, LOD1/LOD2) : rangs parallèles à U
    (même sens que les rangs du maillage), feuillage bruité, sillons de terre entre
    les rangs, points de fleurs ou d'épis optionnels (accent = (couleur, densité))."""
    h = w = size
    n = fbm(h, w, seed, 5, 10)
    d = fbm(h, w, seed + 3, 3, 40)
    v = (np.arange(h)[:, None] + 0.0) / h
    row = 0.5 + 0.5 * np.cos(2 * np.pi * (v * rows - 0.5))  # 1 au centre du rang (v = (k+0,5)/rows)
    cover = np.clip(row * 1.4 + (n - 0.5) * 0.8, 0, 1)
    plant = colorize(n, [(0, leaf), (1, ripe)])
    soil = np.array((70, 56, 40), np.float32) * (0.85 + 0.3 * d)[..., None]
    col = plant * cover[..., None] + soil * (1 - cover[..., None])
    if accent:
        c, dens = accent
        rng = np.random.default_rng(seed)
        m = (rng.random((h, w)) < dens) & (cover > 0.6)
        m = m | np.roll(m, 1, 0) | np.roll(m, 1, 1)
        col[m] = c
    save(f"T_Decor_{name}_BaseColor.png", col.clip(0, 255).astype(np.uint8))
    save(f"T_Decor_{name}_Normal.png", normal_from_height(cover * 0.7 + n * 0.3, 2.0))
    save(f"T_Decor_{name}_ORM.png", orm(n, 0.9, 0.0, 0.3))


TEXTURES.update({
    "Crop_Top_Wheat": lambda: crop_top("Crop_Top_Wheat", 111, (150, 120, 52), (206, 172, 92), 6),
    "Crop_Top_Barley": lambda: crop_top("Crop_Top_Barley", 112, (156, 140, 78), (204, 190, 126), 6),
    "Crop_Top_Corn": lambda: crop_top("Crop_Top_Corn", 113, (46, 84, 38), (98, 132, 58), 5, ((196, 168, 70), 0.004)),
    "Crop_Top_Sunflower": lambda: crop_top("Crop_Top_Sunflower", 114, (48, 86, 40), (90, 124, 54), 5, ((222, 176, 34), 0.012)),
    "Crop_Top_Rapeseed": lambda: crop_top("Crop_Top_Rapeseed", 115, (180, 168, 40), (226, 206, 52), 6, ((240, 222, 70), 0.03)),
    "Crop_Top_Vine": lambda: crop_top("Crop_Top_Vine", 116, (44, 74, 36), (82, 108, 50), 2, ((70, 40, 70), 0.003)),
})


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
