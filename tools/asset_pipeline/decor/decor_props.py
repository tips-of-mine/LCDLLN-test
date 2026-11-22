# Atelier décors — constructeurs procéduraux des props (mobilier, objets, extérieurs,
# militaire) issus de la BD. Chargé par build_decor.py (register) : chaque fonction
# p_<nom>(spec, lod) renvoie une liste de Part (un matériau par Part).
#
# Repère Blender : X largeur, Y profondeur, Z haut ; 1 unité = 1 m. Un prop qui
# remplace un asset existant reproduit sa boîte englobante (bbox_ref du registre,
# convertie depuis glTF : Y_blender = -Z_gltf, Z_blender = Y_gltf), pivot compris.
# LOD : `S(lod, n)` réduit le nombre de côtés ; les détails fins sont réservés au LOD0
# (`lod == 0`) ou retirés au LOD2.
import math
import random

from mathutils import Matrix, Vector

API = {}


def S(lod, n):
    return max(3, int(round(n * (1.0, 0.6, 0.35)[lod])))


class M:
    """Accumulateur de parties par matériau (texture T_Decor_<tex>)."""

    def __init__(self):
        self.parts = {}

    def p(self, tex, alpha=False):
        if tex not in self.parts:
            self.parts[tex] = API["Part"](API["material"](tex, alpha))
        return self.parts[tex]

    def out(self):
        return list(self.parts.values())


# ------------------------------------------------------------------ primitives
def _uvbox(p, n, s):
    ax = max(range(3), key=lambda i: abs(n[i]))
    return ((p.y, p.z) if ax == 0 else (p.x, p.z) if ax == 1 else (p.x, p.y))


def box(part, mn, mx, uv=1.0, xf=None):
    """Pavé aligné (mn, mx), UV projetées par face en mètres (×uv), transformé par xf."""
    x0, y0, z0 = mn
    x1, y1, z1 = mx
    c = [Vector(v) for v in [(x0, y0, z0), (x1, y0, z0), (x1, y1, z0), (x0, y1, z0),
                             (x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1)]]
    faces = [(0, 3, 2, 1), (4, 5, 6, 7), (0, 1, 5, 4), (1, 2, 6, 5), (2, 3, 7, 6), (3, 0, 4, 7)]
    if xf is not None:
        c = [xf @ v for v in c]
    for f in faces:
        a, b, d = c[f[0]], c[f[1]], c[f[2]]
        n = (b - a).cross(d - a)
        i = len(part.v)
        part.v += [c[k] for k in f]
        part.f.append((i, i + 1, i + 2, i + 3))
        part.uv.append([tuple(x * uv for x in _uvbox(c[k], n, uv)) for k in f])
        part.cn.append(None)
    part.smooth = False


def cyl(part, a, b, r0, r1, sides, cap=True, v_scale=1.0):
    """Cylindre / tronc de cône de a à b (Vector), capuchons en éventail."""
    a, b = Vector(a), Vector(b)
    base = len(part.v)
    API["tube"](part, [a, b], [r0, r1], sides, v_scale)
    if not cap:
        return
    for ring, ctr in ((0, a), (1, b)):
        ci = len(part.v)
        part.v.append(ctr)
        for s in range(sides):
            s2 = (s + 1) % sides
            i0, i1 = base + ring * sides + s, base + ring * sides + s2
            part.f.append((ci, i1, i0) if ring == 0 else (ci, i0, i1))
            part.uv.append([(0.5, 0.5), (0.5 + 0.5 * math.cos(s2), 0.5 + 0.5 * math.sin(s2)),
                            (0.5 + 0.5 * math.cos(s), 0.5 + 0.5 * math.sin(s))])
            part.cn.append(None)


def lathe(part, profile, sides, v_scale=1.0):
    """Révolution autour de Z d'un profil [(r, z), ...] (tonneaux, pots, bouteilles)."""
    pts = [Vector((0, 0, z)) for r, z in profile]
    API["tube"](part, pts, [max(0.001, r) for r, z in profile], sides, v_scale)


def plank_row(part, x0, x1, y0, y1, z0, z1, n, axis="x", gap=0.006, jitter=0.0, rng=None):
    """n planches côte à côte entre x0..x1 (axis x) ou y0..y1 (axis y)."""
    for k in range(n):
        j = rng.uniform(-jitter, jitter) if rng else 0.0
        if axis == "x":
            a = x0 + (x1 - x0) * k / n + gap / 2
            b = x0 + (x1 - x0) * (k + 1) / n - gap / 2
            box(part, (a, y0, z0), (b, y1, z1 + j))
        else:
            a = y0 + (y1 - y0) * k / n + gap / 2
            b = y0 + (y1 - y0) * (k + 1) / n - gap / 2
            box(part, (x0, a, z0), (x1, b, z1 + j))


def wheel(m, c, r, w, lod, spokes=8, axis="x"):
    """Roue de bois cerclée de fer, axe selon X (défaut) ou Y."""
    d = Vector((w / 2, 0, 0)) if axis == "x" else Vector((0, w / 2, 0))
    c = Vector(c)
    # Jante : cerclage de fer (bande sans fond) sur un anneau de bois, moyeu, rayons.
    cyl(m.p("Iron"), c - d, c + d, r, r, S(lod, 16), cap=False)
    cyl(m.p("Wood_Dark"), c - d * 0.9, c + d * 0.9, r * 0.9, r * 0.9, S(lod, 16), cap=False)
    cyl(m.p("Wood_Dark"), c - d * 1.6, c + d * 1.6, r * 0.18, r * 0.18, S(lod, 8))
    if lod < 2:
        for k in range(spokes if lod == 0 else spokes // 2):
            a = 2 * math.pi * k / spokes
            if axis == "x":
                e = Vector((0, math.cos(a), math.sin(a))) * r * 0.92
            else:
                e = Vector((math.cos(a), 0, math.sin(a))) * r * 0.92
            cyl(m.p("Wood_Dark"), c, c + e, 0.025, 0.02, 4, False)


def candle(m, base, h, r, lod):
    cyl(m.p("Wax"), base, base + Vector((0, 0, h)), r, r * 0.95, S(lod, 10))
    if lod == 0:
        cyl(m.p("Iron"), base + Vector((0, 0, h)), base + Vector((0, 0, h + 0.015)), 0.002, 0.002, 3, False)


# =================================================================== mobilier
def p_barrel(spec, lod, H=0.9, R=0.35, at=None, lying=False):
    m = M()
    prof = [(R * (0.86 + 0.14 * math.sin(math.pi * t)), H * t) for t in [k / 8 for k in range(9)]]
    part = m.p("Wood_Plank")
    i0 = len(part.v)
    lathe(part, prof, S(lod, 16), 1.0)
    cyl(part, (0, 0, 0.03), (0, 0, 0.03), R * 0.86, R * 0.86, S(lod, 16))
    cyl(part, (0, 0, H - 0.03), (0, 0, H - 0.03), R * 0.86, R * 0.86, S(lod, 16))
    if lod < 2:
        for t in (0.12, 0.3, 0.7, 0.88):
            r = R * (0.86 + 0.14 * math.sin(math.pi * t)) + 0.006
            cyl(m.p("Iron"), (0, 0, H * t - 0.02), (0, 0, H * t + 0.02), r, r, S(lod, 16), False)
    return m.out()


def p_barrel_holder(spec, lod):
    """Chantier : berceau de bois et deux tonneaux couchés, robinet (bbox ±0,68 x 1,25)."""
    m = M()
    w = m.p("Wood_Dark")
    for x in (-0.6, 0.6):
        box(w, (x - 0.05, -0.33, 0), (x + 0.05, 0.33, 0.62))
        box(w, (x - 0.05, -0.36, 0.55), (x + 0.05, 0.36, 0.62))
    box(w, (-0.66, -0.3, 0.25), (0.66, -0.22, 0.32))
    box(w, (-0.66, 0.22, 0.25), (0.66, 0.3, 0.32))
    for z0 in (0.33, 0.33):
        pass
    for k, (z, s) in enumerate(((0.62, 1.0),)):
        parts = p_barrel(spec, lod, 1.2, 0.3)
        for p in parts:
            pm = m.p(p.mat.name.replace("MI_Decor_", ""))
            off = len(pm.v)
            rot = Matrix.Rotation(math.pi / 2, 3, "Y")
            pm.v += [rot @ v + Vector((-0.6, 0, z + 0.3)) for v in p.v]
            pm.f += [tuple(i + off for i in f) for f in p.f]
            pm.uv += p.uv
            pm.cn += p.cn
    if lod == 0:
        cyl(m.p("Brass"), (0.6, 0, 0.82), (0.66, 0, 0.82), 0.02, 0.02, 6)
    return m.out()


def p_chest(spec, lod):
    """Coffre ferré à couvercle bombé (bbox ±0,64 x 0,69 x ±0,38)."""
    m = M()
    w = m.p("Wood_Dark")
    box(w, (-0.62, -0.36, 0), (0.62, 0.36, 0.45))
    lid = m.p("Wood_Dark")
    n = S(lod, 8)
    for k in range(n):
        a0, a1 = math.pi * k / n, math.pi * (k + 1) / n
        y0, z0 = -0.36 * math.cos(a0), 0.45 + 0.22 * math.sin(a0)
        y1, z1 = -0.36 * math.cos(a1), 0.45 + 0.22 * math.sin(a1)
        i = len(lid.v)
        lid.v += [Vector((-0.62, y0, z0)), Vector((0.62, y0, z0)), Vector((0.62, y1, z1)), Vector((-0.62, y1, z1))]
        lid.f.append((i, i + 1, i + 2, i + 3))
        lid.uv.append([(0, k / n), (1.2, k / n), (1.2, (k + 1) / n), (0, (k + 1) / n)])
        lid.cn.append(None)
        for x in (-0.62, 0.62):
            i = len(lid.v)
            lid.v += [Vector((x, 0, 0.45)), Vector((x, y0, z0)), Vector((x, y1, z1))]
            lid.f.append((i, i + 1, i + 2) if x > 0 else (i, i + 2, i + 1))
            lid.uv.append([(0, 0), (y0, z0), (y1, z1)])
            lid.cn.append(None)
    if lod < 2:
        fe = m.p("Iron")
        for x in (-0.45, 0.45):
            box(fe, (x - 0.035, -0.38, 0.02), (x + 0.035, 0.38, 0.46))
        box(fe, (-0.07, -0.385, 0.32), (0.07, -0.36, 0.44))
        box(fe, (-0.64, -0.38, -0.02), (0.64, 0.38, 0.03))
    return m.out()


def p_table_large(spec, lod):
    """Grande table de bois massive à tréteaux (bbox ±1,42 x 0,81 x ±0,55)."""
    m = M()
    w = m.p("Wood_Plank")
    plank_row(w, -1.42, 1.42, -0.55, 0.55, 0.73, 0.81, {0: 5, 1: 3, 2: 1}[lod], axis="y")
    d = m.p("Wood_Dark")
    for x in (-1.1, 1.1):
        box(d, (x - 0.06, -0.42, 0.0), (x + 0.06, 0.42, 0.08))
        box(d, (x - 0.06, -0.06, 0.08), (x + 0.06, 0.06, 0.73))
        if lod < 2:
            box(d, (x - 0.05, -0.4, 0.62), (x + 0.05, 0.4, 0.73))
    if lod < 2:
        box(d, (-1.1, -0.04, 0.3), (1.1, 0.04, 0.38))
    return m.out()


def p_bench(spec, lod):
    m = M()
    w = m.p("Wood_Plank")
    plank_row(w, -1.39, 1.39, -0.27, 0.27, 0.45, 0.53, {0: 2, 1: 1, 2: 1}[lod], axis="y")
    d = m.p("Wood_Dark")
    for x in (-1.15, 1.15):
        box(d, (x - 0.05, -0.22, 0), (x + 0.05, 0.22, 0.45))
    return m.out()


def p_chair(spec, lod):
    m = M()
    d = m.p("Wood_Dark")
    for x in (-0.24, 0.24):
        for y in (-0.24, 0.22):
            top = 1.12 if y > 0 else 0.45
            box(d, (x - 0.03, y - 0.03, 0), (x + 0.03, y + 0.03, top))
    plank_row(m.p("Wood_Plank"), -0.29, 0.29, -0.29, 0.26, 0.42, 0.47, {0: 3, 1: 2, 2: 1}[lod], axis="x")
    if lod < 2:
        for z in (0.7, 0.95):
            box(d, (-0.24, 0.19, z), (0.24, 0.25, z + 0.1))
    return m.out()


def p_stool(spec, lod):
    m = M()
    cyl(m.p("Wood_Plank"), (0, 0, 0.52), (0, 0, 0.58), 0.24, 0.24, S(lod, 14))
    d = m.p("Wood_Dark")
    for k in range(3):
        a = k * 2 * math.pi / 3
        cyl(d, (math.cos(a) * 0.2, math.sin(a) * 0.2, 0), (math.cos(a) * 0.12, math.sin(a) * 0.12, 0.53), 0.025, 0.022, S(lod, 6))
    return m.out()


def p_bed(spec, lod):
    """Lit de bois couvert de fourrures (bbox ±0,94 x H x ±1,21, H selon bbox_ref)."""
    H = spec["bbox_ref"][3]
    m = M()
    d = m.p("Wood_Dark")
    box(d, (-0.94, -1.21, 0.2), (0.94, 1.21, 0.36))
    for x in (-0.88, 0.88):
        for y in (-1.15, 1.15):
            box(d, (x - 0.06, y - 0.06, 0), (x + 0.06, y + 0.06, H if y > 0 else 0.55))
    box(d, (-0.94, 1.12, 0.36), (0.94, 1.21, H - 0.05))
    box(m.p("Cloth_White"), (-0.86, -1.12, 0.36), (0.86, 1.1, 0.5))
    fur = m.p("Fur")
    box(fur, (-0.9, -1.16, 0.5), (0.9, 0.55, 0.58))
    if lod < 2:
        box(m.p("Cloth_White"), (-0.6, 0.72, 0.5), (0.6, 1.05, 0.64))
    return m.out()


def p_candlestick_stand(spec, lod):
    """Chandelier sur pied en fer à cinq bougies (bbox ±0,37 x 1,30)."""
    m = M()
    fe = m.p("Iron")
    for k in range(3):
        a = k * 2 * math.pi / 3
        cyl(fe, (math.cos(a) * 0.3, math.sin(a) * 0.3, 0), (0, 0, 0.18), 0.02, 0.025, S(lod, 6), False)
    cyl(fe, (0, 0, 0.18), (0, 0, 1.12), 0.025, 0.02, S(lod, 8))
    cyl(fe, (-0.32, 0, 1.1), (0.32, 0, 1.1), 0.015, 0.015, S(lod, 6))
    for x in (-0.3, -0.15, 0, 0.15, 0.3):
        cyl(fe, (x, 0, 1.1), (x, 0, 1.13), 0.03, 0.035, S(lod, 8))
        candle(m, Vector((x, 0, 1.13)), 0.15, 0.018, lod)
    return m.out()


def p_candlestick_triple(spec, lod):
    m = M()
    br = m.p("Brass")
    cyl(br, (0, 0, 0), (0, 0, 0.03), 0.07, 0.06, S(lod, 12))
    cyl(br, (0, 0, 0.03), (0, 0, 0.25), 0.012, 0.012, S(lod, 6))
    cyl(br, (-0.18, 0, 0.24), (0.18, 0, 0.24), 0.01, 0.01, S(lod, 6))
    for x in (-0.18, 0, 0.18):
        cyl(br, (x, 0, 0.24), (x, 0, 0.27), 0.025, 0.03, S(lod, 8))
        candle(m, Vector((x, 0, 0.27)), 0.17, 0.015, lod)
    return m.out()


def p_candle(spec, lod):
    m = M()
    h = spec["height_m"][1] * 0.95
    candle(m, Vector((0, 0, 0)), h - 0.015, 0.045, lod)
    if lod == 0:
        cyl(m.p("Wax"), (0.02, 0.0, 0), (0.03, 0.0, h * 0.4), 0.012, 0.004, 5)
    return m.out()


def p_candlestick(spec, lod):
    """Bougeoir à anse (bbox X -0,18..0,08)."""
    m = M()
    br = m.p("Brass")
    cyl(br, (0, 0, 0), (0, 0, 0.02), 0.08, 0.075, S(lod, 12))
    cyl(br, (0, 0, 0.02), (0, 0, 0.05), 0.025, 0.03, S(lod, 8))
    API["tube"](br, [Vector((-0.07, 0, 0.015)), Vector((-0.17, 0, 0.03)), Vector((-0.17, 0, 0.08)), Vector((-0.1, 0, 0.06))],
                [0.008] * 4, S(lod, 5))
    candle(m, Vector((0, 0, 0.05)), 0.09, 0.018, lod)
    return m.out()


def p_banner(spec, lod, drop=1.55):
    """Bannière murale : potence de bois depuis le mur (x=0) et étoffe neutre."""
    m = M()
    d = m.p("Wood_Dark")
    box(d, (0, -0.06, 0.72), (1.61, 0.06, 0.84))
    box(d, (0, -0.06, 0.2), (0.1, 0.06, 0.84))
    API["tube"](d, [Vector((0.05, 0, 0.3)), Vector((0.8, 0, 0.75))], [0.03, 0.03], S(lod, 5))
    cyl(d, (0.85, -0.09, 0.64), (0.85, 0.09, 0.64), 0.02, 0.02, S(lod, 6))
    low = spec["bbox_ref"][2]   # bas de la pointe (glTF Y min)
    _cloth(m.p("Cloth_Red"), 0.45, 1.25, 0.64, 0.64 + (low - 0.64) / 1.18, lod)
    return m.out()


def _cloth(part, x0, x1, ztop, zbot, lod, y=0.0, pointed=True):
    """Étoffe pendante ondulée (pli sinusoïdal), pointe en bas."""
    nx, nz = {0: (8, 6), 1: (4, 3), 2: (2, 1)}[lod]
    grid = []
    for j in range(nz + 1):
        t = j / nz
        row = []
        for i in range(nx + 1):
            u = i / nx
            x = x0 + (x1 - x0) * u
            z = ztop + (zbot - ztop) * t
            if pointed and j == nz:
                z += (zbot - ztop) * 0.18 * (1 - abs(u - 0.5) * 2)
            yy = y + 0.04 * math.sin(u * math.pi * 3) * t
            row.append(Vector((x, yy, z)))
        grid.append(row)
    for j in range(nz):
        for i in range(nx):
            k = len(part.v)
            part.v += [grid[j][i], grid[j][i + 1], grid[j + 1][i + 1], grid[j + 1][i]]
            part.f.append((k, k + 1, k + 2, k + 3))
            part.uv.append([(i / nx, 1 - j / nz), ((i + 1) / nx, 1 - j / nz), ((i + 1) / nx, 1 - (j + 1) / nz), (i / nx, 1 - (j + 1) / nz)])
            part.cn.append(None)


def p_banner_cloth(spec, lod):
    m = M()
    h = spec["bbox_ref"][3] - spec["bbox_ref"][2]
    cyl(m.p("Wood_Dark"), (-0.41, 0, -0.02), (0.41, 0, -0.02), 0.02, 0.02, S(lod, 6))
    _cloth(m.p("Cloth_Red"), -0.38, 0.38, -0.02, -0.02 + (-h + 0.02) / 1.18, lod)
    return m.out()


def p_shield(spec, lod):
    """Bouclier rond de planches à umbo de fer, face vers -Y (bbox ±0,31, ép. 0,18)."""
    m = M()
    w = m.p("Wood_Plank")
    n = S(lod, 20)
    cyl(w, (0, -0.06, 0), (0, 0, 0), 0.31, 0.31, n)
    cyl(m.p("Iron"), (0, -0.062, 0), (0, -0.04, 0), 0.315, 0.315, n, False)
    cyl(m.p("Iron"), (0, -0.06, 0), (0, -0.18, 0), 0.09, 0.03, S(lod, 12))
    # Disque dans le plan XZ, face (umbo) vers -Y Blender = +Z glTF (repère d'origine).
    return m.out()


def p_weapon_stand(spec, lod):
    """Râtelier d'armes : bâti de bois, lances et haches (bbox ±0,69 x 1,11 x ±0,49)."""
    m = M()
    d = m.p("Wood_Dark")
    for x in (-0.65, 0.65):
        box(d, (x - 0.04, -0.45, 0), (x + 0.04, 0.45, 0.06))
        box(d, (x - 0.04, -0.04, 0), (x + 0.04, 0.04, 1.0))
    box(d, (-0.69, -0.05, 0.92), (0.69, 0.05, 1.0))
    box(d, (-0.69, -0.12, 0.25), (0.69, 0.12, 0.3))
    fe = m.p("Iron")
    for k, x in enumerate([-0.45, -0.25, -0.05, 0.15, 0.35, 0.5][: (6 if lod == 0 else 3)]):
        cyl(m.p("Wood_Plank"), (x, 0.08, 0.25), (x, -0.02, 1.08), 0.016, 0.016, S(lod, 6))
        if k % 2 == 0:
            cyl(fe, (x, -0.02, 1.0), (x, -0.03, 1.11), 0.02, 0.002, S(lod, 6))
        else:
            box(fe, (x - 0.002, -0.06, 0.95), (x + 0.012, 0.05, 1.06))
    return m.out()


def p_torch_metal(spec, lod):
    """Torche murale : patte de fer au mur (y=0), torche inclinée (bbox ±0,11)."""
    m = M()
    fe = m.p("Iron")
    box(fe, (-0.1, -0.02, -0.15), (0.1, 0.0, 0.05))  # patte murale
    API["tube"](fe, [Vector((0, -0.01, -0.05)), Vector((0, -0.25, 0.1))], [0.012, 0.012], S(lod, 5))
    cyl(fe, (0, -0.25, 0.02), (0, -0.28, 0.12), 0.045, 0.05, S(lod, 8))
    cyl(m.p("Wood_Dark"), (0, -0.2, -0.28), (0, -0.3, 0.2), 0.022, 0.03, S(lod, 6))
    cyl(m.p("Rope"), (0, -0.29, 0.18), (0, -0.31, 0.37), 0.05, 0.03, S(lod, 8))
    return m.out()


def p_mug(spec, lod):
    m = M()
    w = m.p("Wood_Plank")
    lathe(w, [(0.065, 0), (0.07, 0.1), (0.068, 0.18)], S(lod, 12))
    cyl(w, (0, 0, 0.01), (0, 0, 0.01), 0.065, 0.065, S(lod, 12))
    if lod < 2:
        for t in (0.03, 0.15):
            cyl(m.p("Iron"), (0, 0, t), (0, 0, t + 0.012), 0.072, 0.07, S(lod, 12), False)
        API["tube"](w, [Vector((0.06, 0, 0.14)), Vector((0.115, 0, 0.12)), Vector((0.115, 0, 0.05)), Vector((0.06, 0, 0.04))],
                    [0.012] * 4, S(lod, 5))
    return m.out()


def p_plate(spec, lod):
    m = M()
    lathe(m.p("Wood_Plank"), [(0.001, 0.0), (0.13, 0.004), (0.17, 0.02)], S(lod, 16))
    return m.out()


def p_pot(spec, lod, lid=False):
    m = M()
    c = m.p("Ceramic")
    lathe(c, [(0.12, 0), (0.24, 0.05), (0.27, 0.12), (0.22, 0.18), (0.2, 0.2)], S(lod, 16))
    cyl(c, (0, 0, 0.005), (0, 0, 0.005), 0.12, 0.12, S(lod, 16))
    if lid:
        lathe(c, [(0.21, 0.19), (0.12, 0.23), (0.03, 0.24), (0.02, 0.26)], S(lod, 12))
    return m.out()


def p_pot_lid(spec, lod): return p_pot(spec, lod, True)


def _bottle(m, base, H, R, tex, lod, neck=0.35):
    lathe(m.p(tex), [(R * 0.8, 0), (R, H * 0.08), (R, H * (1 - neck) * 0.9), (R * 0.35, H * (1 - neck)),
                     (R * 0.3, H * 0.92), (R * 0.36, H * 0.95)], S(lod, 12))
    cyl(m.p(tex), base, base, R * 0.8, R * 0.8, S(lod, 12))
    if lod < 2:
        cyl(m.p("Wood_Plank"), (base.x, base.y, H * 0.93), (base.x, base.y, H), R * 0.3, R * 0.28, S(lod, 6))


def p_potion(spec, lod):
    m = M()
    H = spec["bbox_ref"][3]
    R = spec["bbox_ref"][1]
    tex = {"Potion_1": "Glass_Green", "Potion_2": "Glass_Amber", "Potion_4": "Glass_Clear"}.get(spec["id"], "Glass_Green")
    lathe(m.p(tex), [(R * 0.5, 0), (R, H * 0.25), (R * 0.95, H * 0.55), (R * 0.3, H * 0.72), (R * 0.28, H * 0.9)], S(lod, 12))
    cyl(m.p("Wood_Plank"), (0, 0, H * 0.88), (0, 0, H), R * 0.3, R * 0.27, S(lod, 6))
    return m.out()


def p_bottle(spec, lod):
    m = M()
    _bottle(m, Vector((0, 0, 0)), spec["bbox_ref"][3], spec["bbox_ref"][1], "Glass_Green", lod)
    return m.out()


def p_small_bottles(spec, lod):
    m = M()
    for k, x in enumerate((-0.075, 0.0, 0.07)[: (3 if lod < 2 else 1)]):
        _bottle_at(m, x, 0.12 + 0.03 * (k % 2), 0.028, ("Glass_Amber", "Glass_Green", "Glass_Clear")[k], lod)
    return m.out()


def _bottle_at(m, x, H, R, tex, lod):
    p = m.p(tex)
    i = len(p.v)
    lathe(p, [(R * 0.8, 0), (R, H * 0.1), (R, H * 0.6), (R * 0.4, H * 0.75), (R * 0.35, H)], S(lod, 10))
    for k in range(i, len(p.v)):
        p.v[k] = p.v[k] + Vector((x, 0, 0))


def p_shelf_bottles(spec, lod):
    """Étagère murale chargée de fioles (bbox ±0,57 x 0,63, profondeur 0,29)."""
    m = M()
    d = m.p("Wood_Dark")
    for z in (0.0, 0.3, 0.58):
        box(d, (-0.57, -0.29, z), (0.57, -0.01, z + 0.04))
    for x in (-0.55, 0.55):
        box(d, (x - 0.02, -0.29, 0), (x + 0.02, -0.01, 0.62))
    rng = random.Random(spec["seed"])
    n = {0: 12, 1: 6, 2: 0}[lod]
    for k in range(n):
        x = -0.48 + 0.96 * (k % 6) / 5
        z = 0.04 if k < 6 else 0.34
        p = m.p(("Glass_Amber", "Glass_Green", "Glass_Clear")[k % 3])
        i = len(p.v)
        h = rng.uniform(0.12, 0.22)
        lathe(p, [(0.03, 0), (0.035, h * 0.6), (0.012, h * 0.8), (0.012, h)], S(lod, 8))
        for q in range(i, len(p.v)):
            p.v[q] = p.v[q] + Vector((x, -0.15, z))
    return m.out()


def _book(m, mn, mx, lod):
    box(m.p("Book_Covers"), mn, mx, uv=2.0)
    if lod == 0:
        box(m.p("Paper"), (mn[0] + 0.004, mn[1] - 0.002, mn[2] + 0.006), (mx[0] - 0.012, mx[1] + 0.002, mx[2] - 0.006), uv=2.0)


def p_book_flat(spec, lod):
    m = M()
    b = spec["bbox_ref"]  # glTF : X, Y (haut), Z (profondeur)
    _book(m, (b[0], -b[5], 0.0), (b[1], -b[4], b[3] - b[2]), lod)
    return m.out()


def p_book_standing(spec, lod):
    m = M()
    b = spec["bbox_ref"]
    _book(m, (b[0], -b[5], 0.0), (b[1], -b[4], b[3]), lod)
    return m.out()


def p_book_stack(spec, lod):
    m = M()
    rng = random.Random(spec["seed"])
    z = 0.0
    top = spec["bbox_ref"][3]
    while z < top - 0.02:
        h = rng.uniform(0.03, 0.05)
        w, d = rng.uniform(0.2, 0.28), rng.uniform(0.15, 0.22)
        xf = Matrix.Rotation(rng.uniform(-0.25, 0.25), 4, "Z")
        box(m.p("Book_Covers"), (-w / 2, -d / 2, z), (w / 2, d / 2, z + h), 2.0, xf)
        z += h
    return m.out()


def p_book_group(spec, lod):
    """Rangée de livres debout (largeur selon bbox_ref)."""
    m = M()
    b = spec["bbox_ref"]
    rng = random.Random(spec["seed"])
    x = b[0]
    while x < b[1] - 0.02:
        t = rng.uniform(0.03, 0.06)
        h = rng.uniform(0.2, b[3])
        box(m.p("Book_Covers"), (x, -0.09 if lod < 2 else -0.09, 0), (x + t, 0.14, h), 2.0)
        x += t + 0.003
    return m.out()


# ================================================================== extérieurs
def p_stall(spec, lod):
    """Étal de marché à auvent rayé (bbox ±0,92 x 2,62 x ±0,46)."""
    m = M()
    d = m.p("Wood_Dark")
    for x in (-0.88, 0.88):
        for y in (-0.42, 0.42):
            box(d, (x - 0.04, y - 0.04, 0), (x + 0.04, y + 0.04, 2.3 if y > 0 else 2.05))
    plank_row(m.p("Wood_Plank"), -0.92, 0.92, -0.46, 0.46, 0.8, 0.86, {0: 4, 1: 2, 2: 1}[lod], axis="y")
    box(d, (-0.9, -0.44, 0.0), (0.9, -0.4, 0.8))
    aw = m.p("Cloth_Striped")
    i = len(aw.v)
    aw.v += [Vector((-0.92, -0.46, 2.05)), Vector((0.92, -0.46, 2.05)), Vector((0.92, 0.46, 2.6)), Vector((-0.92, 0.46, 2.6))]
    aw.f.append((i, i + 1, i + 2, i + 3))
    aw.uv.append([(0, 0), (1, 0), (1, 1), (0, 1)])
    aw.cn.append(None)
    if lod == 0:
        _cloth(aw, -0.92, 0.92, 2.05, 1.85, 1, y=-0.46, pointed=False)
    return m.out()


def p_stall_cart(spec, lod):
    """Charrette-étal : caisse, grandes roues, brancards vers -X, auvent (bbox X -2,11..0,91)."""
    m = M()
    d = m.p("Wood_Dark")
    box(m.p("Wood_Plank"), (-0.85, -0.5, 0.55), (0.85, 0.5, 0.62))
    for y in (-0.5, 0.5):
        box(m.p("Wood_Plank"), (-0.85, y - 0.03, 0.62), (0.85, y + 0.03, 0.95))
    wheel(m, (0, -0.53 + 0.05, 0.55), 0.55, 0.08, lod, axis="y")
    wheel(m, (0, 0.53 - 0.05, 0.55), 0.55, 0.08, lod, axis="y")
    for y in (-0.35, 0.35):
        box(d, (-2.11, y - 0.03, 0.5), (-0.85, y + 0.03, 0.58))
    for x in (-0.8, 0.8):
        for y in (-0.45, 0.45):
            box(d, (x - 0.03, y - 0.03, 0.6), (x + 0.03, y + 0.03, 2.4))
    aw = m.p("Cloth_Striped")
    i = len(aw.v)
    aw.v += [Vector((-0.91, -0.53, 2.3)), Vector((0.91, -0.53, 2.3)), Vector((0.91, 0.53, 2.63)), Vector((-0.91, 0.53, 2.63))]
    aw.f.append((i, i + 1, i + 2, i + 3))
    aw.uv.append([(0, 0), (1, 0), (1, 1), (0, 1)])
    aw.cn.append(None)
    box(d, (-0.15, -0.05, 0.0), (-0.05, 0.05, 0.55))  # béquille
    return m.out()


def p_wagon(spec, lod, loaded=False):
    """Chariot à quatre roues, timon vers +Y (bbox X ±0,98 ; glTF Z -3,15..0,87)."""
    m = M()
    w = m.p("Wood_Plank")
    box(w, (-0.75, -0.8, 0.62), (0.75, 1.9, 0.7))
    for x in (-0.75, 0.75):
        box(w, (x - 0.03 if x < 0 else x - 0.03, -0.8, 0.7), (x + 0.03, 1.9, 1.05))
    for y in (-0.8, 1.9):
        box(w, (-0.75, y - 0.03, 0.7), (0.75, y + 0.03, 1.0))
    for y in (-0.3, 1.45):
        wheel(m, (-0.9, y, 0.58), 0.58, 0.1, lod)
        wheel(m, (0.9, y, 0.58), 0.58, 0.1, lod)
    d = m.p("Wood_Dark")
    box(d, (-0.05, 1.9, 0.45), (0.05, 3.15, 0.53))
    box(d, (-0.4, 3.0, 0.45), (0.4, 3.08, 0.53))
    if lod < 2 and not loaded:  # arceaux de bâche (hauteur d'origine 1,53 m)
        for y in (-0.6, 0.55, 1.7):
            API["tube"](d, [Vector((-0.74, y, 1.0)), Vector((-0.55, y, 1.42)), Vector((0.0, y, 1.53)),
                            Vector((0.55, y, 1.42)), Vector((0.74, y, 1.0))], [0.025] * 5, 4)
        box(d, (-0.03, -0.6, 1.49), (0.03, 1.7, 1.53))
    if loaded:
        rng = random.Random(spec["seed"])
        for k in range({0: 7, 1: 4, 2: 2}[lod]):
            x, y = rng.uniform(-0.45, 0.45), rng.uniform(-0.55, 1.6)
            if k % 3 == 0:
                for p in p_barrel(spec, lod, 0.7, 0.26):
                    _merge(m, p, Matrix.Translation((x, y, 0.7)))
            elif k % 3 == 1:
                box(m.p("Wood_Plank"), (x - 0.25, y - 0.2, 0.7), (x + 0.25, y + 0.2, 1.05))
            else:
                box(m.p("Canvas"), (x - 0.28, y - 0.22, 0.7), (x + 0.28, y + 0.22, 1.2))
    return m.out()


def _merge(m, p, xf):
    pm = m.p(p.mat.name.replace("MI_Decor_", ""))
    off = len(pm.v)
    pm.v += [xf @ Vector(v) for v in p.v]
    pm.f += [tuple(i + off for i in f) for f in p.f]
    pm.uv += p.uv
    pm.cn += p.cn


def p_wagon_loaded(spec, lod): return p_wagon(spec, lod, True)


def p_cage(spec, lod, W=0.85, H=0.8):
    """Cage de fer à barreaux rivetés, socle de planches."""
    if spec["id"] == "Cage_Large":
        W, H = 2.2, 2.2
    m = M()
    fe = m.p("Iron")
    h2 = W / 2
    box(m.p("Wood_Dark"), (-h2, -h2, -0.04 if W < 1 else 0), (h2, h2, 0.06))
    nb = {0: 7, 1: 4, 2: 2}[lod]
    r = 0.012 if W < 1 else 0.025
    for k in range(nb):
        t = -h2 + W * (k + 0.5) / nb
        for a, b in (((t, -h2), (t, -h2)), ((t, h2), (t, h2)), ((-h2, t), (-h2, t)), ((h2, t), (h2, t))):
            cyl(fe, (a[0], a[1], 0.06), (b[0], b[1], H), r, r, S(lod, 5), False)
    for z in (0.08, H - 0.03, H * 0.5):
        for (a, b) in (((-h2, -h2), (h2, -h2)), ((h2, -h2), (h2, h2)), ((h2, h2), (-h2, h2)), ((-h2, h2), (-h2, -h2))):
            box(fe, (min(a[0], b[0]) - 0.015, min(a[1], b[1]) - 0.015, z - 0.02), (max(a[0], b[0]) + 0.015, max(a[1], b[1]) + 0.015, z + 0.02))
    box(fe, (-h2, -h2, H - 0.02), (h2, h2, H))
    return m.out()


def p_ext_border(spec, lod):
    """Bordure de pierres sèches basse (remplace Prop_ExteriorBorder_*, hauteur 0,13 m)."""
    m = M()
    b = spec["bbox_ref"]
    x0, x1, y0, y1 = b[0], b[1], -b[5], -b[4]
    rng = random.Random(spec["seed"])
    st = m.p("Stone_Wall")
    corner = spec["id"].endswith("Corner")
    n = {0: 9, 1: 5, 2: 2}[lod]
    for k in range(n):
        if corner:
            # deux bras en L le long des axes
            if k % 2 == 0:
                a, bb = x0 + (x1 - x0) * k / n, x0 + (x1 - x0) * (k + 1) / n
                box(st, (a, y0, 0), (bb - 0.01, y0 + 0.16, 0.1 + rng.uniform(0, 0.03)), 0.5)
            else:
                a, bb = y0 + (y1 - y0) * k / n, y0 + (y1 - y0) * (k + 1) / n
                box(st, (x1 - 0.16, a, 0), (x1, bb - 0.01, 0.1 + rng.uniform(0, 0.03)), 0.5)
        else:
            a, bb = x0 + (x1 - x0) * k / n, x0 + (x1 - x0) * (k + 1) / n
            box(st, (a, y0 + rng.uniform(0, 0.08), 0), (bb - 0.012, y1 - rng.uniform(0, 0.08), 0.1 + rng.uniform(0, 0.03)), 0.5)
    return m.out()


def p_brick(spec, lod):
    """Gravats de maçonnerie : moellon éclaté (remplace Prop_Brick1-4)."""
    m = M()
    b = spec["bbox_ref"]
    rng = random.Random(spec["seed"])
    st = m.p("Stone_Wall")
    xf = Matrix.Rotation(rng.uniform(-0.5, 0.5), 4, "Z") @ Matrix.Rotation(rng.uniform(-0.2, 0.2), 4, "X")
    box(st, (b[0] * 0.9, -b[5] * 0.9, b[2]), (b[1] * 0.9, -b[4] * 0.9, b[3] * 0.85), 0.5, xf)
    if lod == 0:
        box(st, (b[0] * 0.3, -b[5] * 0.4, b[3] * 0.5), (b[1] * 0.6, -b[4] * 0.3, b[3]), 0.5)
    return m.out()


def p_support(spec, lod):
    """Étai de chantier : poteau incliné, semelle et cale (bbox d'origine conservée)."""
    m = M()
    d = m.p("Wood_Weathered")
    # glTF : Y 1,21..2,92, Z -0,12..1,92 -> Blender Z 1,21..2,92, Y -1,92..0,12
    API["tube"](d, [Vector((0, -1.85, 1.24)), Vector((0, 0.05, 2.88))], [0.09, 0.08], S(lod, 4))
    box(d, (-0.1, -1.92, 1.21), (0.1, -1.7, 1.3))
    if lod < 2:
        box(d, (-0.1, -0.02, 2.75), (0.1, 0.12, 2.92))
    return m.out()


def p_balcony(spec, lod):
    """Garde-corps de bois à balustres (remplace Balcony_*), mêmes cotes."""
    m = M()
    b = spec["bbox_ref"]
    d = m.p("Wood_Dark")
    corner = "Corner" in spec["id"]
    cross = "Cross" in spec["id"]
    spans = [((b[0], -1.0), (b[1], -1.0))] if not corner else [((b[0], 1.0), (1.0, 1.0)), ((1.0, 1.0), (1.0, -b[4]))]
    # Blender Y du garde-corps : -Z_gltf (0,9..1,1 -> -1,1..-0,9)
    spans = [((x0, -abs(y0)), (x1, -abs(y1))) for (x0, y0), (x1, y1) in spans] if not corner else \
        [((b[0], -1.0), (1.0, -1.0)), ((1.0, -1.0), (1.0, 1.0))]
    for (x0, y0), (x1, y1) in spans:
        L = math.hypot(x1 - x0, y1 - y0)
        dx, dy = (x1 - x0) / L, (y1 - y0) / L
        nx, ny = -dy * 0.05, dx * 0.05
        box(d, (min(x0, x1) - abs(nx) - 0.02, min(y0, y1) - abs(ny) - 0.02, -0.11),
            (max(x0, x1) + abs(nx) + 0.02, max(y0, y1) + abs(ny) + 0.02, -0.02))
        box(d, (min(x0, x1) - abs(nx) - 0.02, min(y0, y1) - abs(ny) - 0.02, 1.04),
            (max(x0, x1) + abs(nx) + 0.02, max(y0, y1) + abs(ny) + 0.02, 1.12))
        if cross and lod < 2:
            for k in range(int(L / 1.0)):
                ax, ay = x0 + dx * k, y0 + dy * k
                bx, by = ax + dx, ay + dy
                API["tube"](d, [Vector((ax, ay, 0.0)), Vector((bx, by, 1.04))], [0.025, 0.025], 4)
                API["tube"](d, [Vector((ax, ay, 1.04)), Vector((bx, by, 0.0))], [0.025, 0.025], 4)
        else:
            nb = {0: int(L / 0.14), 1: int(L / 0.3), 2: int(L / 0.6)}[lod]
            for k in range(nb + 1):
                px, py = x0 + dx * L * k / max(1, nb), y0 + dy * L * k / max(1, nb)
                if lod == 0:
                    lathe_at(d, px, py, [(0.03, -0.02), (0.045, 0.25), (0.025, 0.5), (0.045, 0.8), (0.03, 1.04)], 6)
                else:
                    box(d, (px - 0.03, py - 0.03, -0.02), (px + 0.03, py + 0.03, 1.04))
    return m.out()


def lathe_at(part, x, y, profile, sides):
    i = len(part.v)
    lathe(part, profile, sides)
    for k in range(i, len(part.v)):
        part.v[k] = part.v[k] + Vector((x, y, 0))


def p_door(spec, lod):
    """Porte de planches cloutée à pentures, gond en x=0 (remplace Door_*), mêmes cotes ;
    _Round : linteau en plein cintre."""
    m = M()
    b = spec["bbox_ref"]
    W, H = b[1] - 0.02, b[3]
    T = min(0.11, (b[5] - b[4]) * 0.8)
    rnd = spec["id"].endswith("Round")
    tex = "Wood_Dark" if spec["id"].startswith(("Door_2", "Door_8")) else "Wood_Plank"
    w = m.p(tex)
    n = {0: 6, 1: 3, 2: 1}[lod]
    for k in range(n):
        x0, x1 = W * k / n + 0.004, W * (k + 1) / n - 0.004
        xc = (x0 + x1) / 2 - W / 2
        top = H if not rnd else (H - 0.22) + 0.22 * math.sqrt(max(0.0, 1 - (xc / (W / 2)) ** 2))
        box(w, (x0, -T / 2, 0.04), (x1, T / 2, top), 1.0)
    if lod < 2:
        fe = m.p("Iron")
        for z in (0.35, H - 0.5):
            box(fe, (0.0, -T / 2 - 0.012, z), (W * 0.85, -T / 2, z + 0.06))
            box(fe, (0.0, T / 2, z), (W * 0.85, T / 2 + 0.012, z + 0.06))
        if spec["id"].startswith(("Door_4", "Door_8")):
            API["tube"](fe, [Vector((0.1, -T / 2 - 0.006, 0.45)), Vector((W - 0.1, -T / 2 - 0.006, H - 0.55))], [0.015, 0.015], 4)
        cyl(fe, (W * 0.85, -T / 2 - 0.05, 1.0), (W * 0.85, -T / 2, 1.0), 0.04, 0.04, S(lod, 8))
    return m.out()


def p_door_frame(spec, lod):
    """Encadrement de porte (bois sombre ou pierre), plein cintre pour _Round, mêmes cotes."""
    m = M()
    b = spec["bbox_ref"]
    stone = "Brick" in spec["id"]
    tex = "Stone_Wall" if stone else "Wood_Dark"
    p = m.p(tex)
    hw, H = b[1], b[3]
    y0, y1 = -b[5], -b[4]
    jw = 0.22 if stone else 0.16
    rnd = "Round" in spec["id"]
    spring = H - (hw - jw) - jw if rnd else H - jw
    box(p, (-hw, y0, 0), (-hw + jw, y1, spring), 0.7)
    box(p, (hw - jw, y0, 0), (hw, y1, spring), 0.7)
    if not rnd:
        box(p, (-hw, y0, H - jw), (hw, y1, H), 0.7)
    else:
        n = S(lod, 10)
        r_in, r_out = hw - jw, hw
        for k in range(n):
            a0, a1 = math.pi * k / n, math.pi * (k + 1) / n
            pts = []
            for a, r in ((a0, r_out), (a1, r_out), (a1, r_in), (a0, r_in)):
                pts.append((math.cos(a) * r, spring + math.sin(a) * r))
            for (yy0, yy1) in ((y0, y0), (y1, y1)):
                i = len(p.v)
                p.v += [Vector((x, yy0, z)) for x, z in pts]
                p.f.append((i, i + 1, i + 2, i + 3) if yy0 == y1 else (i + 3, i + 2, i + 1, i))
                p.uv.append([(x * 0.7, z * 0.7) for x, z in pts])
                p.cn.append(None)
            for e in ((0, 1), (2, 3)):
                (xa, za), (xb, zb) = pts[e[0]], pts[e[1]]
                i = len(p.v)
                p.v += [Vector((xa, y0, za)), Vector((xb, y0, zb)), Vector((xb, y1, zb)), Vector((xa, y1, za))]
                p.f.append((i, i + 1, i + 2, i + 3) if e[0] == 0 else (i + 3, i + 2, i + 1, i))
                p.uv.append([(0, 0), (0.3, 0), (0.3, 0.3), (0, 0.3)])
                p.cn.append(None)
    return m.out()


def p_wall_woodgrid(spec, lod):
    """Mur à colombage : panneau d'enduit et pans de bois (remplace Wall_Plaster_WoodGrid)."""
    m = M()
    # glTF Z -0,31..0,09 -> Blender Y -0,09..0,31
    box(m.p("Plaster"), (-1.0, -0.05, 0), (1.0, 0.31, 3.12), 1.0)
    d = m.p("Wood_Dark")
    y0, y1 = -0.09, -0.04
    for x in (-0.94, 0.0, 0.94):
        box(d, (x - 0.08 if abs(x) < 0.9 else (x - 0.06 if x > 0 else x - 0.06), y0, 0), (x + 0.06, y1, 3.12))
    for z in (0.0, 1.5, 3.0):
        box(d, (-1.0, y0, z), (1.0, y1, z + 0.12))
    if lod < 2:
        for (xa, za, xb, zb) in ((-0.9, 0.12, -0.05, 1.5), (0.05, 1.5, 0.9, 3.0), (0.9, 0.12, 0.05, 1.5), (-0.05, 1.5, -0.9, 3.0)):
            API["tube"](d, [Vector((xa, (y0 + y1) / 2, za)), Vector((xb, (y0 + y1) / 2, zb))], [0.045, 0.045], 4)
    return m.out()


# ---------------------------------------------------------------- nouveaux objets
def p_coop(spec, lod):
    m = M()
    w = m.p("Wood_Weathered")
    plank_row(w, -1.0, 1.0, -0.75, -0.72, 0.3, 1.4, {0: 8, 1: 4, 2: 1}[lod])
    plank_row(w, -1.0, 1.0, 0.72, 0.75, 0.3, 1.25, {0: 8, 1: 4, 2: 1}[lod])
    for x in (-1.0, 0.97):
        box(w, (x, -0.75, 0.3), (x + 0.03, 0.75, 1.3))
    d = m.p("Wood_Dark")
    for x in (-0.95, 0.95):
        for y in (-0.7, 0.7):
            box(d, (x - 0.04, y - 0.04, 0), (x + 0.04, y + 0.04, 1.45))
    box(w, (-1.0, -0.75, 0.3), (1.0, 0.75, 0.34))
    xf = Matrix.Rotation(-0.1, 4, "X")
    plank_row(m.p("Wood_Plank"), -1.05, 1.05, -0.85, 0.85, 1.42, 1.47, {0: 6, 1: 3, 2: 1}[lod])
    if lod < 2:
        API["tube"](d, [Vector((0.6, -0.76, 0.32)), Vector((0.75, -1.2, 0.0))], [0.08, 0.08], 4)
        box(d, (0.45, -0.77, 0.4), (0.75, -0.75, 0.7))
    box(m.p("Straw"), (-0.95, -0.7, 0.34), (0.95, 0.7, 0.4), 0.5)
    return m.out()


def p_hut_branch(spec, lod):
    """Hutte de branchages et de peaux (horde, T1 pl.13)."""
    m = M()
    rng = random.Random(spec["seed"])
    d = m.p("Wood_Weathered")
    n = {0: 14, 1: 8, 2: 5}[lod]
    R, H = 1.75, 3.0
    for k in range(n):
        a = 2 * math.pi * k / n
        cyl(d, (math.cos(a) * R, math.sin(a) * R, -0.1), (math.cos(a + 0.2) * 0.12, math.sin(a + 0.2) * 0.12, H + rng.uniform(0, 0.3)),
            0.05, 0.03, 4, False)
    skin = m.p("Leather")
    sides = S(lod, 10)
    for k in range(sides):
        if k == 0:
            continue  # ouverture d'entrée
        a0, a1 = 2 * math.pi * k / sides, 2 * math.pi * (k + 1) / sides
        i = len(skin.v)
        skin.v += [Vector((math.cos(a0) * R * 0.98, math.sin(a0) * R * 0.98, 0)), Vector((math.cos(a1) * R * 0.98, math.sin(a1) * R * 0.98, 0)),
                   Vector((math.cos(a1) * 0.4, math.sin(a1) * 0.4, H * 0.8)), Vector((math.cos(a0) * 0.4, math.sin(a0) * 0.4, H * 0.8))]
        skin.f.append((i, i + 1, i + 2, i + 3))
        skin.uv.append([(k, 0), (k + 1, 0), (k + 1, 2), (k, 2)])
        skin.cn.append(None)
    return m.out()


def _tent(m, lod, R, H, tex, wall=0.0, poles=True):
    sides = S(lod, 14)
    t = m.p(tex)
    for k in range(sides):
        a0, a1 = 2 * math.pi * k / sides, 2 * math.pi * (k + 1) / sides
        p0, p1 = Vector((math.cos(a0) * R, math.sin(a0) * R, wall)), Vector((math.cos(a1) * R, math.sin(a1) * R, wall))
        apex = Vector((0, 0, H))
        i = len(t.v)
        t.v += [p0, p1, apex]
        t.f.append((i, i + 1, i + 2))
        t.uv.append([(k / sides * 4, 0), ((k + 1) / sides * 4, 0), ((k + 0.5) / sides * 4, 2)])
        t.cn.append(None)
        if wall > 0:
            i = len(t.v)
            t.v += [Vector((p0.x, p0.y, 0)), Vector((p1.x, p1.y, 0)), p1, p0]
            t.f.append((i, i + 1, i + 2, i + 3))
            t.uv.append([(k / sides * 4, 0), ((k + 1) / sides * 4, 0), ((k + 1) / sides * 4, wall), (k / sides * 4, wall)])
            t.cn.append(None)
    if poles:
        cyl(m.p("Wood_Dark"), (0, 0, 0), (0, 0, H + 0.4), 0.05, 0.04, S(lod, 6))


def p_tent_pavilion(spec, lod):
    m = M()
    _tent(m, lod, 2.0, 4.0, "Cloth_White", wall=1.6)
    if lod == 0:
        for k in range(8):
            a = 2 * math.pi * k / 8
            API["tube"](m.p("Rope"), [Vector((math.cos(a) * 2.0, math.sin(a) * 2.0, 1.6)), Vector((math.cos(a) * 3.0, math.sin(a) * 3.0, 0.0))], [0.01, 0.01], 3)
    return m.out()


def p_tent_striped(spec, lod):
    m = M()
    _tent(m, lod, 3.0, 5.0, "Cloth_Striped", wall=1.9)
    return m.out()


def p_tent_awning(spec, lod):
    """Tente-auvent de toile sur perches (5 x 4 m, H 2,5)."""
    m = M()
    d = m.p("Wood_Dark")
    for x in (-2.4, 0, 2.4):
        cyl(d, (x, -1.9, 0), (x, -1.9, 2.5), 0.05, 0.045, S(lod, 6))
        cyl(d, (x, 1.9, 0), (x, 1.9, 1.3), 0.05, 0.045, S(lod, 6))
    c = m.p("Canvas")
    i = len(c.v)
    c.v += [Vector((-2.5, -2.0, 2.5)), Vector((2.5, -2.0, 2.5)), Vector((2.5, 2.0, 1.3)), Vector((-2.5, 2.0, 1.3))]
    c.f.append((i, i + 1, i + 2, i + 3))
    c.uv.append([(0, 0), (3, 0), (3, 3), (0, 3)])
    c.cn.append(None)
    _cloth(c, -2.5, 2.5, 1.3, 0.6, lod, y=2.0, pointed=False)
    if lod < 2:
        box(m.p("Fur"), (-1.2, -0.8, 0), (0.6, 0.8, 0.08))
    return m.out()


def p_trough(spec, lod):
    m = M()
    st = m.p("Stone_Wall")
    box(st, (-1.0, -0.4, 0), (1.0, 0.4, 0.12), 0.5)
    box(st, (-1.0, -0.4, 0.12), (1.0, -0.3, 0.7), 0.5)
    box(st, (-1.0, 0.3, 0.12), (1.0, 0.4, 0.7), 0.5)
    box(st, (-1.0, -0.3, 0.12), (-0.9, 0.3, 0.7), 0.5)
    box(st, (0.9, -0.3, 0.12), (1.0, 0.3, 0.7), 0.5)
    return m.out()


def p_campfire_spit(spec, lod):
    m = M()
    rng = random.Random(spec["seed"])
    st = m.p("Rock_Grey")
    for k in range({0: 10, 1: 6, 2: 4}[lod]):
        a = 2 * math.pi * k / 10
        box(st, (math.cos(a) * 0.5 - 0.08, math.sin(a) * 0.5 - 0.08, 0), (math.cos(a) * 0.5 + 0.08, math.sin(a) * 0.5 + 0.08, 0.14), 1.0,
            Matrix.Rotation(a, 4, "Z") @ Matrix.Translation((0, 0, 0)))
    d = m.p("Wood_Dark")
    for k in range(5):
        a = 2 * math.pi * k / 5
        API["tube"](d, [Vector((math.cos(a) * 0.35, math.sin(a) * 0.35, 0.03)), Vector((0, 0, 0.32))], [0.04, 0.03], 5)
    for x in (-0.6, 0.6):
        API["tube"](d, [Vector((x, 0, 0)), Vector((x, 0, 1.15)), Vector((x - 0.1, 0, 1.3))], [0.03, 0.03, 0.02], 5)
        API["tube"](d, [Vector((x, 0, 1.15)), Vector((x + 0.1, 0, 1.3))], [0.025, 0.02], 4)
    cyl(d, (-0.7, 0, 1.17), (0.7, 0, 1.17), 0.018, 0.018, 5)
    return m.out()


def p_brazier_pole(spec, lod):
    """Brasero-torche sur perche : fagot lié au sommet (horde, T1 pl.11)."""
    m = M()
    d = m.p("Wood_Weathered")
    cyl(d, (0, 0, -0.2), (0, 0, 2.6), 0.08, 0.06, S(lod, 8))
    for k in range(3):
        a = 2 * math.pi * k / 3
        API["tube"](d, [Vector((math.cos(a) * 0.6, math.sin(a) * 0.6, 0)), Vector((0, 0, 0.8))], [0.05, 0.04], 4)
    br = m.p("Straw")
    cyl(br, (0, 0, 2.5), (0, 0, 3.2), 0.22, 0.32, S(lod, 10))
    if lod < 2:
        for z in (2.6, 2.95):
            cyl(m.p("Rope"), (0, 0, z), (0, 0, z + 0.05), 0.25 + (z - 2.5) * 0.12, 0.25 + (z - 2.5) * 0.12, S(lod, 10), False)
    return m.out()


def p_pike_stake(spec, lod):
    m = M()
    cyl(m.p("Wood_Weathered"), (0, 0, -0.2), (0.05, 0.02, 2.6), 0.045, 0.03, S(lod, 6))
    cyl(m.p("Iron"), (0.05, 0.02, 2.6), (0.055, 0.02, 2.85), 0.03, 0.002, S(lod, 6))
    return m.out()


def p_debris_wheel(spec, lod):
    """Roue brisée et planches de chariot au sol."""
    m = M()
    xf = Matrix.Translation((0, 0, 0.06)) @ Matrix.Rotation(math.pi / 2, 4, "Y")
    tmp = M()
    wheel(tmp, (0, 0, 0), 0.7, 0.09, lod)
    for p in tmp.out():
        _merge(m, p, xf)
    w = m.p("Wood_Weathered")
    rng = random.Random(spec["seed"])
    for k in range({0: 4, 1: 2, 2: 0}[lod]):
        box(w, (-0.6, -0.08, 0), (0.6, 0.08, 0.04), 1.0,
            Matrix.Translation((rng.uniform(-0.4, 0.4), rng.uniform(0.6, 1.0), 0)) @ Matrix.Rotation(rng.uniform(0, 3), 4, "Z"))
    return m.out()


def p_palisade_stake(spec, lod):
    """Palissade de camp : pieux effilés liés (module 2 m, H 2,5)."""
    m = M()
    rng = random.Random(spec["seed"])
    d = m.p("Wood_Weathered")
    n = {0: 10, 1: 6, 2: 3}[lod]
    for k in range(n):
        x = -1.0 + 2.0 * (k + 0.5) / n
        h = 2.3 + rng.uniform(-0.1, 0.2)
        r = 2.0 / n / 2 * 0.95
        cyl(d, (x, 0, -0.3), (x, 0, h - 0.3), r, r * 0.95, S(lod, 6), False)
        cyl(d, (x, 0, h - 0.3), (x, 0, h), r * 0.95, 0.01, S(lod, 6), False)
    if lod < 2:
        for z in (0.5, 1.7):
            box(d, (-1.0, 0.1, z), (1.0, 0.18, z + 0.1))
    return m.out()


def p_palisade_rampart(spec, lod):
    """Rempart de rondins effilés avec chemin de ronde (module 4 m, H 9 m)."""
    m = M()
    rng = random.Random(spec["seed"])
    d = m.p("Wood_Weathered")
    n = {0: 12, 1: 7, 2: 4}[lod]
    for k in range(n):
        x = -2.0 + 4.0 * (k + 0.5) / n
        h = 8.0 + rng.uniform(-0.2, 0.3)
        r = 4.0 / n / 2 * 0.97
        cyl(d, (x, 0, -0.3), (x, 0, h), r, r * 0.92, S(lod, 8), False)
        cyl(d, (x, 0, h), (x, 0, h + 0.9), r * 0.92, 0.02, S(lod, 8), False)
    w = m.p("Wood_Plank")
    plank_row(w, -2.0, 2.0, 0.4, 1.9, 6.6, 6.7, {0: 8, 1: 4, 2: 1}[lod])
    for x in (-1.6, 0.0, 1.6):
        box(m.p("Wood_Dark"), (x - 0.1, 1.6, 0), (x + 0.1, 1.8, 6.6))
        if lod < 2:
            API["tube"](m.p("Wood_Dark"), [Vector((x, 1.7, 0)), Vector((x, 0.6, 6.55))], [0.08, 0.08], 4)
    return m.out()


def p_door_palace(spec, lod):
    """Porte monumentale double à panneaux (4 x 6 m), motifs géométriques neutres."""
    m = M()
    w = m.p("Wood_Dark")
    for side in (-1, 1):
        x0, x1 = (0.02, 1.98) if side > 0 else (-1.98, -0.02)
        box(w, (x0, -0.08, 0), (x1, 0.08, 6.0))
        if lod < 2:
            for r in range(4):
                for c in range(2):
                    px0 = x0 + 0.18 + c * (x1 - x0 - 0.36) / 2 + 0.05
                    px1 = px0 + (x1 - x0 - 0.36) / 2 - 0.1
                    pz0 = 0.3 + r * 1.4
                    box(m.p("Wood_Plank"), (px0, -0.12, pz0), (px1, -0.08, pz0 + 1.2))
        fe = m.p("Iron")
        cyl(fe, ((x0 + x1) / 2 + side * -0.7, -0.2, 2.9), ((x0 + x1) / 2 + side * -0.7, -0.08, 2.9), 0.12, 0.12, S(lod, 12))
    return m.out()


def p_throne_wood(spec, lod):
    """Trône de bois à dossier haut ajouré (H 2,2, assise 0,5)."""
    m = M()
    d = m.p("Wood_Dark")
    box(d, (-0.45, -0.4, 0), (0.45, 0.4, 0.5))
    box(m.p("Cloth_Red"), (-0.4, -0.36, 0.5), (0.4, 0.36, 0.56))
    for x in (-0.42, 0.42):
        box(d, (x - 0.06, 0.28, 0.5), (x + 0.06, 0.4, 2.2))
        box(d, (x - 0.06, -0.4, 0.5), (x + 0.06, 0.4, 0.85))
    box(d, (-0.48, 0.28, 2.0), (0.48, 0.4, 2.12))
    if lod < 2:
        for k in range(5):
            x = -0.28 + k * 0.14
            box(d, (x - 0.025, 0.3, 0.56), (x + 0.025, 0.36, 2.0))
    return m.out()


def p_trophy_skull(spec, lod):
    """Trophée mural : écu de bois et crâne de bête à cornes (fond au mur, y=0)."""
    m = M()
    box(m.p("Wood_Dark"), (-0.25, -0.04, -0.3), (0.25, 0.0, 0.3))
    bone = m.p("Bone")
    lathe_at(bone, 0, 0, [(0.001, 0.0), (0.1, 0.03), (0.13, 0.15), (0.09, 0.28), (0.001, 0.3)], S(lod, 10))
    for p in (bone,):
        pass
    for s in (-1, 1):
        API["tube"](bone, [Vector((s * 0.1, -0.1, 0.18)), Vector((s * 0.3, -0.15, 0.28)), Vector((s * 0.4, -0.1, 0.45))],
                    [0.04, 0.03, 0.008], S(lod, 6))
    xf = Matrix.Translation((0, -0.12, -0.15))
    bone.v = [xf @ v for v in bone.v]
    for p in m.out():  # base au sol (accroché au mur à 1,8 m par le générateur)
        p.v = [v + Vector((0, 0, 0.3)) for v in p.v]
    return m.out()


def p_fireplace_arch(spec, lod):
    """Âtre en arc de pierre (2 x 1,8 m), manteau et foyer."""
    m = M()
    st = m.p("Stone_Wall")
    box(st, (-1.0, 0.0, 0), (-0.65, 0.5, 1.3), 0.7)
    box(st, (0.65, 0.0, 0), (1.0, 0.5, 1.3), 0.7)
    box(st, (-1.0, 0.0, 1.3), (1.0, 0.5, 1.8), 0.7)
    box(st, (-0.65, 0.4, 0), (0.65, 0.5, 1.3), 0.7)
    box(m.p("Wood_Dark"), (-1.1, -0.05, 1.45), (1.1, 0.55, 1.55))
    if lod < 2:
        for k in range(3):
            API["tube"](m.p("Wood_Dark"), [Vector((-0.35 + k * 0.1, 0.1 + k * 0.05, 0.08)), Vector((0.35 - k * 0.05, 0.25, 0.1))], [0.06, 0.06], 5)
    return m.out()


def p_carpet_runner(spec, lod):
    m = M()
    box(m.p("Cloth_Red"), (-1.0, -6.0, 0.0), (1.0, 6.0, 0.015), 0.5)
    if lod < 2:
        for x in (-0.95, 0.85):
            box(m.p("Cloth_Ochre"), (x, -6.0, 0.015), (x + 0.1, 6.0, 0.02), 0.5)
    return m.out()


def p_alembic(spec, lod):
    m = M()
    lathe(m.p("Brass"), [(0.001, 0), (0.14, 0.04), (0.16, 0.18), (0.08, 0.32), (0.03, 0.4)], S(lod, 12))
    API["tube"](m.p("Brass"), [Vector((0, 0, 0.4)), Vector((0.15, 0, 0.5)), Vector((0.35, 0, 0.3))], [0.02, 0.015, 0.012], S(lod, 6))
    lathe_at(m.p("Glass_Clear"), 0.42, 0, [(0.001, 0), (0.08, 0.04), (0.09, 0.14), (0.03, 0.24), (0.025, 0.3)], S(lod, 10))
    for k in range(3):
        a = 2 * math.pi * k / 3
        cyl(m.p("Iron"), (math.cos(a) * 0.13, math.sin(a) * 0.13, -0.15), (math.cos(a) * 0.12, math.sin(a) * 0.12, 0.05), 0.01, 0.01, 4, False)
    for p in m.out():
        p.v = [v + Vector((0, 0, 0.15)) for v in p.v]
    return m.out()


def p_crystal_ball(spec, lod):
    m = M()
    d = m.p("Wood_Dark")
    lathe(d, [(0.2, 0), (0.18, 0.05), (0.05, 0.15), (0.04, 0.8), (0.08, 0.9), (0.12, 0.95)], S(lod, 12))
    g = m.p("Glass_Clear")
    n = S(lod, 12)
    prof = [(0.15 * math.sin(math.pi * k / n) + 0.001, 0.95 + 0.15 - 0.15 * math.cos(math.pi * k / n)) for k in range(n + 1)]
    lathe(g, prof, n)
    return m.out()


def p_drinking_horn(spec, lod):
    m = M()
    API["tube"](m.p("Bone"), [Vector((0, 0, 0.02)), Vector((0.12, 0, 0.04)), Vector((0.26, 0, 0.12)), Vector((0.32, 0, 0.24))],
                [0.008, 0.025, 0.04, 0.05], S(lod, 10))
    if lod < 2:
        cyl(m.p("Brass"), (0.31, 0, 0.22), (0.33, 0, 0.26), 0.052, 0.052, S(lod, 10), False)
    return m.out()


# ===================================================================== militaire
def p_siege_tower(spec, lod):
    """Tour de siège en bois effilée (base 5 x 5, H 16 m), roues, pont-levis."""
    m = M()
    d = m.p("Wood_Dark")
    w = m.p("Wood_Plank")
    H = 15.5
    for (x, y) in ((-2.4, -2.4), (2.4, -2.4), (2.4, 2.4), (-2.4, 2.4)):
        API["tube"](d, [Vector((x, y, 0.5)), Vector((x * 0.62, y * 0.62, H))], [0.18, 0.14], 4)
    for lvl in range(5 if lod < 2 else 2):
        z = 0.6 + lvl * (H - 1) / (5 if lod < 2 else 2)
        s = 2.45 - (2.45 - 1.55) * z / H
        for side in range(4):
            xf = Matrix.Rotation(side * math.pi / 2, 4, "Z")
            box(d, (-s, -s - 0.1, z), (s, -s + 0.1, z + 0.2), 1.0, xf)
    for side in range(4):
        xf = Matrix.Rotation(side * math.pi / 2, 4, "Z")
        n = {0: 12, 1: 6, 2: 2}[lod]
        for k in range(n):
            t0, t1 = k / n, (k + 1) / n
            z0, z1 = 0.8 + t0 * (H - 0.8), 0.8 + t1 * (H - 0.8)
            s0, s1 = 2.45 - 0.9 * z0 / H, 2.45 - 0.9 * z1 / H
            i = len(w.v)
            w.v += [xf @ Vector((-s0, -s0, z0)), xf @ Vector((s0, -s0, z0)), xf @ Vector((s1, -s1, z1)), xf @ Vector((-s1, -s1, z1))]
            w.f.append((i, i + 1, i + 2, i + 3))
            w.uv.append([(0, z0 / 2), (s0, z0 / 2), (s1, z1 / 2), (0, z1 / 2)])
            w.cn.append(None)
    box(d, (-1.6, -1.6, H), (1.6, 1.6, H + 0.15))
    if lod < 2:
        for x in (-1.5, 1.5):
            box(d, (x - 0.06, -1.6, H), (x + 0.06, 1.6, H + 1.0))
        plank_row(w, -1.2, 1.2, -1.75, -1.65, H - 3.2, H, 6)
    for (x, y) in ((-2.5, -1.6), (-2.5, 1.6), (2.5, -1.6), (2.5, 1.6)):
        wheel(m, (x, y, 0.55), 0.55, 0.2, lod)
    return m.out()


def p_catapult(spec, lod):
    """Mangonneau : bâti de poutres, bras et cuiller (6 x 3 m)."""
    m = M()
    d = m.p("Wood_Dark")
    for y in (-1.2, 1.2):
        box(d, (-3.0, y - 0.15, 0.3), (3.0, y + 0.15, 0.6))
        API["tube"](d, [Vector((-1.0, y, 0.6)), Vector((0.0, y, 2.4))], [0.12, 0.1], 4)
        API["tube"](d, [Vector((1.0, y, 0.6)), Vector((0.0, y, 2.4))], [0.12, 0.1], 4)
    for x in (-2.6, 0.0, 2.6):
        box(d, (x - 0.12, -1.35, 0.3), (x + 0.12, 1.35, 0.55))
    box(d, (-0.15, -1.3, 2.3), (0.15, 1.3, 2.55))
    API["tube"](d, [Vector((-2.8, 0, 0.9)), Vector((0.0, 0, 1.4)), Vector((2.2, 0, 3.2))], [0.15, 0.14, 0.09], S(lod, 6))
    lathe_at(m.p("Wood_Plank"), -2.9, 0, [(0.001, 0.8), (0.35, 0.85), (0.4, 1.05)], S(lod, 10))
    if lod < 2:
        API["tube"](m.p("Rope"), [Vector((0.0, -1.2, 0.6)), Vector((0.0, 0, 1.35)), Vector((0.0, 1.2, 0.6))], [0.05, 0.08, 0.05], 6)
    for (x, y) in ((-2.4, -1.45), (-2.4, 1.45), (2.4, -1.45), (2.4, 1.45)):
        wheel(m, (x, y, 0.45), 0.45, 0.15, lod, axis="y")
    return m.out()


def p_mantlet(spec, lod):
    """Mantelet : pavois de planches incliné sur béquille (2,5 x 2 m)."""
    m = M()
    xf = Matrix.Rotation(-0.25, 4, "X")
    w = m.p("Wood_Plank")
    n = {0: 9, 1: 5, 2: 1}[lod]
    for k in range(n):
        x0, x1 = -1.25 + 2.5 * k / n + 0.01, -1.25 + 2.5 * (k + 1) / n - 0.01
        box(w, (x0, -0.06, 0.0), (x1, 0.06, 2.0), 1.0, xf)
    d = m.p("Wood_Dark")
    for z in (0.4, 1.6):
        box(d, (-1.25, 0.06, z), (1.25, 0.14, z + 0.12), 1.0, xf)
    for x in (-0.8, 0.8):
        API["tube"](d, [Vector((x, 1.2, 0.0)), Vector((x, 0.35, 1.55))], [0.05, 0.05], 4)
    return m.out()


def p_siege_ladder(spec, lod):
    """Échelle d'assaut de 10 m, posée à plat au sol."""
    m = M()
    d = m.p("Wood_Weathered")
    for x in (-0.3, 0.3):
        cyl(d, (x, -5.0, 0.07), (x * 0.85, 5.0, 0.07), 0.07, 0.06, S(lod, 6))
    for k in range({0: 28, 1: 14, 2: 5}[lod]):
        y = -4.8 + 9.6 * k / max(1, {0: 27, 1: 13, 2: 4}[lod])
        cyl(d, (-0.32, y, 0.07), (0.32, y, 0.07), 0.025, 0.025, S(lod, 5), False)
    return m.out()


def p_portcullis(spec, lod):
    """Herse de fer et bois à pointes (4 x 5 m), chaînes."""
    m = M()
    fe = m.p("Iron")
    n = {0: 9, 1: 5, 2: 3}[lod]
    for k in range(n):
        x = -1.9 + 3.8 * k / (n - 1)
        box(fe, (x - 0.05, -0.05, 0.25), (x + 0.05, 0.05, 5.0))
        cyl(fe, (x, 0, 0.25), (x, 0, 0.0), 0.05, 0.002, 4, False)
    for k in range({0: 7, 1: 4, 2: 2}[lod]):
        z = 0.6 + 4.2 * k / max(1, {0: 6, 1: 3, 2: 1}[lod])
        box(m.p("Wood_Dark"), (-2.0, 0.05, z), (2.0, 0.15, z + 0.14))
    if lod < 2:
        for x in (-1.5, 1.5):
            for k in range(8):
                cyl(fe, (x, 0, 5.0 + k * 0.12), (x, 0, 5.1 + k * 0.12), 0.03, 0.03, 5, False)
    return m.out()


def p_window_great(spec, lod):
    """Grande verrière à petits carreaux et meneaux (3 x 6 m), arc en ogive."""
    m = M()
    st = m.p("Stone_Wall")
    box(st, (-1.5, -0.15, 0), (-1.25, 0.15, 4.4), 0.7)
    box(st, (1.25, -0.15, 0), (1.5, 0.15, 4.4), 0.7)
    box(st, (-1.5, -0.15, 0), (1.5, 0.15, 0.25), 0.7)
    n = S(lod, 10)
    for side in (-1, 1):
        for k in range(n):
            a0, a1 = math.pi / 2 * k / n, math.pi / 2 * (k + 1) / n
            R = 2.5
            cx = -side * 1.0
            p0 = Vector((cx + side * R * math.cos(a0), 0, 4.4 + R * math.sin(a0) * 0.64))
            p1 = Vector((cx + side * R * math.cos(a1), 0, 4.4 + R * math.sin(a1) * 0.64))
            API["tube"](st, [p0, p1], [0.15, 0.15], 4)
    gl = m.p("Glass_Clear")
    box(gl, (-1.25, -0.02, 0.25), (1.25, 0.02, 4.4), 1.0)
    fe = m.p("Iron")
    if lod < 2:
        for x in (-0.42, 0.42):
            box(st, (x - 0.06, -0.08, 0.25), (x + 0.06, 0.08, 4.4), 0.7)
        step = 0.3 if lod == 0 else 0.6
        z = 0.25
        while z < 4.4:
            box(fe, (-1.25, -0.03, z), (1.25, 0.03, z + 0.015))
            z += step
        x = -1.25
        while x < 1.25:
            box(fe, (x, -0.03, 0.25), (x + 0.015, 0.03, 4.4))
            x += step
    return m.out()


def p_statue_niche(spec, lod):
    """Statue en niche : figure drapée neutre (sans visage identifiable), socle, niche."""
    m = M()
    st = m.p("Stone_Wall")
    box(st, (-0.8, 0.3, 0), (0.8, 0.6, 3.2), 0.7)
    box(st, (-0.8, -0.1, 0), (-0.6, 0.6, 3.2), 0.7)
    box(st, (0.6, -0.1, 0), (0.8, 0.6, 3.2), 0.7)
    box(st, (-0.8, -0.1, 2.95), (0.8, 0.6, 3.2), 0.7)
    sc = m.p("Rock_Grey")
    box(sc, (-0.45, -0.1, 0), (0.45, 0.3, 0.45), 1.0)
    n = S(lod, 12)
    lathe_at(sc, 0, 0.1, [(0.3, 0.45), (0.32, 0.9), (0.24, 1.6), (0.2, 2.05), (0.24, 2.15), (0.11, 2.25), (0.13, 2.45), (0.001, 2.55)], n)
    if lod < 2:
        API["tube"](sc, [Vector((-0.22, 0.05, 2.0)), Vector((-0.28, -0.05, 1.5)), Vector((-0.1, -0.12, 1.3))], [0.07, 0.06, 0.05], 6)
        API["tube"](sc, [Vector((0.22, 0.05, 2.0)), Vector((0.3, 0.0, 1.6)), Vector((0.25, -0.05, 1.2))], [0.07, 0.06, 0.05], 6)
    return m.out()


# ================================================================ enseignes
# Panneau de service sur potence (atelier décors, décision du 22/11/2026) : un
# pictogramme peint par service, sans texte ; le nom du service s'affiche dans la
# langue du joueur au survol ou au clic (client). Case de l'atlas = spec["picto"]
# (ordre SIGN_SERVICES de decor_textures.py, 8 x 4 cases).
def _board(part, x0, x1, z0, z1, t, tile, cols=8, rows=4):
    r, c = divmod(tile, cols)
    u0, u1 = c / cols + 0.004, (c + 1) / cols - 0.004
    v1, v0 = 1 - r / rows - 0.004, 1 - (r + 1) / rows + 0.004
    y0, y1 = -t / 2, t / 2
    quads = [  # face avant (-Y) puis arrière (+Y, U inversé pour lire le pictogramme à l'endroit)
        ([(x0, y0, z0), (x1, y0, z0), (x1, y0, z1), (x0, y0, z1)], [(u0, v0), (u1, v0), (u1, v1), (u0, v1)]),
        ([(x1, y1, z0), (x0, y1, z0), (x0, y1, z1), (x1, y1, z1)], [(u0, v0), (u1, v0), (u1, v1), (u0, v1)]),
    ]
    for pts, uv in quads:
        i = len(part.v)
        part.v += [Vector(q) for q in pts]
        part.f.append((i, i + 1, i + 2, i + 3))
        part.uv.append(uv)
        part.cn.append(None)
    part.smooth = False


def p_sign_service(spec, lod):
    m = M()
    wood, iron = m.p("Wood_Dark"), m.p("Iron")
    H, arm = 2.6, 0.95
    box(wood, (-0.06, -0.06, 0.0), (0.06, 0.06, H))                   # poteau
    box(wood, (0.06, -0.04, H - 0.30), (arm, 0.04, H - 0.22))          # potence
    if lod < 2:
        a, b = Vector((0.06, 0, H - 0.75)), Vector((0.55, 0, H - 0.27))
        cyl(wood, a, b, 0.03, 0.03, 4, False)                          # jambe de force
        box(wood, (-0.08, -0.08, H), (0.08, 0.08, H + 0.04))           # chapeau
    bx0, bx1, bz1 = 0.22, 0.86, H - 0.42
    bz0 = bz1 - 0.64
    for x in (bx0 + 0.06, bx1 - 0.06):                                 # tiges de suspension
        cyl(iron, (x, 0, bz1), (x, 0, H - 0.30), 0.008, 0.008, S(lod, 6), False)
    box(wood, (bx0, -0.018, bz0), (bx1, 0.018, bz1))                   # chant du panneau
    _board(m.p("Sign_Pictos"), bx0, bx1, bz0, bz1, 0.04, int(spec["picto"]))
    if lod == 0:
        box(iron, (bx0 - 0.01, -0.024, bz1 - 0.02), (bx1 + 0.01, 0.024, bz1 + 0.01))
    return m.out()


def register(api):
    API.update(api)
    out = {}
    for k, v in dict(globals()).items():
        if k.startswith("p_") and callable(v):
            out[k[2:]] = v
    return out
