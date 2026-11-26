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


# ====================================================================== T2
# Atelier décors, tome 2 (n° 76-110). Repère Blender, 1 unité = 1 m.
def _jitter(part, i0, amp, seed):
    """Bosselle les sommets [i0:] (roche) par un bruit déterministe lié à la position."""
    for k in range(i0, len(part.v)):
        v = part.v[k]
        h = math.sin(v.x * 3.1 + seed) * math.cos(v.y * 2.7 - seed) + math.sin(v.z * 2.3 + seed * 0.7)
        h2 = math.sin(v.x * 7.9 - v.z * 6.1 + seed) * 0.4
        part.v[k] = v + Vector((h * 0.6, (h + h2) * 0.5, h2)) * amp


def p_rock_arch(spec, lod):
    """Arche rocheuse naturelle (n° 78) : ouverture ~6 x 8 m, piles massives."""
    m = M()
    part = m.p("Rock_Grey")
    n = {0: 22, 1: 14, 2: 8}[lod]
    pts, radii = [], []
    for k in range(n + 1):
        t = k / n
        a = math.pi * (1 - t)
        x = 4.6 * math.cos(a)
        z = -0.4 + 8.4 * math.sin(a) ** 0.8
        pts.append(Vector((x, 0, z)))
        radii.append(1.55 - 0.55 * math.sin(math.pi * t))
    i0 = len(part.v)
    API["tube"](part, pts, radii, S(lod, 14), 0.5)
    _jitter(part, i0, 0.32, spec["seed"] % 17)
    for x in (-4.6, 4.6):  # éboulis au pied
        i1 = len(part.v)
        cyl(part, (x, 0, -0.3), (x * 1.02, 0, 0.9), 2.0, 1.5, S(lod, 10))
        _jitter(part, i1, 0.25, spec["seed"] % 11 + x)
    for k in range(i0, len(part.v)):   # rien sous -0,3 m (ancrage au sol)
        if part.v[k].z < -0.3:
            part.v[k] = Vector((part.v[k].x, part.v[k].y, -0.3))
    return m.out()


def p_signpost(spec, lod):
    """Poteau indicateur à flèches de bois, sans inscription (n° 84)."""
    m = M()
    d, w = m.p("Wood_Dark"), m.p("Wood_Weathered")
    box(d, (-0.06, -0.06, 0.0), (0.06, 0.06, 2.5))
    if lod < 2:
        box(d, (-0.08, -0.08, 2.5), (0.08, 0.08, 2.56))
    for k, (z, ang) in enumerate(((2.25, 15), (1.95, 125), (1.65, 250))[:{0: 3, 1: 3, 2: 2}[lod]]):
        xf = Matrix.Rotation(math.radians(ang), 4, "Z")
        L = 0.85 if k != 1 else 0.7
        p = w
        i = len(p.v)
        box(p, (0.05, -0.02, z - 0.1), (L, 0.02, z + 0.1))
        tip = [Vector((L, -0.02, z - 0.13)), Vector((L + 0.18, -0.02, z)), Vector((L, -0.02, z + 0.13)),
               Vector((L, 0.02, z - 0.13)), Vector((L + 0.18, 0.02, z)), Vector((L, 0.02, z + 0.13))]
        j = len(p.v)
        p.v += tip
        for f in ((0, 1, 2), (5, 4, 3), (0, 3, 4, 1), (1, 4, 5, 2)):
            p.f.append(tuple(j + q for q in f))
            p.uv.append([(0, 0), (1, 0), (1, 1), (0, 1)][:len(f)])
            p.cn.append(None)
        for q in range(i, len(p.v)):
            p.v[q] = xf @ p.v[q]
    return m.out()


def p_gallows(spec, lod):
    """Gibet de bois sans pendu (n° 85) : estrade, montant 4 m, potence 2 m, jambe de force."""
    m = M()
    d, w = m.p("Wood_Dark"), m.p("Wood_Plank")
    plank_row(w, -1.3, 1.3, -1.3, 1.3, 0.55, 0.65, {0: 8, 1: 4, 2: 1}[lod], axis="x")
    for x in (-1.2, 1.2):
        for y in (-1.2, 1.2):
            box(d, (x - 0.08, y - 0.08, 0), (x + 0.08, y + 0.08, 0.55))
    box(d, (-1.0, -0.11, 0.65), (-0.78, 0.11, 4.4))
    box(d, (-1.0, -0.1, 4.2), (1.1, 0.1, 4.4))
    if lod < 2:
        cyl(d, (-0.9, 0, 3.3), (-0.1, 0, 4.22), 0.06, 0.06, 4, False)
        for k in range(4):  # marches
            z = 0.14 * (k + 1)
            box(w, (1.3 + 0.28 * (3 - k), -0.45, 0), (1.3 + 0.28 * (4 - k), 0.45, z))
    if lod == 0:
        cyl(m.p("Rope"), (0.85, 0, 4.2), (0.85, 0, 3.1), 0.018, 0.018, 5, False)
        cyl(m.p("Rope"), (0.85, 0, 3.1), (0.85, 0, 3.1), 0.12, 0.12, 8)
    return m.out()


def p_fountain(spec, lod):
    """Fontaine à vasque (n° 86) : bassin octogonal Ø 2,5 m, fût, vasque haute, eau."""
    m = M()
    s = m.p("Stone_Carved")
    sides = {0: 8, 1: 8, 2: 8}[lod]
    lathe(s, [(1.25, 0.0), (1.25, 0.55), (1.1, 0.6), (1.1, 0.12)], sides)
    cyl(s, (0, 0, 0.12), (0, 0, 0.12), 1.1, 1.1, sides)
    lathe(s, [(0.22, 0.1), (0.16, 0.5), (0.16, 1.0), (0.12, 1.15)], S(lod, 12))
    lathe(s, [(0.12, 1.12), (0.55, 1.3), (0.62, 1.42), (0.5, 1.42), (0.45, 1.32)], S(lod, 16))
    lathe(s, [(0.08, 1.3), (0.1, 1.55), (0.05, 1.7), (0.09, 1.78), (0.001, 1.85)], S(lod, 10))
    wtr = m.p("Water_Still")
    cyl(wtr, (0, 0, 0.45), (0, 0, 0.45), 1.12, 1.12, sides)
    cyl(wtr, (0, 0, 1.38), (0, 0, 1.38), 0.48, 0.48, S(lod, 16))
    return m.out()


def p_awning_striped(spec, lod):
    """Auvent rayé de marché sur quatre perches (n° 88), 3 x 2 m."""
    m = M()
    d = m.p("Wood_Dark")
    for x in (-1.45, 1.45):
        box(d, (x - 0.04, 0.9, 0), (x + 0.04, 0.98, 2.6))
        box(d, (x - 0.04, -1.0, 0), (x + 0.04, -0.92, 2.1))
    c = m.p("Cloth_Striped")
    nx = {0: 8, 1: 4, 2: 2}[lod]
    i = len(c.v)
    for j in range(2):
        for k in range(nx + 1):
            x = -1.55 + 3.1 * k / nx
            y = 1.0 if j == 0 else -1.05
            z = 2.62 if j == 0 else 2.12
            c.v.append(Vector((x, y, z - (0.06 * math.sin(math.pi * k / nx) if j else 0))))
    for k in range(nx):
        a, b = i + k, i + k + 1
        c.f.append((a, b, b + nx + 1, a + nx + 1))
        c.uv.append([(k / nx, 1), ((k + 1) / nx, 1), ((k + 1) / nx, 0), (k / nx, 0)])
        c.cn.append(None)
    if lod < 2:  # lambrequin festonné
        for k in range(nx):
            x0, x1 = -1.55 + 3.1 * k / nx, -1.55 + 3.1 * (k + 1) / nx
            j = len(c.v)
            c.v += [Vector((x0, -1.05, 2.12)), Vector((x1, -1.05, 2.12)), Vector(((x0 + x1) / 2, -1.06, 1.88))]
            c.f.append((j, j + 1, j + 2))
            c.uv.append([(0, 1), (1, 1), (0.5, 0)])
            c.cn.append(None)
    return m.out()


def p_bonfire_stack(spec, lod):
    """Bûcher de rondins empilés en tour carrée qui s'affine (n° 89), H 4 m, base 2 m."""
    m = M()
    w = m.p("Bark_Broad")
    layers = {0: 18, 1: 12, 2: 6}[lod]
    rng = random.Random(spec["seed"])
    for k in range(layers):
        t = k / max(1, layers - 1)
        half = 1.0 - 0.55 * t
        z = 0.12 + k * (3.75 / layers)
        r = 0.12 * (1 - 0.3 * t) * (18 / layers) ** 0.3
        for s in (-1, 1):
            if k % 2 == 0:
                cyl(w, (-half - 0.15, s * half, z), (half + 0.15, s * half + rng.uniform(-0.04, 0.04), z), r, r, S(lod, 7))
            else:
                cyl(w, (s * half, -half - 0.15, z), (s * half + rng.uniform(-0.04, 0.04), half + 0.15, z), r, r, S(lod, 7))
    if lod < 2:
        g = m.p("Straw")
        lathe(g, [(0.7, 0), (0.6, 0.6), (0.3, 1.2), (0.05, 1.4)], S(lod, 8))
    return m.out()


def p_skull_beast(spec, lod):
    """Crâne de grande bête à cornes de bélier, à demi enfoui (n° 90), ~3 x 2 m."""
    m = M()
    b = m.p("Bone")
    i0 = len(b.v)
    lathe(b, [(0.75, -0.05), (0.85, 0.3), (0.72, 0.72), (0.38, 0.98), (0.001, 1.04)], S(lod, 14))
    for k in range(i0, len(b.v)):
        v = b.v[k]
        b.v[k] = Vector((v.x - 0.5, v.y * 0.88, v.z))
    i1 = len(b.v)
    pts = [Vector((-0.2 + 1.8 * t, 0, 0.5 - 0.32 * t)) for t in [k / 6 for k in range(7)]]
    API["tube"](b, pts, [0.55 * (1 - 0.6 * t) for t in [k / 6 for k in range(7)]], S(lod, 12), 0.5, True)
    for k in range(i1, len(b.v)):
        v = b.v[k]
        b.v[k] = Vector((v.x, v.y, max(0.02, 0.5 - 0.32 * (v.x + 0.2) / 1.8 + (v.z - (0.5 - 0.32 * (v.x + 0.2) / 1.8)) * 0.7)))
    _jitter(b, i0, 0.025, 3)
    if lod < 2:  # orbites
        for y in (-1, 1):
            cyl(m.p("Wood_Dark"), (0.15, y * 0.36, 0.66), (0.15, y * 0.46, 0.64), 0.15, 0.1, S(lod, 8))
    for sgn in (-1, 1):  # cornes de bélier enroulées
        n = 12
        hp, hr = [], []
        for k in range(n + 1):
            t = k / n
            a = t * 1.6 * math.pi
            R = 0.6 * (1 - 0.45 * t)
            hp.append(Vector((-0.45 - R * math.sin(a), sgn * (0.6 + 0.55 * t), 0.4 + R * math.cos(a))))
            hr.append(0.24 * (1 - 0.8 * t) + 0.02)
        API["tube"](b, hp, hr, S(lod, 8), 0.5, True)
    if lod == 0:  # dents
        for k in range(6):
            x = 1.0 + k * 0.1
            for sgn in (-1, 1):
                cyl(b, (x, sgn * (0.2 - 0.02 * k), 0.24), (x, sgn * (0.2 - 0.02 * k), 0.08), 0.03, 0.004, 4)
    return m.out()


def p_pack_saddle(spec, lod):
    """Bât de mulet posé au sol (n° 91) : arçon de bois en A, deux sacoches de toile."""
    m = M()
    d = m.p("Wood_Dark")
    for x in (-0.38, 0.38):
        cyl(d, (x, -0.32, 0.0), (x, 0, 0.62), 0.03, 0.03, 5, False)
        cyl(d, (x, 0.32, 0.0), (x, 0, 0.62), 0.03, 0.03, 5, False)
    cyl(d, (-0.48, 0, 0.6), (0.48, 0, 0.6), 0.035, 0.035, S(lod, 6))
    for s in (-1, 1):
        box(m.p("Canvas"), (-0.42, s * 0.3 - 0.1, 0.02), (0.42, s * 0.3 + 0.1, 0.42))
        if lod < 2:
            box(m.p("Leather"), (-0.3, s * 0.42 - 0.02, 0.22), (0.3, s * 0.42 + 0.02, 0.4))
    if lod < 2:
        box(m.p("Leather"), (-0.45, -0.25, 0.5), (0.45, 0.25, 0.54))
    return m.out()


def p_trebuchet(spec, lod):
    """Trébuchet nain à contrepoids sur roues (n° 98), H ~9 m, base 5 x 4 m."""
    m = M()
    d, w = m.p("Wood_Dark"), m.p("Wood_Plank")
    for y in (-1.6, 1.6):
        box(d, (-2.5, y - 0.15, 0.45), (2.5, y + 0.15, 0.75))
        for x0 in (-1.8, 1.8):  # montants en A
            cyl(d, (x0, y, 0.75), (0, y * 0.75, 5.2), 0.14, 0.12, S(lod, 6), False)
        cyl(d, (0, y, 0.75), (0, y * 0.75, 5.2), 0.15, 0.13, S(lod, 6), False)
    for x in (-2.3, 0, 2.3):
        box(d, (x - 0.12, -1.75, 0.5), (x + 0.12, 1.75, 0.7))
    for x in (-1.9, 1.9):
        for y in (-1.95, 1.95):
            wheel(m, (x, y, 0.45), 0.45, 0.18, lod, 6, axis="y")
    cyl(m.p("Iron"), (0, -1.4, 5.2), (0, 1.4, 5.2), 0.1, 0.1, S(lod, 8))
    ang = math.radians(32)
    a = Vector((0, 0, 5.2))
    tip = a + Vector((math.cos(ang), 0, math.sin(ang))) * 6.2
    short = a - Vector((math.cos(ang), 0, math.sin(ang))) * 1.9
    cyl(d, short, tip, 0.2, 0.08, S(lod, 6))
    box(w, (short.x - 0.75, -0.75, short.z - 2.4), (short.x + 0.75, 0.75, short.z - 1.0))   # caisse du contrepoids
    if lod < 2:
        for k in (-0.5, 0.5):
            cyl(m.p("Iron"), (short.x, k, short.z), (short.x, k, short.z - 1.0), 0.03, 0.03, 4, False)
        cyl(m.p("Rope"), tip, tip - Vector((0.8, 0, 2.5)), 0.02, 0.02, 4, False)
    return m.out()


def p_chandelier_wheel(spec, lod):
    """Lustre-roue à bougies suspendu (n° 99, remplace Chandelier) : pivot au crochet (z = 0),
    roue de bois cerclée Ø 1,3 m à ~1,2 m sous le crochet."""
    m = M()
    z = -1.22
    R = 0.62
    cyl(m.p("Wood_Dark"), (0, 0, z - 0.05), (0, 0, z + 0.05), R, R, S(lod, 20), False)
    cyl(m.p("Wood_Dark"), (0, 0, z - 0.05), (0, 0, z + 0.05), R - 0.08, R - 0.08, S(lod, 20), False)
    cyl(m.p("Iron"), (0, 0, z - 0.06), (0, 0, z - 0.03), R + 0.01, R + 0.01, S(lod, 20), False)
    cyl(m.p("Wood_Dark"), (0, 0, z - 0.08), (0, 0, z + 0.08), 0.1, 0.1, S(lod, 8))
    for k in range(4):
        a = k * math.pi / 2
        cyl(m.p("Wood_Dark"), (0, 0, z), (math.cos(a) * (R - 0.05), math.sin(a) * (R - 0.05), z), 0.03, 0.03, 4, False)
    for k in range(3):
        a = k * 2 * math.pi / 3
        cyl(m.p("Iron"), (math.cos(a) * (R - 0.04), math.sin(a) * (R - 0.04), z + 0.05), (0, 0, -0.08), 0.008, 0.008, 4, False)
    cyl(m.p("Iron"), (0, 0, -0.08), (0, 0, 0.03), 0.02, 0.02, 4)
    n = {0: 12, 1: 8, 2: 6}[lod]
    for k in range(n):
        a = 2 * math.pi * (k + 0.5) / n
        candle(m, Vector((math.cos(a) * (R - 0.04), math.sin(a) * (R - 0.04), z + 0.05)), 0.16, 0.022, lod)
    return m.out()


def p_beam_truss(spec, lod):
    """Ferme de charpente apparente (n° 100) : entrait 8 m, arbalétriers, poinçon, contrefiches."""
    m = M()
    d = m.p("Wood_Dark")
    box(d, (-4.0, -0.2, 0.0), (4.0, 0.2, 0.4))
    apex = Vector((0, 0, 3.2))
    for s in (-1, 1):
        cyl(d, Vector((s * 3.9, 0, 0.3)), apex, 0.18, 0.18, 4)
    box(d, (-0.16, -0.16, 0.4), (0.16, 0.16, 3.1))
    if lod < 2:
        for s in (-1, 1):
            cyl(d, Vector((0, 0, 0.9)), Vector((s * 1.9, 0, 1.8)), 0.1, 0.1, 4)
            for x in (s * 1.5, s * 3.0):
                cyl(m.p("Iron"), (x, -0.21, 0.2), (x, 0.21, 0.2), 0.025, 0.025, 6)
    return m.out()


def p_armchair_carved(spec, lod):
    """Fauteuil sculpté à volutes (n° 101), H 1,4 m, assise 0,45 m."""
    m = M()
    d = m.p("Wood_Dark")
    for x in (-0.3, 0.3):
        for y in (-0.26, 0.24):
            hgt = 0.62 if y < 0 else 1.32
            cyl(d, (x, y, 0), (x, y, hgt), 0.035, 0.03, S(lod, 8))
        box(d, (x - 0.04, -0.3, 0.62), (x + 0.04, 0.26, 0.68))           # accoudoirs
        if lod < 2:
            cyl(d, (x, -0.31, 0.6), (x, -0.31, 0.7), 0.06, 0.06, S(lod, 10))  # volutes
    box(d, (-0.32, -0.28, 0.4), (0.32, 0.26, 0.45))
    box(d, (-0.3, 0.2, 0.5), (0.3, 0.26, 1.28))
    if lod < 2:
        lathe_at(d, 0, 0.23, [(0.05, 1.28), (0.09, 1.34), (0.03, 1.42)], S(lod, 8))
        for x in (-0.3, 0.3):
            lathe_at(d, x, 0.24, [(0.045, 1.32), (0.06, 1.36), (0.001, 1.42)], S(lod, 8))
    c = m.p("Cloth_Red")
    box(c, (-0.28, -0.26, 0.45), (0.28, 0.22, 0.52))
    box(c, (-0.26, 0.17, 0.6), (0.26, 0.2, 1.18))
    return m.out()


def p_shelf_apothecary(spec, lod):
    """Étagère d'apothicaire chargée de bocaux et de fioles (n° 102), 2 x 0,4 x 2,2 m."""
    m = M()
    d = m.p("Wood_Dark")
    for x in (-1.0, 0.0, 1.0):
        box(d, (x - 0.03, -0.2, 0), (x + 0.03, 0.2, 2.2))
    zs = (0.05, 0.5, 0.95, 1.4, 1.85)
    for z in zs:
        box(d, (-1.0, -0.2, z - 0.03), (1.0, 0.2, z))
    box(d, (-1.0, 0.18, 0), (1.0, 0.2, 2.2))
    rng = random.Random(spec["seed"])
    per = {0: 7, 1: 4, 2: 0}[lod]
    texs = ("Glass_Amber", "Glass_Green", "Glass_Clear", "Ceramic")
    for z in zs[:4]:
        for k in range(per * 2):
            x = -0.92 + 1.84 * (k + 0.5) / (per * 2) + rng.uniform(-0.03, 0.03)
            if abs(x) < 0.06:
                continue
            H = rng.uniform(0.14, 0.32)
            R = rng.uniform(0.04, 0.08)
            tex = texs[rng.randrange(4)]
            p = m.p(tex)
            i = len(p.v)
            if tex == "Ceramic":
                lathe(p, [(R * 0.8, 0), (R * 1.1, H * 0.5), (R * 0.9, H * 0.85), (R * 0.95, H)], S(lod, 8))
            else:
                lathe(p, [(R * 0.8, 0), (R, H * 0.1), (R, H * 0.6), (R * 0.4, H * 0.78), (R * 0.35, H)], S(lod, 8))
            for q in range(i, len(p.v)):
                p.v[q] = p.v[q] + Vector((x, rng.uniform(-0.05, 0.05), z))
    return m.out()


def p_chain_hook(spec, lod):
    """Chaîne pendante à crochet (n° 103) : potence murale à 3 m, chaîne, crochet, mou au sol."""
    m = M()
    i = m.p("Iron")
    box(m.p("Wood_Dark"), (-0.1, 0.0, 2.6), (0.1, 0.12, 3.2))
    cyl(i, (0, 0.06, 3.0), (0, -0.4, 3.0), 0.025, 0.025, 6)
    n = {0: 26, 1: 12, 2: 4}[lod]
    top, bot = Vector((0, -0.4, 2.98)), Vector((0, -0.4, 0.55))
    for k in range(n):
        t0, t1 = k / n, (k + 0.75) / n
        a = top.lerp(bot, t0)
        b = top.lerp(bot, t1)
        if lod == 2:
            cyl(i, top, bot, 0.02, 0.02, 4, False)
            break
        rot = 0.03 if k % 2 else 0.0
        cyl(i, a + Vector((rot, 0, 0)), b + Vector((rot, 0, 0)), 0.018, 0.018, 4, False)
    hk = [Vector((0, -0.4, 0.55)), Vector((0, -0.4, 0.35)), Vector((0.06, -0.4, 0.22)), Vector((0.14, -0.4, 0.28)),
          Vector((0.15, -0.4, 0.38))]
    API["tube"](i, hk, [0.022, 0.024, 0.022, 0.018, 0.012], S(lod, 6), 0.5)
    if lod < 2:  # mou de chaîne posé au sol
        for k in range(10):
            a = k * 0.9
            p0 = Vector((0.25 + 0.18 * math.cos(a), -0.2 + 0.18 * math.sin(a), 0.02 + 0.012 * k))
            p1 = Vector((0.25 + 0.18 * math.cos(a + 0.7), -0.2 + 0.18 * math.sin(a + 0.7), 0.02 + 0.012 * k))
            cyl(i, p0, p1, 0.018, 0.018, 4, False)
    return m.out()


def p_door_knocker(spec, lod):
    """Heurtoir à anneau sur platine ronde à mufle (n° 104), Ø 0,25 m, origine au bas."""
    m = M()
    b = m.p("Brass")
    cyl(b, (0, 0, 0.16), (0, -0.03, 0.16), 0.11, 0.1, S(lod, 16))
    if lod < 2:  # mufle stylisé : museau et deux bosses
        cyl(b, (0, -0.03, 0.16), (0, -0.07, 0.15), 0.045, 0.03, S(lod, 8))
        for x in (-0.045, 0.045):
            cyl(b, (x, -0.03, 0.2), (x, -0.05, 0.2), 0.02, 0.012, 6)
    ring = []
    n = {0: 14, 1: 10, 2: 6}[lod]
    for k in range(n + 1):
        a = -math.pi / 2 + 2 * math.pi * k / n
        ring.append(Vector((0.09 * math.cos(a), -0.08, 0.1 + 0.09 * math.sin(a))))
    API["tube"](b, ring, [0.012] * len(ring), S(lod, 6), 0.5)
    return m.out()


def p_table_stone(spec, lod):
    """Table de pierre massive (n° 105), 2 x 0,9 m, H 0,9 m (sans runes)."""
    m = M()
    s = m.p("Stone_Carved")
    box(s, (-1.0, -0.45, 0.72), (1.0, 0.45, 0.9))
    for x in (-0.68, 0.68):
        box(s, (x - 0.22, -0.32, 0.0), (x + 0.22, 0.32, 0.72))
        if lod < 2:
            box(s, (x - 0.27, -0.37, 0.0), (x + 0.27, 0.37, 0.08))
    return m.out()


def p_basket(spec, lod):
    """Corbeille tressée à anse (n° 107), Ø 0,5 m."""
    m = M()
    s = m.p("Straw")
    lathe(s, [(0.16, 0.0), (0.22, 0.06), (0.25, 0.24), (0.26, 0.3), (0.24, 0.3), (0.23, 0.25), (0.2, 0.06), (0.14, 0.03)], S(lod, 14))
    cyl(s, (0, 0, 0.02), (0, 0, 0.02), 0.15, 0.15, S(lod, 14))
    if lod < 2:
        pts = [Vector((0.24 * math.cos(a), 0, 0.3 + 0.22 * math.sin(a))) for a in [k * math.pi / 8 for k in range(9)]]
        API["tube"](s, pts, [0.015] * 9, 5, 0.5)
    return m.out()


def p_cushion_pile(spec, lod):
    """Tas de coussins et de fourrures (n° 108), ~2 x 2 m."""
    m = M()
    rng = random.Random(spec["seed"])
    fur = m.p("Fur")
    i0 = len(fur.v)
    lathe(fur, [(0.95, 0.0), (1.0, 0.04), (0.9, 0.09), (0.001, 0.1)], S(lod, 12))
    for k in range(i0, len(fur.v)):
        v = fur.v[k]
        fur.v[k] = Vector((v.x * 1.0, v.y * 0.85, v.z))
    n = {0: 7, 1: 5, 2: 3}[lod]
    for k in range(n):
        tex = ("Cloth_Red", "Cloth_Ochre", "Cloth_White", "Cloth_Striped")[k % 4]
        p = m.p(tex)
        i = len(p.v)
        r = rng.uniform(0.28, 0.4)
        lathe(p, [(r * 0.85, 0), (r, 0.06), (r * 0.9, 0.13), (0.001, 0.15)], S(lod, 10))
        a = rng.uniform(0, 6.28)
        cx, cy = rng.uniform(-0.55, 0.55), rng.uniform(-0.45, 0.45)
        z = 0.09 + (0.12 if k >= 4 else 0.0)
        xf = Matrix.Translation((cx, cy, z)) @ Matrix.Rotation(a, 4, "Z") @ Matrix.Diagonal((1.25, 0.9, 1.0, 1.0)) @ \
            Matrix.Rotation(rng.uniform(-0.25, 0.25), 4, "X")
        for q in range(i, len(p.v)):
            p.v[q] = xf @ p.v[q]
    return m.out()


def p_table_cloth(spec, lod):
    """Grande table couverte d'une nappe à carreaux (n° 109, variante de Table_Large)."""
    parts = p_table_large(spec, lod)
    m = M()
    for p in parts:
        _merge(m, p, Matrix.Identity(4))
    c = m.p("Cloth_Checked")
    box(c, (-1.3, -0.62, 0.81), (1.3, 0.62, 0.825))
    for s in (-1, 1):
        box(c, (-1.3, s * 0.62 - 0.006, 0.55), (1.3, s * 0.62 + 0.006, 0.825))
    return m.out()


def p_stepladder(spec, lod):
    """Escabeau de bois en A (n° 110), H ~2 m."""
    m = M()
    d = m.p("Wood_Plank")
    H, base = 2.0, 0.55
    for s in (-1, 1):
        for x in (-0.25, 0.25):
            cyl(d, (x, s * base, 0), (x * 0.8, s * 0.06, H), 0.03, 0.025, 4, False)
    nr = {0: 7, 1: 4, 2: 2}[lod]
    for k in range(nr):
        t = (k + 1) / (nr + 1)
        y = base * (1 - t) + 0.06 * t
        x = 0.25 * (1 - t) + 0.2 * t
        box(d, (-x, -y - 0.05, H * t - 0.015), (x, -y + 0.05, H * t + 0.015))
    box(d, (-0.24, -0.1, H - 0.02), (0.24, 0.1, H + 0.03))
    return m.out()


def p_ground_cracked(spec, lod):
    """Plaque de sol craquelé (n° 80), 4 x 4 m, posée à plat (bord découpé en alpha)."""
    m = M()
    p = m.p("Ground_Cracked", True)
    n = {0: 6, 1: 3, 2: 1}[lod]
    i0 = len(p.v)
    for j in range(n + 1):
        for i in range(n + 1):
            x, y = -2 + 4 * i / n, -2 + 4 * j / n
            z = 0.03 + (0.025 * math.sin(x * 2.1 + y * 1.3) if 0 < i < n and 0 < j < n else 0.0)
            p.v.append(Vector((x, y, z)))
    for j in range(n):
        for i in range(n):
            a = i0 + j * (n + 1) + i
            p.f.append((a, a + 1, a + n + 2, a + n + 1))
            p.uv.append([(i / n, j / n), ((i + 1) / n, j / n), ((i + 1) / n, (j + 1) / n), (i / n, (j + 1) / n)])
            p.cn.append(None)
    p.smooth = True
    return m.out()


# =========================================================== tombes anciennes
# Demande du propriétaire (22/11/2026) : cimetières aux tombes ANCIENNES — tas de
# cailloux envahis de mousse, croix de bois plus ou moins bien faites, vieilles stèles.
def _stone(part, c, r, h, seed, lod):
    """Caillou bosselé (demi-ellipsoïde enfoncé) centré en c, rayon r, hauteur h."""
    i0 = len(part.v)
    n = S(lod, 7)
    lathe(part, [(r, -0.03), (r * 0.95, h * 0.45), (r * 0.6, h * 0.85), (0.001, h)], n)
    for k in range(i0, len(part.v)):
        v = part.v[k]
        part.v[k] = Vector((c.x + v.x * (1.0 + 0.25 * math.sin(seed + k)), c.y + v.y * (0.8 + 0.2 * math.cos(seed * 1.7 + k)),
                            max(-0.03, c.z + v.z * (0.85 + 0.3 * math.sin(seed * 0.7 + k * 1.3)))))


def p_grave_cairn(spec, lod):
    """Tombe ancienne : tas de cailloux allongé (~2 x 0,9 m), à demi enfoui et moussu."""
    m = M()
    rng = random.Random(spec["seed"])
    n = {0: 26, 1: 12, 2: 5}[lod]
    big = {0: 0, 1: 0, 2: 1}[lod]
    for k in range(n):
        tex = "Rock_Mossy" if rng.random() < 0.55 else "Rock_Grey"
        u, v = rng.uniform(-1, 1), rng.uniform(-1, 1)
        if u * u + v * v > 1.0:
            u, v = u * 0.7, v * 0.7
        x, y = u * 0.95, v * 0.42
        hump = 0.16 * (1 - (u * u + v * v)) + 0.02
        r = rng.uniform(0.1, 0.2) * (1.6 if big else 1.0)
        _stone(m.p(tex), Vector((x, y, hump * rng.uniform(0.3, 0.8))), r, rng.uniform(0.08, 0.16) + hump * 0.4, k + spec["seed"], lod)
    return m.out()


def p_cross_wood(spec, lod):
    """Croix de bois de qualité variable : _1 équarrie et assemblée (usée), _2 branches
    liées de corde, _3 vieille, penchée, bras cassé."""
    m = M()
    v = spec["id"][-1]
    rng = random.Random(spec["seed"])
    if v == "1":
        w = m.p("Wood_Weathered")
        box(w, (-0.05, -0.04, -0.25), (0.05, 0.04, 1.3))
        box(w, (-0.36, -0.035, 0.88), (0.36, 0.035, 0.98))
        if lod < 2:
            box(w, (-0.07, -0.05, -0.05), (0.07, 0.05, 0.05))
        return m.out()
    if v == "2":
        b = m.p("Bark_Dark")
        pts = [Vector((rng.uniform(-0.03, 0.03), rng.uniform(-0.02, 0.02), -0.25 + 1.35 * k / 4)) for k in range(5)]
        API["tube"](b, pts, [0.045, 0.042, 0.04, 0.036, 0.032], S(lod, 6), 0.5, True)
        arm = [Vector((-0.32, 0.0, 0.8)), Vector((0.0, 0.03, 0.83)), Vector((0.3, 0.0, 0.79))]
        API["tube"](b, arm, [0.03, 0.032, 0.026], S(lod, 6), 0.5, True)
        if lod < 2:
            r = m.p("Rope")
            for k in range(3):
                cyl(r, (-0.05, -0.045, 0.78 + 0.02 * k), (0.05, 0.045, 0.84 - 0.02 * k), 0.012, 0.012, 4, False)
                cyl(r, (0.05, -0.045, 0.78 + 0.02 * k), (-0.05, 0.045, 0.84 - 0.02 * k), 0.012, 0.012, 4, False)
        return m.out()
    w = m.p("Wood_Weathered")
    i0 = len(w.v)
    box(w, (-0.05, -0.04, -0.25), (0.05, 0.04, 0.95))
    box(w, (-0.3, -0.035, 0.62), (0.05, 0.035, 0.7))           # bras gauche seul
    if lod < 2:                                                    # moignon du bras cassé
        box(w, (0.05, -0.03, 0.63), (0.13, 0.03, 0.69))
    tilt = Matrix.Rotation(math.radians(14), 4, "Y") @ Matrix.Rotation(math.radians(6), 4, "X")
    for k in range(i0, len(w.v)):
        w.v[k] = tilt @ w.v[k]
    if lod < 2:                                                    # bras tombé dans l'herbe
        box(w, (0.1, 0.15, 0.0), (0.42, 0.21, 0.06))
    return m.out()


def p_grave_stone_old(spec, lod):
    """Vieille stèle de pierre usée, moussue et penchée, à demi enfoncée."""
    m = M()
    s = m.p("Rock_Mossy")
    i0 = len(s.v)
    box(s, (-0.28, -0.08, -0.3), (0.28, 0.08, 0.55))
    cyl(s, (0, -0.08, 0.55), (0, 0.08, 0.55), 0.28, 0.28, S(lod, 10))
    _jitter(s, i0, 0.012, spec["seed"] % 13)
    tilt = Matrix.Rotation(math.radians(-9), 4, "X") @ Matrix.Rotation(math.radians(5), 4, "Y")
    for k in range(i0, len(s.v)):
        v = tilt @ s.v[k]
        s.v[k] = Vector((v.x, v.y, max(v.z, -0.3)))
    if lod < 2:
        _stone(m.p("Rock_Grey"), Vector((0.05, 0.55, 0.0)), 0.5, 0.06, spec["seed"], lod)   # dalle enfoncée
    return m.out()


def p_turret_corner(spec, lod):
    """Tourelle d'angle ronde à flèche d'ardoise (n° 92) : Ø 3 m, corps 9 m (3 niveaux),
    flèche conique 5 m ; posée sur l'angle avant gauche des maisons hautes à ardoise."""
    m = M()
    n = S(lod, 18)
    cyl(m.p("Stone_Wall"), (0, 0, -0.2), (0, 0, 1.0), 1.55, 1.55, n, cap=False)
    cyl(m.p("Plaster"), (0, 0, 1.0), (0, 0, 9.0), 1.5, 1.5, n, cap=False)
    cyl(m.p("Wood_Dark"), (0, 0, 8.85), (0, 0, 9.1), 1.6, 1.6, n)
    lathe(m.p("Shingle_Slate"), [(1.75, 9.0), (1.45, 10.0), (0.8, 12.0), (0.25, 13.6), (0.02, 14.0)], n)
    if lod < 2:
        cyl(m.p("Iron"), (0, 0, 13.9), (0, 0, 14.4), 0.03, 0.02, 5)
        for k in range(3 if lod == 0 else 2):
            a = math.radians(-120 + 60 * k)
            z = 3.4 + 3.0 * k
            c = Vector((math.cos(a) * 1.49, math.sin(a) * 1.49, z))
            t = Vector((-math.sin(a), math.cos(a), 0))
            nrm = Vector((math.cos(a), math.sin(a), 0))
            box_pts = [c - t * 0.25 - Vector((0, 0, 0.45)), c + t * 0.25 - Vector((0, 0, 0.45)),
                       c + t * 0.25 + Vector((0, 0, 0.45)), c - t * 0.25 + Vector((0, 0, 0.45))]
            g = m.p("Wood_Dark")
            i = len(g.v)
            g.v += [q + nrm * 0.02 for q in box_pts]
            g.f.append((i, i + 1, i + 2, i + 3))
            g.uv.append([(0, 0), (1, 0), (1, 1), (0, 1)])
            g.cn.append(None)
    return m.out()


# ================================================================ architecture T2
# n° 93-97 : pièces de kit en pierre (places, halles, palais, temples, cachots).
def _ray_rect(cx, cz, dx, dz, xl, xr, zt):
    """Point où le rayon (cx, cz) + t (dx, dz) sort du rectangle [xl, xr] x ]-inf, zt]."""
    ts = []
    if dx > 1e-9:
        ts.append((xr - cx) / dx)
    if dx < -1e-9:
        ts.append((xl - cx) / dx)
    if dz > 1e-9:
        ts.append((zt - cz) / dz)
    t = min(ts) if ts else 0.0
    return cx + dx * t, cz + dz * t


def _arch_band(part, x0, x1, z_spring, rise, depth, thick, lod, y0=None, rect=None):
    """Arc en plein cintre (ou surbaissé si rise < demi-portée) de x0 à x1, naissance à
    z_spring, épaisseur radiale `thick`, profondeur `depth` (Y centré). `rect` =
    (x_gauche, x_droite, z_haut) : l'extrados suit ce rectangle (écoinçons pleins)."""
    n = {0: 14, 1: 8, 2: 5}[lod]
    cx, r = (x0 + x1) / 2, (x1 - x0) / 2
    yA, yB = (-depth / 2, depth / 2) if y0 is None else y0
    for k in range(n):
        a0, a1 = math.pi * k / n, math.pi * (k + 1) / n
        pts = []
        for a in (a0, a1):
            ci, si = math.cos(a), math.sin(a)
            if rect:
                xo, zo = _ray_rect(cx, z_spring, ci, si * (rise / r), *rect)
                pts.append((cx + r * ci, z_spring + rise * si, xo, zo))
            else:
                pts.append((cx + r * ci, z_spring + rise * si, cx + (r + thick) * ci, z_spring + (rise + thick) * si))
        (xi0, zi0, xo0, zo0), (xi1, zi1, xo1, zo1) = pts
        quads = [
            [(xi0, yA, zi0), (xi1, yA, zi1), (xo1, yA, zo1), (xo0, yA, zo0)],
            [(xo0, yB, zo0), (xo1, yB, zo1), (xi1, yB, zi1), (xi0, yB, zi0)],
            [(xi1, yA, zi1), (xi0, yA, zi0), (xi0, yB, zi0), (xi1, yB, zi1)],
            [(xo0, yA, zo0), (xo1, yA, zo1), (xo1, yB, zo1), (xo0, yB, zo0)],
        ]
        for q in quads:
            i = len(part.v)
            part.v += [Vector(v) for v in q]
            part.f.append((i, i + 1, i + 2, i + 3))
            part.uv.append([(0, 0), (1, 0), (1, 1), (0, 1)])
            part.cn.append(None)
    part.smooth = False


def p_column_stone(spec, lod):
    """Colonne de pierre (n° 93) : base moulurée, fût Ø 0,6 m, chapiteau ; H 5 m."""
    m = M()
    s = m.p("Stone_Carved")
    box(s, (-0.42, -0.42, 0.0), (0.42, 0.42, 0.3))
    lathe(s, [(0.36, 0.3), (0.34, 0.42), (0.3, 0.5), (0.3, 4.4), (0.34, 4.5), (0.4, 4.62)], S(lod, 14))
    box(s, (-0.45, -0.45, 4.62), (0.45, 0.45, 5.0))
    return m.out()


def p_arch_stone(spec, lod):
    """Arc en plein cintre posé sur deux colonnes (n° 93) : entraxe 4 m, sommet à ~7,4 m ;
    piles comprises (origine au pied, au centre)."""
    m = M()
    s = m.p("Stone_Carved")
    for x in (-2.0, 2.0):
        box(s, (x - 0.42, -0.42, 0.0), (x + 0.42, 0.42, 0.3))
        lathe_at(s, x, 0, [(0.36, 0.3), (0.3, 0.5), (0.3, 4.4), (0.4, 4.62)], S(lod, 12))
        box(s, (x - 0.45, -0.45, 4.62), (x + 0.45, 0.45, 5.0))
    _arch_band(s, -1.7, 1.7, 5.0, 1.7, 0.8, 0.55, lod, rect=(-2.45, 2.45, 7.25))
    box(s, (-2.45, -0.42, 7.25), (2.45, 0.42, 7.45))
    return m.out()


def p_stairs_street(spec, lod):
    """Escalier de rue en pierre (n° 94) : 2 m de large, 10 marches de 0,18 x 0,30 m,
    murets latéraux ; départ en y = 0, montée vers +Y."""
    m = M()
    s = m.p("Stone_Wall")
    for k in range(10):
        box(s, (-1.0, k * 0.3, 0.0), (1.0, (k + 1) * 0.3 + 0.02, (k + 1) * 0.18))
    if lod < 2:
        for x in (-1.25, 1.0):
            for k in range(10):
                box(s, (x, k * 0.3, 0.0), (x + 0.25, (k + 1) * 0.3, (k + 1) * 0.18 + 0.75))
    return m.out()


def p_flying_buttress(spec, lod):
    """Arc-boutant (n° 94-96) : culée massive et demi-arc rampant de 6 m de portée."""
    m = M()
    s = m.p("Stone_Wall")
    box(s, (-0.6, -0.5, 0.0), (0.6, 0.5, 6.0))
    if lod < 2:   # pinacle
        lathe_at(s, 0, 0, [(0.5, 6.0), (0.001, 7.4)], 4)
    n = {0: 12, 1: 7, 2: 4}[lod]
    for k in range(n):
        t0, t1 = k / n, (k + 1) / n
        def at(t, off):
            x = 0.6 + 5.4 * t
            z = 5.6 + 2.0 * math.sin(t * math.pi / 2) + off
            return x, z
        (x0, z0), (x1, z1) = at(t0, 0), at(t1, 0)
        th = 0.6
        q = [[(x0, -0.35, z0), (x1, -0.35, z1), (x1, -0.35, z1 - th), (x0, -0.35, z0 - th)],
             [(x1, 0.35, z1), (x0, 0.35, z0), (x0, 0.35, z0 - th), (x1, 0.35, z1 - th)],
             [(x0, 0.35, z0), (x1, 0.35, z1), (x1, -0.35, z1), (x0, -0.35, z0)],
             [(x0, -0.35, z0 - th), (x1, -0.35, z1 - th), (x1, 0.35, z1 - th), (x0, 0.35, z0 - th)]]
        for qq in q:
            i = len(s.v)
            s.v += [Vector(v) for v in qq]
            s.f.append((i, i + 1, i + 2, i + 3))
            s.uv.append([(0, 0), (1, 0), (1, 1), (0, 1)])
            s.cn.append(None)
    return m.out()


def p_portal_gothic(spec, lod):
    """Portail gothique en ogive à rosace (n° 95) : baie 4 x 9 m, ébrasements, vantaux."""
    m = M()
    s = m.p("Stone_Carved")
    box(s, (-3.2, -0.6, 0.0), (-2.0, 0.6, 9.0))
    box(s, (2.0, -0.6, 0.0), (3.2, 0.6, 9.0))
    n = {0: 10, 1: 6, 2: 3}[lod]
    for side in (-1, 1):   # ogive : deux arcs de cercle centrés sur l'autre naissance
        for k in range(n):
            a0, a1 = math.radians(60) * k / n, math.radians(60) * (k + 1) / n
            R = 4.0
            cx = -2.0 * side
            pts = []
            for a in (a0, a1):
                xi = cx + side * R * math.cos(a)
                zi = 5.5 + R * math.sin(a)
                # extrados : rayon prolongé jusqu'au cadre (piédroits x = ±2, linteau z = 9)
                xo, zo = _ray_rect(cx, 5.5, side * math.cos(a), math.sin(a), -2.0, 2.0, 9.0)
                pts.append((xi, zi, xo, zo))
            (xi0, zi0, xo0, zo0), (xi1, zi1, xo1, zo1) = pts
            for y0, y1 in ((-0.6, 0.6),):
                for qq in ([(xi0, y0, zi0), (xi1, y0, zi1), (xo1, y0, zo1), (xo0, y0, zo0)],
                           [(xo0, y1, zo0), (xo1, y1, zo1), (xi1, y1, zi1), (xi0, y1, zi0)],
                           [(xi1, y0, zi1), (xi0, y0, zi0), (xi0, y1, zi0), (xi1, y1, zi1)]):
                    i = len(s.v)
                    s.v += [Vector(v) for v in qq]
                    s.f.append((i, i + 1, i + 2, i + 3))
                    s.uv.append([(0, 0), (1, 0), (1, 1), (0, 1)])
                    s.cn.append(None)
    box(s, (-3.2, -0.6, 9.0), (3.2, 0.6, 9.6))
    if lod < 2:   # rosace au-dessus du linteau
        cyl(m.p("Glass_Amber"), (0, -0.62, 7.2), (0, -0.55, 7.2), 0.9, 0.9, S(lod, 16))
        cyl(s, (0, -0.68, 7.2), (0, -0.6, 7.2), 1.05, 1.05, S(lod, 16), cap=False)
        box(s, (-2.0, -0.4, 5.3), (2.0, 0.4, 5.6))   # linteau du tympan
    box(s, (-2.0, -0.1, 5.6), (2.0, 0.1, 9.0))       # tympan (masqué par les écoinçons hors de l'ogive)
    d = m.p("Wood_Dark")
    box(d, (-2.0, -0.12, 0.0), (-0.02, 0.0, 5.3))
    box(d, (0.02, -0.12, 0.0), (2.0, 0.0, 5.3))
    if lod == 0:
        for z in (1.0, 2.6, 4.2):
            box(m.p("Iron"), (-1.9, -0.14, z), (1.9, -0.12, z + 0.08))
    return m.out()


def p_pillar_gothic(spec, lod):
    """Pilier gothique en faisceau (n° 96) : noyau et colonnettes, chapiteau, départ de
    quatre nervures d'ogive ; H ~12 m."""
    m = M()
    s = m.p("Stone_Carved")
    box(s, (-0.7, -0.7, 0.0), (0.7, 0.7, 0.5))
    cyl(s, (0, 0, 0.5), (0, 0, 9.0), 0.5, 0.5, S(lod, 12))
    if lod < 2:
        for k in range(8):
            a = k * math.pi / 4
            cyl(s, (math.cos(a) * 0.5, math.sin(a) * 0.5, 0.5), (math.cos(a) * 0.5, math.sin(a) * 0.5, 9.0), 0.13, 0.13, S(lod, 6), False)
    box(s, (-0.75, -0.75, 9.0), (0.75, 0.75, 9.4))
    for k in range(4):
        a = math.pi / 4 + k * math.pi / 2
        pts = [Vector((math.cos(a) * 0.6 * t * 4, math.sin(a) * 0.6 * t * 4, 9.4 + 2.6 * math.sin(t * math.pi / 2))) for t in [j / 6 for j in range(7)]]
        API["tube"](s, pts, [0.2] * 7, S(lod, 6), 0.5, True)
    return m.out()


def p_dungeon_vault(spec, lod):
    """Travée de cachot (n° 97) : voûte en berceau plein cintre, 4 m de large, 4 m de long,
    H 4 m, murs bas de pierre."""
    m = M()
    s = m.p("Stone_Wall")
    for x in (-2.4, 2.0):
        box(s, (x, -2.0, 0.0), (x + 0.4, 2.0, 2.0))
    _arch_band(s, -2.0, 2.0, 2.0, 2.0, 4.0, 0.4, lod)
    box(m.p("Stone_Carved"), (-2.0, -2.0, -0.05), (2.0, 2.0, 0.0))
    if lod < 2:   # anneau scellé
        lathe_at(m.p("Iron"), -1.98, 0.6, [(0.04, 1.4), (0.04, 1.42)], 6)
        ring = [Vector((-1.95, 0.6 + 0.08 * math.cos(a), 1.32 + 0.08 * math.sin(a))) for a in [k * math.pi / 5 for k in range(11)]]
        API["tube"](m.p("Iron"), ring, [0.012] * 11, 5, 0.5)
    return m.out()


def p_dungeon_pillar(spec, lod):
    """Pilier massif de cachot (n° 97), 1 x 1 m, H 4 m, chanfreins."""
    m = M()
    s = m.p("Stone_Wall")
    box(s, (-0.55, -0.55, 0.0), (0.55, 0.55, 0.3))
    box(s, (-0.5, -0.5, 0.3), (0.5, 0.5, 3.6))
    box(s, (-0.6, -0.6, 3.6), (0.6, 0.6, 4.0))
    return m.out()


def p_dungeon_grate(spec, lod):
    """Grille de cachot en treillis (n° 97) : porte de fer 1,2 x 2,2 m à croisillons rivetés."""
    m = M()
    i = m.p("Iron")
    box(i, (-0.6, -0.03, 0.0), (0.6, 0.03, 0.06))
    box(i, (-0.6, -0.03, 2.14), (0.6, 0.03, 2.2))
    for x in (-0.6, 0.57):
        box(i, (x, -0.03, 0.0), (x + 0.03, 0.03, 2.2))
    nv = {0: 7, 1: 4, 2: 2}[lod]
    for k in range(nv):
        x = -0.45 + 0.9 * k / (nv - 1)
        box(i, (x - 0.012, -0.012, 0.0), (x + 0.012, 0.012, 2.2))
    nh = {0: 9, 1: 5, 2: 3}[lod]
    for k in range(nh):
        z = 0.2 + 1.8 * k / (nh - 1)
        box(i, (-0.6, -0.018, z - 0.012), (0.6, 0.018, z + 0.012))
    return m.out()


def p_dungeon_window(spec, lod):
    """Soupirail à barreaux (n° 97) : encadrement de pierre 0,8 x 0,5 m, quatre barreaux."""
    m = M()
    s = m.p("Stone_Wall")
    box(s, (-0.5, -0.15, 0.0), (0.5, 0.15, 0.12))
    box(s, (-0.5, -0.15, 0.62), (0.5, 0.15, 0.74))
    for x in (-0.5, 0.38):
        box(s, (x, -0.15, 0.12), (x + 0.12, 0.15, 0.62))
    for k in range(4 if lod < 2 else 2):
        x = -0.27 + 0.54 * k / (3 if lod < 2 else 1)
        cyl(m.p("Iron"), (x, 0, 0.12), (x, 0, 0.62), 0.015, 0.015, S(lod, 6), False)
    return m.out()


# ================================================================ ponts T2
# n° 81-83 : modules de pont posés par tools/world_gen (BuildBridges). Repère de pièce :
# X le long du pont (2 m), Z blender = haut, origine au DESSUS du tablier (dalles) ou
# au pied (garde-corps, arches). Collisions : game/data/collision/building_pieces.json.
def p_bridge_stone_deck(spec, lod):
    """Dalle de tablier de pont de pierre, 2 x 2 m, dessus à z = 0,01 (marchable)."""
    m = M()
    box(m.p("Stone_Wall"), (-1.0, -1.0, -0.35), (1.0, 1.0, 0.01))
    return m.out()


def p_bridge_stone_parapet(spec, lod):
    """Garde-corps de pierre de pont, 2 m, H 0,95 m, chaperon débordant."""
    m = M()
    s = m.p("Stone_Wall")
    box(s, (-1.0, -0.17, 0.0), (1.0, 0.17, 0.82))
    box(m.p("Stone_Carved"), (-1.0, -0.22, 0.82), (1.0, 0.22, 0.95))
    return m.out()


def p_bridge_stone_arch(spec, lod):
    """Parement d'arche de pont (n° 81) : panneau 2 x 3 m percé d'un plein cintre
    (ouverture 1,6 m), claveaux et bandeau ; même encombrement que Wall_Arch."""
    m = M()
    s = m.p("Stone_Wall")
    box(s, (-1.0, 0.0, 0.0), (-0.8, 0.3, 2.2))
    box(s, (0.8, 0.0, 0.0), (1.0, 0.3, 2.2))
    _arch_band(s, -0.8, 0.8, 2.2, 0.8, 0.3, 0.2, lod, y0=(0.0, 0.3), rect=(-1.0, 1.0, 3.0))
    if lod < 2:
        box(m.p("Stone_Carved"), (-1.0, -0.04, 2.85), (1.0, 0.34, 3.0))
    return m.out()


def p_boardwalk_deck(spec, lod):
    """Passerelle de planches sur pilotis (n° 83), module 2 x 2 m : dessus à z = 0,01,
    deux longerons, quatre pieux descendant à 1,6 m sous le tablier."""
    m = M()
    w = m.p("Wood_Weathered")
    n = {0: 8, 1: 4, 2: 1}[lod]
    for k in range(n):
        x0 = -1.0 + 2.0 * k / n + 0.01
        x1 = -1.0 + 2.0 * (k + 1) / n - 0.01
        box(w, (x0, -1.0, -0.05), (x1, 1.0, 0.01))
    d = m.p("Wood_Dark")
    for y in (-0.8, 0.8):
        box(d, (-1.0, y - 0.07, -0.25), (1.0, y + 0.07, -0.05))
        if lod < 2:
            for x in (-0.85, 0.85):
                cyl(d, (x, y, -1.6), (x, y, -0.05), 0.09, 0.08, S(lod, 6))
    return m.out()


def p_bridge_rope_deck(spec, lod):
    """Pont suspendu de cordes et planches (n° 82), module 2 m x 1,5 m : planches liées,
    poteaux aux extrémités, mains courantes de corde à 1,1 m."""
    m = M()
    w = m.p("Wood_Weathered")
    n = {0: 10, 1: 5, 2: 2}[lod]
    for k in range(n):
        x0 = -1.0 + 2.0 * k / n + 0.025
        x1 = -1.0 + 2.0 * (k + 1) / n - 0.025
        box(w, (x0, -0.75, -0.04), (x1, 0.75, 0.01))
    r = m.p("Rope")
    for y in (-0.8, 0.8):
        cyl(m.p("Wood_Dark"), (-1.0, y, -0.1), (-1.0, y, 1.2), 0.05, 0.045, S(lod, 6))
        for z0 in ((1.1, 0.0) if lod < 2 else (1.1,)):
            pts = [Vector((-1.0 + 2.0 * t, y, z0 + (0.0 if z0 == 0.0 else -0.12 * math.sin(math.pi * t)))) for t in [j / 6 for j in range(7)]]
            API["tube"](r, pts, [0.02] * 7, S(lod, 5), 0.5)
        if lod == 0:
            for k in range(4):
                x = -0.75 + 0.5 * k
                cyl(r, (x, y, 0.0), (x, y, 1.1 - 0.12 * math.sin(math.pi * (x + 1) / 2)), 0.01, 0.01, 4, False)
    return m.out()


# ====================================================================== T3
# Atelier décors, tome 3 (n° 112-142, sauf 111 automne, 118 = Cross_Wood, 140 fait au T1,
# 141 renvoyé aux PNJ).
def _quad(part, a, b, c, d, uv=((0, 0), (1, 0), (1, 1), (0, 1))):
    i = len(part.v)
    part.v += [Vector(a), Vector(b), Vector(c), Vector(d)]
    part.f.append((i, i + 1, i + 2, i + 3))
    part.uv.append(list(uv))
    part.cn.append(None)


def p_mushroom_bracket(spec, lod):
    """Polypores en étagère sur un tronc (n° 112) : demi-disques étagés, dos plat en y = 0
    (posé contre l'écorce), ~0,5 x 0,5 m."""
    m = M()
    rng = random.Random(spec["seed"])
    n = {0: 5, 1: 3, 2: 2}[lod]
    for k in range(n):
        p = m.p("Fur" if k % 2 else "Leather")
        R = rng.uniform(0.12, 0.25)
        z = 0.06 + k * 0.1 + rng.uniform(-0.02, 0.02)
        x = rng.uniform(-0.12, 0.12)
        i0 = len(p.v)
        sides = S(lod, 10)
        ring_top, ring_bot = [], []
        for s in range(sides + 1):
            a = math.pi * s / sides
            ring_top.append(Vector((x + R * math.cos(a), -R * 0.8 * math.sin(a), z + 0.03)))
            ring_bot.append(Vector((x + R * math.cos(a) * 0.95, -R * 0.75 * math.sin(a), z - 0.015)))
        ctr_t, ctr_b = Vector((x, 0, z + 0.05)), Vector((x, 0, z - 0.02))
        for s in range(sides):
            _quad(p, ring_bot[s], ring_bot[s + 1], ring_top[s + 1], ring_top[s])
            j = len(p.v)
            p.v += [ctr_t, ring_top[s], ring_top[s + 1]]
            p.f.append((j, j + 1, j + 2)); p.uv.append([(0.5, 0.5), (0, 1), (1, 1)]); p.cn.append(None)
            j = len(p.v)
            p.v += [ctr_b, ring_bot[s + 1], ring_bot[s]]
            p.f.append((j, j + 1, j + 2)); p.uv.append([(0.5, 0.5), (1, 1), (0, 1)]); p.cn.append(None)
    return m.out()


def p_duckweed(spec, lod):
    """Lentilles d'eau (n° 113) : nappe flottante 4 x 4 m, carte unique en alpha (MASK)."""
    m = M()
    p = m.p("Plant_Duckweed", True)
    _quad(p, (-2, -2, 0.0), (2, -2, 0.0), (2, 2, 0.0), (-2, 2, 0.0))
    return m.out()


def p_vine(spec, lod):
    """Lierre pendant (n° 114, remplace Prop_Vine*) : cartes de feuillage verticales qui
    remplissent la boîte de l'original (fit_bbox), accroche en haut."""
    m = M()
    p = m.p("Leaves_Ivy", True)
    b = spec["bbox_ref"]
    x0, x1, z0, z1 = b[0], b[1], b[2], b[3]
    y0, y1 = -b[5], -b[4]
    rng = random.Random(spec["seed"])
    n = {0: 9, 1: 5, 2: 2}[lod]
    for k in range(n):
        w = (x1 - x0) * rng.uniform(0.35, 0.6)
        cx = rng.uniform(x0 + w / 2, x1 - w / 2) if x1 - x0 > w else (x0 + x1) / 2
        y = rng.uniform(y0, y1)
        top = z1 - rng.uniform(0, 0.15) * (z1 - z0)
        bot = z0 + rng.uniform(0, 0.25) * (z1 - z0)
        _quad(p, (cx - w / 2, y, bot), (cx + w / 2, y, bot), (cx + w / 2, y, top), (cx - w / 2, y, top))
    if (y1 - y0) > 0.6:   # retombée horizontale (lierre qui déborde d'un mur)
        for k in range({0: 4, 1: 2, 2: 1}[lod]):
            w = (x1 - x0) * rng.uniform(0.4, 0.7)
            cx = rng.uniform(x0 + w / 2, x1 - w / 2)
            z = z1 - rng.uniform(0.0, 0.2)
            _quad(p, (cx - w / 2, y0, z), (cx + w / 2, y0, z), (cx + w / 2, y1, z), (cx - w / 2, y1, z))
    return m.out()


def p_daisies(spec, lod):
    """Marguerites (n° 115, remplace Flower_3_*) : cartes croisées, H ~0,45 m."""
    m = M()
    p = m.p("Plant_Daisy", True)
    group = spec["id"].endswith("Group")
    n = ({0: 6, 1: 4, 2: 2} if group else {0: 3, 1: 2, 2: 2})[lod]
    W = 0.7 if group else 0.32
    rng = random.Random(spec["seed"])
    for k in range(n):
        a = math.pi * k / n + rng.uniform(-0.2, 0.2)
        c = Vector((rng.uniform(-0.12, 0.12), rng.uniform(-0.12, 0.12), 0)) if group else Vector((0, 0, 0))
        dx = Vector((math.cos(a), math.sin(a), 0)) * W / 2
        h = rng.uniform(0.4, 0.48)
        _quad(p, c - dx, c + dx, c + dx + Vector((0, 0, h)), c - dx + Vector((0, 0, h)))
    return m.out()


def p_haystack(spec, lod):
    """Meule de foin conique sur perche (n° 116), Ø 3 m, H 3 m."""
    m = M()
    lathe(m.p("Straw"), [(1.45, 0.0), (1.5, 0.35), (1.35, 1.2), (0.95, 2.0), (0.45, 2.6), (0.08, 2.9), (0.001, 2.95)], S(lod, 14))
    if lod < 2:
        cyl(m.p("Wood_Dark"), (0, 0, 2.6), (0.03, 0.02, 3.3), 0.035, 0.03, 5)
        cyl(m.p("Rope"), (0, 0, 1.7), (0, 0, 1.75), 1.12, 1.12, S(lod, 14), cap=False)
    return m.out()


def p_rock_path_irregular(spec, lod):
    """Pavage irrégulier 2 x 2 m (n° 119, variante de RockPath_Square_Wide) : dalles de
    0,4-0,8 m bombées, dessus à ~0,16 m, sur une assise de terre."""
    m = M()
    rng = random.Random(spec["seed"])
    s = m.p("Stone_Wall")
    box(m.p("Rock_Ochre"), (-1.0, -1.0, -0.01), (1.0, 1.0, 0.06))
    y = -1.0
    while y < 0.99:
        h = rng.uniform(0.4, 0.75)
        y1 = min(1.0, y + h)
        x = -1.0 + rng.uniform(-0.2, 0.0)
        while x < 0.99:
            w = rng.uniform(0.4, 0.8)
            x1 = min(1.0, x + w)
            g = 0.03
            if lod < 2 or w > 0.55:
                box(s, (max(-1.0, x) + g, y + g, 0.06), (x1 - g, y1 - g, rng.uniform(0.13, 0.166)))
            x = x1
        y = y1
    return m.out()


def p_tub_wood(spec, lod):
    """Baquet de bain en douves cerclées (n° 120), Ø 1,4 m, H 0,7 m, eau."""
    m = M()
    lathe(m.p("Wood_Plank"), [(0.6, 0.0), (0.7, 0.68), (0.66, 0.7), (0.57, 0.08)], S(lod, 18))
    cyl(m.p("Wood_Plank"), (0, 0, 0.06), (0, 0, 0.06), 0.58, 0.58, S(lod, 18))
    cyl(m.p("Water_Still"), (0, 0, 0.55), (0, 0, 0.55), 0.665, 0.665, S(lod, 18))
    if lod < 2:
        for z in (0.12, 0.55):
            r = 0.6 + 0.1 * z / 0.68 + 0.008
            cyl(m.p("Iron"), (0, 0, z - 0.025), (0, 0, z + 0.025), r, r, S(lod, 18), cap=False)
    return m.out()


def p_brazier_column(spec, lod):
    """Brasero-vasque sur fût de pierre (n° 121), H 2 m, vasque Ø 0,6 m, braises."""
    m = M()
    s = m.p("Stone_Carved")
    box(s, (-0.3, -0.3, 0.0), (0.3, 0.3, 0.2))
    lathe(s, [(0.18, 0.2), (0.15, 0.35), (0.15, 1.6), (0.2, 1.7)], S(lod, 12))
    lathe(m.p("Iron"), [(0.1, 1.7), (0.3, 1.85), (0.32, 2.0), (0.29, 2.0), (0.27, 1.9)], S(lod, 14))
    lathe(m.p("Rock_Red"), [(0.27, 1.92), (0.15, 1.98), (0.001, 2.0)], S(lod, 10))
    return m.out()


def p_drawbridge(spec, lod):
    """Pont-levis de planches abaissé (n° 122), 4 x 6 m, poutres, chaînes vers la porte ;
    dessus à z = 0,01 (marchable), long de Y (vers -Y = sortie)."""
    m = M()
    w = m.p("Wood_Dark")
    n = {0: 10, 1: 5, 2: 1}[lod]
    for k in range(n):
        x0 = -2.0 + 4.0 * k / n + 0.01
        x1 = -2.0 + 4.0 * (k + 1) / n - 0.01
        box(w, (x0, -6.0, -0.1), (x1, 0.0, 0.01))
    for x in (-1.7, 0.0, 1.7):
        box(m.p("Wood_Plank"), (x - 0.1, -6.0, -0.3), (x + 0.1, 0.0, -0.1))
    if lod < 2:
        for x in (-1.9, 1.9):
            cyl(m.p("Iron"), (x, -5.6, 0.02), (x, 0.2, 4.2), 0.03, 0.03, 5, False)
        for y in (-5.5, -3.0, -0.5):
            box(m.p("Iron"), (-2.0, y - 0.05, 0.01), (2.0, y + 0.05, 0.025))
    return m.out()


def p_chimney(spec, lod):
    """Souche de cheminée de pierre (n° 123, remplace Prop_Chimney*) : fût maçonné,
    bandeau, couronnement débordant, mitron."""
    m = M()
    b = spec["bbox_ref"]
    H = b[3]
    s = m.p("Stone_Wall")
    box(s, (-0.42, -0.42, 0.0), (0.42, 0.42, H - 0.35))
    box(m.p("Stone_Carved"), (-0.5, -0.5, H - 0.35), (0.5, 0.5, H - 0.2))
    if lod < 2:
        box(s, (-0.36, -0.36, H - 0.2), (0.36, 0.36, H - 0.05))
        cyl(m.p("Ceramic"), (0, 0, H - 0.05), (0, 0, H), 0.12, 0.1, S(lod, 8), cap=False)
    return m.out()


def p_tower_square_village(spec, lod):
    """Tour carrée de village (n° 124), 5 x 5 m, H 14 m : pierre, fenêtres étroites,
    toit pyramidal de bardeaux."""
    m = M()
    s = m.p("Stone_Wall")
    box(s, (-2.5, -2.5, 0.0), (2.5, 2.5, 10.5))
    box(m.p("Stone_Carved"), (-2.7, -2.7, 10.5), (2.7, 2.7, 10.8))
    r = m.p("Shingle")
    apex = Vector((0, 0, 14.0))
    cs = [Vector((-2.9, -2.9, 10.8)), Vector((2.9, -2.9, 10.8)), Vector((2.9, 2.9, 10.8)), Vector((-2.9, 2.9, 10.8))]
    for k in range(4):
        a, b = cs[k], cs[(k + 1) % 4]
        j = len(r.v)
        r.v += [a, b, apex]
        r.f.append((j, j + 1, j + 2)); r.uv.append([(0, 0), (3, 0), (1.5, 2)]); r.cn.append(None)
    d = m.p("Wood_Dark")
    box(d, (-0.55, -2.53, 0.0), (0.55, -2.5, 2.3))
    if lod < 2:
        for z in (4.0, 7.5):
            for (ax, ay) in ((0, -1), (1, 0), (0, 1), (-1, 0)):
                c = Vector((ax * 2.52, ay * 2.52, z))
                t = Vector((-ay, ax, 0)) * 0.18
                _quad(d, c - t, c + t, c + t + Vector((0, 0, 0.9)), c - t + Vector((0, 0, 0.9)))
        cyl(m.p("Iron"), (0, 0, 13.9), (0, 0, 14.6), 0.03, 0.02, 5)
    return m.out()


def p_bridge_hump(spec, lod):
    """Pont de pierre en dos d'âne à une arche (n° 125) : portée 8 m (X), tablier 3,5 m,
    flèche du tablier 1 m, parapets ; origine au niveau des culées (dessus z = 0,01)."""
    m = M()
    s = m.p("Stone_Wall")
    n = {0: 12, 1: 8, 2: 4}[lod]
    def top(x):
        return 0.01 + 1.0 * math.cos(math.pi * x / 9.0)
    for k in range(n):
        xa, xb = -4.5 + 9.0 * k / n, -4.5 + 9.0 * (k + 1) / n
        ta, tb = top(xa), top(xb)
        _quad(m.p("Stone_Carved"), (xa, -1.75, ta), (xb, -1.75, tb), (xb, 1.75, tb), (xa, 1.75, ta))
        for y in (-1.75, 1.75):   # parapets
            ys = (y, y + (0.3 if y < 0 else -0.3))
            _quad(s, (xa, ys[0], ta), (xb, ys[0], tb), (xb, ys[0], tb + 0.8), (xa, ys[0], ta + 0.8))
            _quad(s, (xb, ys[1], tb), (xa, ys[1], ta), (xa, ys[1], ta + 0.8), (xb, ys[1], tb + 0.8))
            _quad(s, (xa, ys[0], ta + 0.8), (xb, ys[0], tb + 0.8), (xb, ys[1], tb + 0.8), (xa, ys[1], ta + 0.8))
    # tympans percés d'une arche en plein cintre (ouverture 6 m, sous le tablier)
    for y0, y1 in ((-1.75, -1.75), (1.75, 1.75)):
        _arch_band(s, -3.0, 3.0, -2.0, 2.4, 0.0, 0.3, lod, y0=(y0 - 0.001, y1 + 0.001), rect=(-4.5, 4.5, 0.6))
    _arch_band(s, -3.0, 3.0, -2.0, 2.4, 3.5, 0.3, lod)
    box(s, (-4.5, -1.75, -2.0), (-3.0, 1.75, 0.0))
    box(s, (3.0, -1.75, -2.0), (4.5, 1.75, 0.0))
    return m.out()


def p_door_barn(spec, lod):
    """Porte de grange à deux vantaux, écharpes en Z (n° 126), 3 x 3,5 m, franchissable."""
    m = M()
    w = m.p("Wood_Weathered")
    d = m.p("Wood_Dark")
    for s in (-1, 1):
        x0, x1 = (-1.5, -0.01) if s < 0 else (0.01, 1.5)
        n = {0: 6, 1: 3, 2: 1}[lod]
        for k in range(n):
            a = x0 + (x1 - x0) * k / n + 0.006
            b = x0 + (x1 - x0) * (k + 1) / n - 0.006
            box(w, (a, -0.06, 0.0), (b, 0.0, 3.5))
        for z in (0.4, 3.0):
            box(d, (x0, -0.1, z - 0.08), (x1, -0.06, z + 0.08))
        if lod < 2:
            cyl(d, (x0 + 0.1, -0.08, 0.5), (x1 - 0.1, -0.08, 2.9), 0.06, 0.06, 4, False)
    if lod == 0:
        for s in (-1, 1):
            for z in (0.6, 2.8):
                box(m.p("Iron"), (s * 1.45 - 0.25, -0.11, z - 0.03), (s * 1.45 + 0.25, -0.1, z + 0.03))
    return m.out()


def p_facade_dwarf_balcony(spec, lod):
    """Balcon nain à balustres (n° 127) : dalle sur corbeaux, balustrade, 3 m x 1,2 m ;
    à poser contre une façade (dos en y = 0), sol du balcon à z = 2,4."""
    m = M()
    s = m.p("Stone_Carved")
    box(s, (-1.5, -1.2, 2.25), (1.5, 0.0, 2.4))
    for x in (-1.2, 0.0, 1.2):
        box(m.p("Stone_Wall"), (x - 0.15, -0.6, 1.7), (x + 0.15, 0.0, 2.25))
    box(s, (-1.5, -1.2, 3.25), (1.5, -1.05, 3.35))
    box(s, (-1.5, -1.2, 2.4), (-1.35, 0.0, 3.35))
    box(s, (1.35, -1.2, 2.4), (1.5, 0.0, 3.35))
    nb = {0: 11, 1: 6, 2: 3}[lod]
    for k in range(nb):
        x = -1.3 + 2.6 * k / (nb - 1)
        lathe_at(s, x, -1.12, [(0.04, 2.4), (0.07, 2.6), (0.04, 2.85), (0.06, 3.1), (0.04, 3.25)], S(lod, 6))
    return m.out()


def p_facade_dwarf_window(spec, lod):
    """Fenêtre naine à petits carreaux sous linteau de pierre (n° 127), 1,2 x 1,4 m ;
    dos en y = 0, appui à z = 0 (à poser sur un mur)."""
    m = M()
    s = m.p("Stone_Carved")
    box(s, (-0.75, -0.12, 0.0), (0.75, 0.0, 0.12))
    box(s, (-0.8, -0.14, 1.4), (0.8, 0.0, 1.62))
    for x in (-0.75, 0.6):
        box(s, (x, -0.12, 0.12), (x + 0.15, 0.0, 1.4))
    box(m.p("Glass_Amber"), (-0.6, -0.05, 0.12), (0.6, -0.04, 1.4))
    if lod < 2:
        d = m.p("Wood_Dark")
        for k in (1, 2, 3):
            x = -0.6 + 1.2 * k / 4
            box(d, (x - 0.02, -0.07, 0.12), (x + 0.02, -0.05, 1.4))
        for k in (1, 2, 3, 4):
            z = 0.12 + 1.28 * k / 5
            box(d, (-0.6, -0.07, z - 0.02), (0.6, -0.05, z + 0.02))
    return m.out()


def p_vault_dwarf(spec, lod):
    """Travée de halle naine (n° 128) : quatre piliers trapus, arcs surbaissés, sol en
    damier ; travée 5 x 5 m, H 4,6 m."""
    m = M()
    s = m.p("Stone_Wall")
    for x in (-2.5, 2.5):
        for y in (-2.5, 2.5):
            box(s, (x - 0.4, y - 0.4, 0.0), (x + 0.4, y + 0.4, 3.2))
            box(m.p("Stone_Carved"), (x - 0.5, y - 0.5, 3.2), (x + 0.5, y + 0.5, 3.45))
    for y in (-2.5, 2.5):
        _arch_band(s, -2.1, 2.1, 3.45, 0.9, 0.6, 0.35, lod, y0=(y - 0.3, y + 0.3))
    for x in (-2.5, 2.5):
        i0 = len(s.v)
        _arch_band(s, -2.1, 2.1, 3.45, 0.9, 0.6, 0.35, lod, y0=(-0.3, 0.3))
        rz = Matrix.Translation((x, 0, 0)) @ Matrix.Rotation(math.pi / 2, 4, "Z")
        for k in range(i0, len(s.v)):
            s.v[k] = rz @ s.v[k]
    f = m.p("Floor_Checker")
    _quad(f, (-2.9, -2.9, 0.01), (2.9, -2.9, 0.01), (2.9, 2.9, 0.01), (-2.9, 2.9, 0.01), ((0, 0), (2.9, 0), (2.9, 2.9), (0, 2.9)))
    return m.out()


def p_crucible_hanging(spec, lod):
    """Creuset suspendu à une potence de coulée (n° 129) : chevalet de bois, chaîne,
    creuset de fer Ø 1,2 m incliné ; H ~4,5 m."""
    m = M()
    d = m.p("Wood_Dark")
    for x in (-1.6, 1.6):
        cyl(d, (x, -0.9, 0.0), (x, 0.0, 4.3), 0.12, 0.1, S(lod, 6))
        cyl(d, (x, 0.9, 0.0), (x, 0.0, 4.3), 0.12, 0.1, S(lod, 6))
    box(d, (-1.8, -0.15, 4.2), (1.8, 0.15, 4.5))
    i = m.p("Iron")
    cyl(i, (0, 0, 4.2), (0, 0, 2.3), 0.03, 0.03, 4, False)
    tilt = Matrix.Translation((0, 0, 1.2)) @ Matrix.Rotation(math.radians(12), 4, "X")
    j0 = len(i.v)
    lathe(i, [(0.2, -0.55), (0.5, -0.45), (0.6, -0.1), (0.6, 0.25), (0.55, 0.28)], S(lod, 14))
    for k in range(j0, len(i.v)):
        i.v[k] = tilt @ i.v[k]
    lathe_at(m.p("Rock_Red"), 0, 0, [(0.5, 1.4), (0.001, 1.42)], S(lod, 10))
    for s in (-1, 1):
        cyl(i, (s * 0.6, 0, 1.45), (0, 0, 2.3), 0.02, 0.02, 4, False)
    return m.out()


def p_armor_stand(spec, lod):
    """Présentoir d'armure (n° 130) : croix de bois sur pied, cuirasse, heaume, H 1,9 m."""
    m = M()
    d = m.p("Wood_Dark")
    box(d, (-0.3, -0.3, 0.0), (0.3, 0.3, 0.08))
    box(d, (-0.04, -0.04, 0.08), (0.04, 0.04, 1.6))
    box(d, (-0.32, -0.04, 1.42), (0.32, 0.04, 1.5))
    lathe(m.p("Iron"), [(0.16, 0.95), (0.22, 1.05), (0.26, 1.3), (0.24, 1.48), (0.12, 1.55)], S(lod, 12))
    lathe(m.p("Iron"), [(0.12, 1.58), (0.13, 1.7), (0.12, 1.82), (0.06, 1.9), (0.001, 1.91)], S(lod, 10))
    if lod < 2:
        for s in (-1, 1):   # épaulières
            cyl(m.p("Iron"), (s * 0.2, 0, 1.45), (s * 0.36, 0, 1.4), 0.1, 0.08, S(lod, 8))
        lathe(m.p("Leather"), [(0.24, 0.82), (0.26, 0.95)], S(lod, 12))
    return m.out()


def p_dummy(spec, lod):
    """Mannequin d'entraînement (n° 130, remplace Dummy) : poteau, bras, torse de toile
    rembourrée, tête de sac."""
    m = M()
    d = m.p("Wood_Dark")
    box(d, (-0.3, -0.3, 0.0), (0.3, 0.3, 0.06))
    cyl(d, (0, 0, 0.0), (0, 0, 1.55), 0.06, 0.05, S(lod, 6))
    cyl(d, (-0.4, 0, 1.3), (0.4, 0, 1.3), 0.04, 0.04, S(lod, 6))
    lathe(m.p("Canvas"), [(0.12, 0.85), (0.24, 0.95), (0.27, 1.2), (0.24, 1.42), (0.12, 1.5)], S(lod, 10))
    lathe(m.p("Canvas"), [(0.08, 1.52), (0.13, 1.62), (0.12, 1.78), (0.04, 1.86)], S(lod, 8))
    if lod < 2:
        cyl(m.p("Rope"), (0, 0, 1.0), (0, 0, 1.05), 0.255, 0.255, S(lod, 10), cap=False)
    return m.out()


def p_desk_counter(spec, lod):
    """Bureau-comptoir à parchemins (n° 131), 2,5 x 0,8 m, H 1 m, casiers."""
    m = M()
    d = m.p("Wood_Dark")
    box(d, (-1.25, -0.4, 0.9), (1.25, 0.4, 1.0))
    box(d, (-1.25, 0.32, 0.0), (1.25, 0.4, 0.9))
    for x in (-1.25, 1.17):
        box(d, (x, -0.4, 0.0), (x + 0.08, 0.4, 0.9))
    if lod < 2:
        box(m.p("Wood_Plank"), (-1.17, -0.38, 0.05), (1.17, 0.32, 0.1))
        for k in range(5):
            x = -1.0 + 0.5 * k
            box(m.p("Paper"), (x - 0.15, -0.2, 1.0), (x + 0.15, 0.1, 1.005))
        cyl(m.p("Paper"), (0.6, -0.1, 1.04), (0.95, -0.1, 1.04), 0.035, 0.035, S(lod, 8))
    return m.out()


def p_frame_scaffold(spec, lod):
    """Bâti d'échafaudage à échelle (n° 132), 2 x 1,2 m, H 4 m : montants, plateaux, échelle."""
    m = M()
    d = m.p("Wood_Weathered")
    for x in (-1.0, 1.0):
        for y in (-0.6, 0.6):
            box(d, (x - 0.05, y - 0.05, 0.0), (x + 0.05, y + 0.05, 4.0))
    for z in (2.0, 3.9):
        plank_row(m.p("Wood_Plank"), -1.05, 1.05, -0.65, 0.65, z, z + 0.05, {0: 5, 1: 3, 2: 1}[lod], axis="x")
    if lod < 2:
        for y in (-0.6, 0.6):
            cyl(d, (-1.0, y, 0.2), (1.0, y, 1.9), 0.03, 0.03, 4, False)
        for x in (-0.7, -0.3):
            cyl(d, (x, -0.75, 0.0), (x, -0.75, 4.0), 0.03, 0.03, 4, False)
        for k in range(12):
            z = 0.3 + k * 0.32
            cyl(d, (-0.7, -0.75, z), (-0.3, -0.75, z), 0.02, 0.02, 4, False)
    return m.out()


def p_wagon_armored(spec, lod):
    """Chariot blindé nain (n° 133), 6 x 2,5 m, H 3,5 m : caisse bardée de plaques
    rivetées, toit en bâtière, meurtrières, quatre roues cerclées."""
    m = M()
    d = m.p("Wood_Dark")
    box(d, (-3.0, -1.1, 0.7), (3.0, 1.1, 0.9))
    box(m.p("Iron"), (-2.8, -1.15, 0.9), (2.8, 1.15, 2.7))
    r = m.p("Iron")
    for s in (-1, 1):
        _quad(r, (-2.9, s * 1.2, 2.7), (2.9, s * 1.2, 2.7), (2.9, 0.0, 3.5), (-2.9, 0.0, 3.5))
    for x in (-2.9, 2.9):
        j = len(r.v)
        r.v += [Vector((x, -1.2, 2.7)), Vector((x, 1.2, 2.7)), Vector((x, 0, 3.5))]
        r.f.append((j, j + 1, j + 2)); r.uv.append([(0, 0), (1, 0), (0.5, 1)]); r.cn.append(None)
    if lod < 2:
        for x in (-2.0, -0.7, 0.7, 2.0):
            for s in (-1, 1):
                box(d, (x - 0.2, s * 1.16 - 0.01, 1.9), (x + 0.2, s * 1.16 + 0.01, 2.0))   # meurtrières
        for z in (1.2, 2.4):
            for s in (-1, 1):
                box(m.p("Brass"), (-2.8, s * 1.17 - 0.01, z - 0.03), (2.8, s * 1.17 + 0.01, z + 0.03))
        box(d, (3.0, -0.08, 0.6), (4.0, 0.08, 0.75))   # timon
    for x in (-2.0, 2.0):
        for y in (-1.25, 1.25):
            wheel(m, (x, y, 0.65), 0.65, 0.2, lod, 8, axis="y")
    return m.out()


def p_stool_low(spec, lod):
    """Tabouret bas nain (n° 134), assise 0,35 m, trois pieds."""
    m = M()
    cyl(m.p("Wood_Plank"), (0, 0, 0.29), (0, 0, 0.35), 0.2, 0.2, S(lod, 12))
    for k in range(3):
        a = k * 2 * math.pi / 3
        cyl(m.p("Wood_Dark"), (math.cos(a) * 0.17, math.sin(a) * 0.17, 0.0), (math.cos(a) * 0.1, math.sin(a) * 0.1, 0.29), 0.025, 0.022, 5, False)
    return m.out()


def p_daybed(spec, lod):
    """Divan à coussins (n° 135), 2 x 0,9 m : châssis sculpté, matelas, dossier d'un bout."""
    m = M()
    d = m.p("Wood_Dark")
    box(d, (-1.0, -0.45, 0.15), (1.0, 0.45, 0.32))
    for x in (-0.9, 0.9):
        for y in (-0.35, 0.35):
            lathe_at(d, x, y, [(0.04, 0.0), (0.06, 0.08), (0.04, 0.15)], S(lod, 6))
    box(d, (-1.0, -0.45, 0.32), (-0.9, 0.45, 0.8))
    box(m.p("Cloth_Red"), (-0.9, -0.42, 0.32), (1.0, 0.42, 0.45))
    if lod < 2:
        for k, x in enumerate((-0.65, -0.3)):
            lathe_at(m.p("Cloth_Ochre" if k else "Cloth_Striped"), x, 0, [(0.001, 0.45), (0.2, 0.5), (0.22, 0.6), (0.001, 0.66)], S(lod, 8))
    return m.out()


def p_carafe(spec, lod):
    """Carafe de verre (n° 136), H 0,35 m."""
    m = M()
    lathe(m.p("Glass_Clear"), [(0.06, 0.0), (0.09, 0.05), (0.1, 0.14), (0.05, 0.24), (0.035, 0.31), (0.045, 0.35)], S(lod, 12))
    cyl(m.p("Glass_Clear"), (0, 0, 0.0), (0, 0, 0.0), 0.06, 0.06, S(lod, 12))
    return m.out()


def p_glass_flute(spec, lod):
    """Flûte de verre (n° 136), H 0,22 m."""
    m = M()
    lathe(m.p("Glass_Clear"), [(0.035, 0.0), (0.006, 0.01), (0.005, 0.09), (0.025, 0.12), (0.03, 0.22)], S(lod, 10))
    return m.out()


def p_bed_canopy(spec, lod):
    """Lit à baldaquin à colonnes torses (n° 137), 2,2 x 1,8 m, H 2,4 m."""
    m = M()
    d = m.p("Wood_Dark")
    box(d, (-0.9, -1.1, 0.2), (0.9, 1.1, 0.45))
    box(m.p("Cloth_White"), (-0.85, -1.05, 0.45), (0.85, 1.05, 0.62))
    box(m.p("Cloth_Red"), (-0.86, -1.06, 0.5), (0.86, 0.3, 0.64))
    box(d, (-0.9, 1.0, 0.45), (0.9, 1.1, 1.4))
    for x in (-0.85, 0.85):
        for y in (-1.05, 1.05):
            n = {0: 16, 1: 8, 2: 2}[lod]
            pts = [Vector((x + 0.025 * math.cos(k * 1.3), y + 0.025 * math.sin(k * 1.3), 2.4 * k / n)) for k in range(n + 1)]
            API["tube"](d, pts, [0.05] * (n + 1), S(lod, 6), 0.5)
    box(d, (-0.9, -1.1, 2.3), (0.9, 1.1, 2.4))
    c = m.p("Cloth_Red")
    for x in (-0.92, 0.92):
        _quad(c, (x, -1.1, 2.3), (x, 1.1, 2.3), (x, 1.1, 1.5), (x, -1.1, 1.7))
    _quad(c, (-0.92, 1.12, 2.3), (0.92, 1.12, 2.3), (0.92, 1.12, 1.45), (-0.92, 1.12, 1.45))
    return m.out()


def p_goblet_metal(spec, lod):
    """Gobelet de métal (n° 138), H 0,15 m."""
    m = M()
    lathe(m.p("Brass"), [(0.04, 0.0), (0.012, 0.02), (0.012, 0.06), (0.045, 0.09), (0.05, 0.15), (0.045, 0.15), (0.04, 0.095)], S(lod, 10))
    cyl(m.p("Brass"), (0, 0, 0.0), (0, 0, 0.0), 0.04, 0.04, S(lod, 10))
    return m.out()


def p_bowl_wood(spec, lod):
    """Écuelle de bois (n° 138), Ø 0,18 m."""
    m = M()
    lathe(m.p("Wood_Plank"), [(0.05, 0.0), (0.085, 0.03), (0.095, 0.07), (0.085, 0.07), (0.075, 0.035), (0.001, 0.02)], S(lod, 12))
    return m.out()


def p_food_platter(spec, lod):
    """Corbeille de fruits et de pains (n° 139), Ø 0,4 m."""
    m = M()
    lathe(m.p("Straw"), [(0.12, 0.0), (0.2, 0.06), (0.21, 0.1), (0.19, 0.1), (0.17, 0.065), (0.001, 0.02)], S(lod, 12))
    rng = random.Random(spec["seed"])
    for k in range({0: 7, 1: 4, 2: 2}[lod]):
        tex = ("Cloth_Red", "Cloth_Ochre", "Bone")[k % 3]
        x, y = rng.uniform(-0.1, 0.1), rng.uniform(-0.1, 0.1)
        r = rng.uniform(0.035, 0.05) if tex != "Bone" else 0.06
        lathe_at(m.p(tex), x, y, [(0.001, 0.05), (r, 0.05 + r), (0.001, 0.05 + 2 * r)], S(lod, 8))
    return m.out()


def p_pouch_large(spec, lod):
    """Bourse de cuir à gemmes (n° 142, remplace Pouch_Large) : sac noué, gemmes au pied."""
    m = M()
    lathe(m.p("Leather"), [(0.06, 0.0), (0.1, 0.04), (0.095, 0.09), (0.04, 0.125), (0.05, 0.16)], S(lod, 10))
    if lod < 2:
        cyl(m.p("Rope"), (0, 0, 0.12), (0, 0, 0.13), 0.045, 0.045, S(lod, 8), cap=False)
        for k, (x, y) in enumerate(((0.09, -0.03), (0.1, 0.03), (0.07, 0.05))):
            lathe_at(m.p(("Glass_Green", "Glass_Amber", "Glass_Clear")[k]), x, y, [(0.001, 0.0), (0.012, 0.01), (0.001, 0.02)], 5)
    return m.out()


# ====================================================================== T4
# Atelier décors, tome 4 (n° 143-169 ; 170 = Tent_Striped_1, fait au T1).
def p_flower_garden(spec, lod):
    """Massif de fleurs de jardin (n° 143) : _1 iris violets, _2 lis blancs ; H ~0,8 m."""
    m = M()
    p = m.p("Plant_Iris" if spec["id"].endswith("1") else "Plant_GardenLily", True)
    rng = random.Random(spec["seed"])
    n = {0: 6, 1: 4, 2: 2}[lod]
    for k in range(n):
        a = math.pi * k / n + rng.uniform(-0.2, 0.2)
        c = Vector((rng.uniform(-0.15, 0.15), rng.uniform(-0.15, 0.15), 0))
        dx = Vector((math.cos(a), math.sin(a), 0)) * 0.45
        h = rng.uniform(0.75, 0.9)
        _quad(p, c - dx, c + dx, c + dx + Vector((0, 0, h)), c - dx + Vector((0, 0, h)))
    return m.out()


def p_cypress_potted(spec, lod):
    """Cyprès taillé en bac (n° 144) : bac de terre cuite 0,8 m, fuseau de feuillage, H 2,5 m."""
    m = M()
    lathe(m.p("Ceramic"), [(0.32, 0.0), (0.4, 0.55), (0.44, 0.6), (0.42, 0.62), (0.36, 0.58)], S(lod, 12))
    cyl(m.p("Rock_Ochre"), (0, 0, 0.55), (0, 0, 0.55), 0.38, 0.38, S(lod, 12))
    cyl(m.p("Bark_Dark"), (0, 0, 0.5), (0, 0, 0.9), 0.05, 0.04, 5)
    lathe(m.p("Hedge"), [(0.001, 0.8), (0.32, 1.05), (0.38, 1.5), (0.3, 2.0), (0.14, 2.35), (0.001, 2.5)], S(lod, 12))
    return m.out()


def p_hedge(spec, lod):
    """Buis taillé (n° 145) : _1 boule Ø 1,5 m sur pied court, _2 module de haie 2 x 1,2 x 1,2 m."""
    m = M()
    h = m.p("Hedge")
    if spec["id"].endswith("1"):
        lathe(h, [(0.001, 0.0), (0.5, 0.08), (0.75, 0.75), (0.55, 1.35), (0.001, 1.5)], S(lod, 14))
    else:
        i0 = len(h.v)
        box(h, (-1.0, -0.6, 0.0), (1.0, 0.6, 1.2))
        _jitter(h, i0, 0.02, spec["seed"] % 7)
    return m.out()


def _statue_figure(m, base_z, H, lod):
    """Silhouette drapée neutre, sans visage (décision du propriétaire, Q18) : robe
    évasée, épaules, bras croisés, tête ovale ; hauteur H (m) depuis base_z."""
    s = m.p("Stone_Carved")
    k = H / 1.6
    lathe_at(s, 0, 0, [(0.32 * k, base_z), (0.28 * k, base_z + 0.25 * H), (0.21 * k, base_z + 0.55 * H),
                        (0.24 * k, base_z + 0.76 * H), (0.2 * k, base_z + 0.8 * H), (0.07 * k, base_z + 0.84 * H)], S(lod, 14))
    lathe_at(s, 0, 0, [(0.001, base_z + 0.84 * H), (0.1 * k, base_z + 0.89 * H), (0.105 * k, base_z + 0.95 * H),
                        (0.06 * k, base_z + 0.99 * H), (0.001, base_z + H)], S(lod, 10))
    if lod < 2:   # bras repliés sur la poitrine, voile tombant dans le dos
        cyl(s, (-0.23 * k, 0.0, base_z + 0.75 * H), (0.08 * k, -0.2 * k, base_z + 0.62 * H), 0.06 * k, 0.05 * k, 6)
        cyl(s, (0.23 * k, 0.0, base_z + 0.75 * H), (-0.08 * k, -0.2 * k, base_z + 0.65 * H), 0.06 * k, 0.05 * k, 6)
        box(s, (-0.16 * k, 0.12 * k, base_z + 0.35 * H), (0.16 * k, 0.2 * k, base_z + 0.92 * H))


def p_statue_garden(spec, lod):
    """Statue de jardin (n° 147) : _1 figure drapée sur colonne (H 4 m), _2 sur socle (H 2,6 m)."""
    m = M()
    s = m.p("Stone_Carved")
    if spec["id"].endswith("1"):
        box(s, (-0.4, -0.4, 0.0), (0.4, 0.4, 0.3))
        lathe(s, [(0.25, 0.3), (0.22, 0.45), (0.22, 2.1), (0.3, 2.3)], S(lod, 12))
        box(s, (-0.35, -0.35, 2.3), (0.35, 0.35, 2.4))
        _statue_figure(m, 2.4, 1.6, lod)
    else:
        box(s, (-0.45, -0.45, 0.0), (0.45, 0.45, 0.2))
        box(s, (-0.36, -0.36, 0.2), (0.36, 0.36, 0.95))
        box(s, (-0.42, -0.42, 0.95), (0.42, 0.42, 1.05))
        _statue_figure(m, 1.05, 1.55, lod)
    return m.out()


def p_urn_stone(spec, lod):
    """Urne de pierre sur socle (n° 148), H 1,6 m."""
    m = M()
    s = m.p("Stone_Carved")
    box(s, (-0.3, -0.3, 0.0), (0.3, 0.3, 0.7))
    box(s, (-0.34, -0.34, 0.7), (0.34, 0.34, 0.78))
    lathe(s, [(0.12, 0.78), (0.08, 0.86), (0.16, 0.92), (0.3, 1.15), (0.28, 1.35), (0.2, 1.45), (0.24, 1.52), (0.2, 1.55), (0.001, 1.53)], S(lod, 14))
    if lod < 2:
        for sgn in (-1, 1):
            pts = [Vector((sgn * (0.28 + 0.08 * math.sin(a)), 0, 1.2 + 0.18 * (1 - math.cos(a)) / 2 + 0.1 * math.sin(a))) for a in [k * math.pi / 5 for k in range(6)]]
            API["tube"](s, pts, [0.025] * 6, 5, 0.5)
    return m.out()


def p_gravestone(spec, lod):
    """Pierre tombale brute (n° 149) : _1 dalle dressée arrondie, _2 bloc fruste, _3 stèle
    haute fendue ; usées, moussues, base enfoncée de 0,3 m."""
    m = M()
    v = spec["id"][-1]
    s = m.p("Rock_Mossy" if v != "2" else "Rock_Grey")
    i0 = len(s.v)
    if v == "1":
        box(s, (-0.32, -0.09, -0.3), (0.32, 0.09, 0.7))
        cyl(s, (0, -0.09, 0.7), (0, 0.09, 0.7), 0.32, 0.32, S(lod, 10))
    elif v == "2":
        box(s, (-0.35, -0.2, -0.3), (0.35, 0.2, 0.55))
    else:
        box(s, (-0.25, -0.1, -0.3), (0.25, 0.1, 1.2))
        box(s, (-0.2, -0.1, 1.2), (0.2, 0.1, 1.45))
    _jitter(s, i0, 0.02, spec["seed"] % 11)
    tilt = Matrix.Rotation(math.radians(random.Random(spec["seed"]).uniform(-8, 8)), 4, "X")
    for k in range(i0, len(s.v)):
        q = tilt @ s.v[k]
        s.v[k] = Vector((q.x, q.y, max(q.z, -0.3)))
    return m.out()


def p_fence_iron(spec, lod):
    """Grille de fer forgé à piques (n° 150), module 2 m ; _Ornament : volutes ; remplace
    Prop_MetalFence_* (même boîte) ou module neuf Fence_Iron_Spear_1 (H 2 m)."""
    m = M()
    i = m.p("Iron")
    if "bbox_ref" in spec:
        H = spec["bbox_ref"][3]
    else:
        H = 2.0
    box(i, (-0.97, -0.03, 0.1), (0.97, 0.03, 0.16))
    box(i, (-0.97, -0.03, H * 0.82), (0.97, 0.03, H * 0.82 + 0.06))
    for x in (-0.97, 0.97):
        box(m.p("Stone_Carved") if x < 0 else i, (x - 0.05, -0.06, 0.0), (x + 0.05, 0.06, H * 0.9))
    nb = {0: 9, 1: 5, 2: 3}[lod]
    for k in range(nb):
        x = -0.8 + 1.6 * k / (nb - 1)
        cyl(i, (x, 0, 0.1), (x, 0, H * 0.94), 0.012, 0.012, S(lod, 6), False)
        if lod < 2:
            cyl(i, (x, 0, H * 0.94), (x, 0, H), 0.03, 0.001, 4)
    if lod < 2 and spec["id"].endswith("Ornament"):
        for k in range(nb - 1):
            x = -0.8 + 1.6 * (k + 0.5) / (nb - 1)
            pts = [Vector((x + 0.06 * math.cos(a), 0, H * 0.5 + 0.08 * math.sin(a) + 0.02 * a)) for a in [j * math.pi / 3 for j in range(8)]]
            API["tube"](i, pts, [0.008] * 8, 4, 0.5)
    return m.out()


def p_stall_tent_market(spec, lod):
    """Tente de marché à auvent frangé sur mâts rayés (n° 151), 3 x 3 m, mât 4 m ;
    _1 rouge, _2 rayée, _3 bleue."""
    m = M()
    tex = {"1": "Cloth_Red", "2": "Cloth_Striped", "3": "Cloth_Blue"}[spec["id"][-1]]
    c = m.p(tex)
    for x in (-1.5, 1.5):
        for y in (-1.5, 1.5):
            cyl(m.p("Wood_Plank") if (x > 0) == (y > 0) else m.p("Cloth_White"), (x, y, 0.0), (x, y, 2.6), 0.05, 0.045, S(lod, 6))
    cyl(m.p("Wood_Dark"), (0, 0, 2.6), (0, 0, 4.0), 0.06, 0.05, S(lod, 6))
    apex = Vector((0, 0, 3.8))
    cs = [Vector((-1.6, -1.6, 2.6)), Vector((1.6, -1.6, 2.6)), Vector((1.6, 1.6, 2.6)), Vector((-1.6, 1.6, 2.6))]
    for k in range(4):
        a, b = cs[k], cs[(k + 1) % 4]
        j = len(c.v)
        c.v += [a, b, apex]
        c.f.append((j, j + 1, j + 2)); c.uv.append([(0, 0), (1, 0), (0.5, 1)]); c.cn.append(None)
        nf = {0: 6, 1: 3, 2: 1}[lod]   # lambrequin frangé
        for q in range(nf):
            p0, p1 = a.lerp(b, q / nf), a.lerp(b, (q + 1) / nf)
            j = len(c.v)
            c.v += [p0, p1, (p0 + p1) / 2 - Vector((0, 0, 0.35))]
            c.f.append((j, j + 1, j + 2)); c.uv.append([(0, 1), (1, 1), (0.5, 0)]); c.cn.append(None)
    box(m.p("Wood_Plank"), (-1.2, -1.45, 0.8), (1.2, -0.9, 0.86))
    return m.out()


def p_cage_poultry(spec, lod):
    """Cage à volailles d'osier (n° 152), 0,8 x 0,5 x 0,45 m."""
    m = M()
    s = m.p("Straw")
    box(s, (-0.4, -0.25, 0.0), (0.4, 0.25, 0.04))
    box(s, (-0.4, -0.25, 0.41), (0.4, 0.25, 0.45))
    nb = {0: 10, 1: 6, 2: 3}[lod]
    for k in range(nb):
        x = -0.38 + 0.76 * k / (nb - 1)
        for y in (-0.24, 0.24):
            cyl(s, (x, y, 0.04), (x, y, 0.41), 0.01, 0.01, 4, False)
    for x in (-0.39, 0.39):
        for k in range(4 if lod < 2 else 2):
            y = -0.2 + 0.4 * k / 3
            cyl(s, (x, y, 0.04), (x, y, 0.41), 0.01, 0.01, 4, False)
    return m.out()


def p_cloth_rolls(spec, lod):
    """Rouleaux d'étoffe de drapier sur chevalet (n° 153), 1,2 m."""
    m = M()
    d = m.p("Wood_Dark")
    for x in (-0.55, 0.55):
        cyl(d, (x, -0.3, 0.0), (x, 0.0, 0.9), 0.025, 0.025, 4, False)
        cyl(d, (x, 0.3, 0.0), (x, 0.0, 0.9), 0.025, 0.025, 4, False)
    for k, (tex, y, z) in enumerate((("Cloth_Red", -0.13, 0.82), ("Cloth_Blue", 0.13, 0.82), ("Cloth_Ochre", 0.0, 0.98),
                                       ("Cloth_White", -0.12, 1.12), ("Cloth_Striped", 0.12, 1.12))[:{0: 5, 1: 3, 2: 2}[lod]]):
        cyl(m.p(tex), (-0.6, y, z), (0.6, y, z), 0.09, 0.09, S(lod, 10))
    return m.out()


def p_gate_city_iron(spec, lod):
    """Porte de ville à deux vantaux ouverts (n° 154) : vantaux de chêne 2,2 x 4,2 m à
    pentures en croix et tirants rivetés, ouverts à 90° vers l'intérieur (+Z glTF = -Y)."""
    m = M()
    w, i = m.p("Wood_Dark"), m.p("Iron")
    for sgn in (-1, 1):
        x = sgn * 2.35
        box(w, (x - 0.08, -2.25, 0.0), (x + 0.08, -0.05, 4.2))
        for z in (0.6, 2.1, 3.6):
            box(i, (x - 0.1, -2.2, z - 0.05), (x + 0.1, -0.1, z + 0.05))
        if lod < 2:
            pts = [Vector((x - 0.1 * sgn, -0.15, 0.4)), Vector((x - 0.1 * sgn, -2.1, 3.8))]
            cyl(i, pts[0], pts[1], 0.04, 0.04, 4, False)
            cyl(i, Vector((x - 0.1 * sgn, -2.1, 0.4)), Vector((x - 0.1 * sgn, -0.15, 3.8)), 0.04, 0.04, 4, False)
            if lod == 0:
                for z in (0.6, 2.1, 3.6):
                    for yy in (-0.4, -1.1, -1.8):
                        cyl(i, (x - 0.11 * sgn, yy, z), (x - 0.14 * sgn, yy, z), 0.025, 0.02, 5)
        cyl(i, (x, -0.02, 0.2), (x, -0.02, 4.0), 0.05, 0.05, S(lod, 6))
    return m.out()


def p_gallery_wood(spec, lod):
    """Galerie de bois en encorbellement (n° 155), module 2 m : plancher 1,2 m sur
    aisseliers, garde-corps à balustres ; dos en y = 0, plancher à z = 3."""
    m = M()
    d = m.p("Wood_Dark")
    plank_row(m.p("Wood_Plank"), -1.0, 1.0, -1.2, 0.0, 2.92, 3.0, {0: 6, 1: 3, 2: 1}[lod], axis="x")
    for x in (-0.9, 0.9):
        box(d, (x - 0.07, -1.2, 2.78), (x + 0.07, 0.0, 2.92))
        cyl(d, (x, 0.0, 1.9), (x, -0.95, 2.8), 0.05, 0.05, 4, False)
        box(d, (x - 0.06, -1.18, 3.0), (x + 0.06, -1.06, 4.0))
    box(d, (-1.0, -1.18, 3.95), (1.0, -1.06, 4.05))
    nb = {0: 9, 1: 5, 2: 2}[lod]
    for k in range(nb):
        x = -0.8 + 1.6 * k / max(1, nb - 1)
        box(d, (x - 0.025, -1.14, 3.0), (x + 0.025, -1.1, 3.95))
    return m.out()


def p_stairs_gallery(spec, lod):
    """Escalier extérieur de bois montant à une galerie (n° 155) : 1 m de large, 12 marches
    de 0,25 m, limons et rampe ; départ en y = 0 vers +Y."""
    m = M()
    d, w = m.p("Wood_Dark"), m.p("Wood_Plank")
    for k in range(12):
        z = (k + 1) * 0.25
        box(w, (-0.5, k * 0.3, z - 0.05), (0.5, k * 0.3 + 0.32, z))
    for x in (-0.55, 0.5):
        cyl(d, (x + 0.025, 0.0, 0.05), (x + 0.025, 3.6, 3.1), 0.05, 0.05, 4, False)
    if lod < 2:
        cyl(d, (0.55, 0.0, 1.0), (0.55, 3.6, 3.95), 0.03, 0.03, 4, False)
        for k in range(0, 12, 3):
            cyl(d, (0.55, k * 0.3, (k + 1) * 0.25), (0.55, k * 0.3, (k + 1) * 0.25 + 0.95), 0.025, 0.025, 4, False)
    return m.out()


def p_window_leaded(spec, lod):
    """Fenêtre à vitraux plombés (n° 157), 0,8 x 1,4 m : _1 losanges, _2 petits carreaux ;
    dos en y = 0, appui à z = 0."""
    m = M()
    s = m.p("Stone_Carved")
    box(s, (-0.5, -0.12, 0.0), (0.5, 0.0, 0.1))
    box(s, (-0.5, -0.12, 1.5), (0.5, 0.0, 1.62))
    for x in (-0.5, 0.4):
        box(s, (x, -0.12, 0.1), (x + 0.1, 0.0, 1.5))
    box(m.p("Glass_Green" if spec["id"].endswith("1") else "Glass_Amber"), (-0.4, -0.05, 0.1), (0.4, -0.04, 1.5))
    if lod < 2:
        i = m.p("Iron")
        if spec["id"].endswith("1"):   # résille de losanges dans le cadre
            for ix in range(4):
                for iz in range(7):
                    cx, cz = -0.3 + 0.2 * ix, 0.2 + 0.2 * iz
                    pts = [(cx, cz - 0.1), (cx + 0.1, cz), (cx, cz + 0.1), (cx - 0.1, cz)]
                    for q in range(4):
                        a, b = pts[q], pts[(q + 1) % 4]
                        cyl(i, (a[0], -0.06, a[1]), (b[0], -0.06, b[1]), 0.006, 0.006, 3, False)
        else:
            for k in range(1, 4):
                cyl(i, (-0.4 + 0.2 * k, -0.06, 0.1), (-0.4 + 0.2 * k, -0.06, 1.5), 0.008, 0.008, 3, False)
            for k in range(1, 7):
                cyl(i, (-0.4, -0.06, 0.1 + 0.2 * k), (0.4, -0.06, 0.1 + 0.2 * k), 0.008, 0.008, 3, False)
    return m.out()


def p_stalls_gothic(spec, lod):
    """Stalle gothique de bois sculpté à pinacles (n° 158), module 1 x 0,7 x 3 m."""
    m = M()
    d = m.p("Wood_Dark")
    box(d, (-0.5, -0.35, 0.0), (0.5, 0.35, 0.1))
    box(d, (-0.45, -0.3, 0.42), (0.45, 0.3, 0.48))
    box(d, (-0.5, 0.28, 0.1), (0.5, 0.35, 2.4))
    for x in (-0.5, 0.45):
        box(d, (x, -0.35, 0.1), (x + 0.05, 0.35, 1.0))
    box(d, (-0.5, -0.35, 2.4), (0.5, 0.35, 2.5))
    if lod < 2:
        for x in (-0.45, 0.0, 0.45):
            lathe_at(d, x, -0.3, [(0.06, 2.5), (0.05, 2.7), (0.001, 3.0)], 4)
        _arch_band(d, -0.4, 0.4, 1.8, 0.45, 0.06, 0.06, lod, y0=(-0.33, -0.27))
    return m.out()


def p_ruin_wall(spec, lod):
    """Ruine (n° 159) : _1 pan de mur de brique effondré (H 6 m), _2 arc brisé, _3 frise
    sculptée tombée (bloc couché)."""
    m = M()
    v = spec["id"][-1]
    rng = random.Random(spec["seed"])
    if v == "1":
        b = m.p("Brick_Red")
        n = 8
        for k in range(n):
            x0, x1 = -2.0 + 4.0 * k / n, -2.0 + 4.0 * (k + 1) / n
            top = 6.0 * (1 - abs((k + 0.5) / n - 0.35) * 1.4) + rng.uniform(-0.6, 0.6)
            box(b, (x0, -0.3, 0.0), (x1, 0.3, max(0.8, top)))
        if lod < 2:
            for k in range(6):   # briques tombées au pied du pan
                cx, cy = rng.uniform(-2.2, 2.2), rng.uniform(-1.6, -0.6)
                box(b, (cx - 0.25, cy - 0.12, 0.0), (cx + 0.25, cy + 0.12, rng.uniform(0.15, 0.3)))
    elif v == "2":
        s = m.p("Stone_Wall")
        box(s, (-2.6, -0.5, 0.0), (-1.8, 0.5, 2.6))   # pile effondrée
        box(s, (1.8, -0.5, 0.0), (2.6, 0.5, 4.5))     # pile portant le reste de l'arc
        # arc interrompu : 60 % des claveaux, depuis la pile gauche
        f0 = len(s.f)
        _arch_band(s, -1.8, 1.8, 4.5, 1.8, 1.0, 0.45, lod)
        n = {0: 14, 1: 8, 2: 5}[lod]
        keep = f0 + int(n * 0.6) * 4
        del s.f[keep:], s.uv[keep:], s.cn[keep:]
    else:
        s = m.p("Stone_Carved")
        box(s, (-1.4, -0.45, 0.0), (1.4, 0.45, 0.6))
        if lod < 2:   # frise : rang de rosettes en relief
            for k in range(5):
                x = -1.1 + 0.55 * k
                cyl(s, (x, -0.46, 0.3), (x, -0.52, 0.3), 0.16, 0.12, S(lod, 8))
    return m.out()


def p_tea_set(spec, lod):
    """Service de table (n° 160) : théière, quatre tasses, plat de gâteaux, sur plateau 0,6 m."""
    m = M()
    cer = m.p("Ceramic")
    box(m.p("Brass"), (-0.3, -0.22, 0.0), (0.3, 0.22, 0.02))
    lathe_at(cer, -0.12, 0.0, [(0.05, 0.02), (0.1, 0.08), (0.095, 0.16), (0.04, 0.2), (0.05, 0.22), (0.001, 0.25)], S(lod, 12))
    if lod < 2:
        cyl(cer, (-0.03, 0, 0.1), (0.02, 0, 0.19), 0.015, 0.01, 5)
    for k, (x, y) in enumerate(((0.08, -0.12), (0.18, -0.12), (0.08, 0.12), (0.18, 0.12))):
        lathe_at(cer, x, y, [(0.025, 0.02), (0.04, 0.06), (0.038, 0.06), (0.02, 0.025)], S(lod, 8))
    lathe_at(m.p("Bone"), 0.0, 0.14, [(0.001, 0.02), (0.08, 0.025), (0.06, 0.05), (0.001, 0.06)], S(lod, 10))
    return m.out()


def p_chair_gothic(spec, lod):
    """Fauteuil gothique à haut dossier (n° 161), H 1,8 m, assise 0,45 m, pinacles."""
    m = M()
    d = m.p("Wood_Dark")
    for x in (-0.28, 0.28):
        box(d, (x - 0.04, -0.25, 0.0), (x + 0.04, -0.17, 0.66))
        box(d, (x - 0.04, 0.2, 0.0), (x + 0.04, 0.28, 1.7))
        box(d, (x - 0.04, -0.27, 0.62), (x + 0.04, 0.28, 0.68))
        if lod < 2:
            lathe_at(d, x, 0.24, [(0.04, 1.7), (0.03, 1.75), (0.001, 1.82)], 4)
    box(m.p("Cloth_Red"), (-0.3, -0.25, 0.42), (0.3, 0.24, 0.5))
    box(d, (-0.26, 0.22, 0.5), (0.26, 0.26, 1.62))
    if lod < 2:
        _arch_band(d, -0.2, 0.2, 1.25, 0.32, 0.04, 0.04, lod, y0=(0.19, 0.22))
    return m.out()


def p_table_carved_long(spec, lod):
    """Longue table sculptée à piètement en lyre (n° 162), 4 x 1 m, H 0,78 m."""
    m = M()
    d = m.p("Wood_Dark")
    box(d, (-2.0, -0.5, 0.7), (2.0, 0.5, 0.78))
    for x in (-1.6, 0.0, 1.6):
        box(d, (x - 0.35, -0.08, 0.0), (x + 0.35, 0.08, 0.08))
        lathe_at(d, x, 0, [(0.08, 0.08), (0.14, 0.2), (0.07, 0.38), (0.12, 0.55), (0.08, 0.7)], S(lod, 8))
    box(d, (-1.6, -0.04, 0.12), (1.6, 0.04, 0.2))
    return m.out()


def p_sconce_candle(spec, lod):
    """Applique murale à cinq bougies (n° 163), 0,6 m ; dos en y = 0, base à z = 0."""
    m = M()
    i = m.p("Brass")
    box(i, (-0.05, -0.03, 0.0), (0.05, 0.0, 0.3))
    cyl(i, (0, -0.03, 0.15), (0, -0.2, 0.15), 0.015, 0.015, 5)
    for k in range(5):
        x = -0.26 + 0.13 * k
        z = 0.22 + 0.08 * (1 - abs(k - 2) / 2)
        cyl(i, (0, -0.2, 0.15), (x, -0.2, z), 0.01, 0.01, 4, False)
        cyl(i, (x, -0.2, z), (x, -0.2, z + 0.02), 0.035, 0.03, 6)
        candle(m, Vector((x, -0.2, z + 0.02)), 0.12, 0.016, lod)
    return m.out()


def p_quill_inkwell(spec, lod):
    """Plume et encrier (n° 164), 0,3 m."""
    m = M()
    lathe(m.p("Glass_Clear"), [(0.035, 0.0), (0.04, 0.04), (0.02, 0.06), (0.022, 0.07)], S(lod, 10))
    cyl(m.p("Wood_Dark"), (0, 0, 0.0), (0, 0, 0.0), 0.035, 0.035, S(lod, 10))
    p = m.p("Paper")
    _quad(p, (0.0, 0.0, 0.06), (0.04, 0.0, 0.32), (0.07, 0.0, 0.3), (0.012, 0.0, 0.06))
    return m.out()


def p_mirror_standing(spec, lod):
    """Grand miroir sur pied à cadre sculpté (n° 165), H 2 m."""
    m = M()
    d = m.p("Wood_Dark")
    for x in (-0.4, 0.4):
        box(d, (x - 0.04, -0.3, 0.0), (x + 0.04, 0.3, 0.06))
        box(d, (x - 0.04, -0.04, 0.06), (x + 0.04, 0.04, 1.85))
        lathe_at(d, x, 0, [(0.04, 1.85), (0.06, 1.9), (0.001, 2.0)], 6)
    box(d, (-0.36, -0.05, 0.45), (0.36, 0.05, 0.52))
    box(d, (-0.36, -0.05, 1.75), (0.36, 0.05, 1.82))
    for x in (-0.36, 0.3):
        box(d, (x, -0.05, 0.52), (x + 0.06, 0.05, 1.75))
    box(m.p("Mirror"), (-0.3, -0.03, 0.52), (0.3, -0.02, 1.75))
    return m.out()


def p_sign_tavern_skull(spec, lod):
    """Enseigne d'auberge à crâne d'aurochs sur potence à volutes (n° 166), 1,5 x 1 m ;
    dos en y = 0 (façade), potence à z = 3."""
    m = M()
    i = m.p("Iron")
    box(i, (-0.05, -0.04, 2.4), (0.05, 0.0, 3.3))
    cyl(i, (0, -0.02, 3.2), (0, -1.5, 3.2), 0.025, 0.025, 5)
    if lod < 2:
        pts = [Vector((0, -0.02 - 0.5 * t, 2.55 + 0.55 * t + 0.15 * math.sin(t * 6))) for t in [k / 8 for k in range(9)]]
        API["tube"](i, pts, [0.015] * 9, 4, 0.5)
    b = m.p("Bone")
    lathe_at(b, 0, -1.0, [(0.001, 2.35), (0.12, 2.42), (0.2, 2.6), (0.18, 2.85), (0.001, 2.95)], S(lod, 10))
    for sgn in (-1, 1):
        pts = [Vector((sgn * (0.15 + 0.35 * t), -1.0, 2.8 + 0.25 * math.sin(t * 2.2))) for t in [k / 6 for k in range(7)]]
        API["tube"](b, pts, [0.06 * (1 - 0.8 * t) + 0.01 for t in [k / 6 for k in range(7)]], 6, 0.5, True)
    for y in (-0.75, -1.25):
        cyl(i, (0, y, 3.2), (0, y, 2.95), 0.006, 0.006, 3, False)
    for prt in m.parts.values():   # origine au point bas (le crâne), pose en façade par yOffset
        prt.v = [v - Vector((0, 0, 2.35)) for v in prt.v]
    return m.out()


def p_carpet_pattern(spec, lod):
    """Tapis à motifs losangés (n° 167), 4 x 8 m."""
    m = M()
    box(m.p("Carpet_Diamond"), (-2.0, -4.0, 0.0), (2.0, 4.0, 0.02), uv=0.125)
    return m.out()


def p_fireplace_mantel(spec, lod):
    """Cheminée à manteau (n° 168), 2 x 1,6 m : jambages, linteau mouluré, tablette, foyer."""
    m = M()
    s = m.p("Stone_Carved")
    for x in (-1.0, 0.65):
        box(s, (x, -0.45, 0.0), (x + 0.35, 0.0, 1.25))
    box(s, (-1.0, -0.45, 1.25), (1.0, 0.0, 1.5))
    box(s, (-1.1, -0.55, 1.5), (1.1, 0.0, 1.6))
    box(m.p("Stone_Wall"), (-0.65, -0.05, 0.0), (0.65, 0.0, 1.25))
    box(m.p("Rock_Red"), (-0.55, -0.4, 0.0), (0.55, -0.05, 0.03))
    if lod < 2:
        for k in range(3):
            cyl(m.p("Bark_Dark"), (-0.4 + 0.1 * k, -0.35 + 0.08 * k, 0.08), (0.4 - 0.1 * k, -0.2, 0.08 + 0.06 * k), 0.06, 0.06, 6)
    return m.out()


def p_watchpost_wood(spec, lod):
    """Guérite de bois carrée de camp (n° 169), 2 x 2 m, H 4 m : pilotis, plancher à 2 m,
    parapet de planches, toit à quatre pans, échelle ; sans croix."""
    m = M()
    d, w = m.p("Wood_Dark"), m.p("Wood_Plank")
    for x in (-0.95, 0.95):
        for y in (-0.95, 0.95):
            box(d, (x - 0.07, y - 0.07, 0.0), (x + 0.07, y + 0.07, 3.3))
    plank_row(w, -1.0, 1.0, -1.0, 1.0, 2.0, 2.06, {0: 5, 1: 3, 2: 1}[lod], axis="x")
    for (a, b) in (((-1.0, -1.0), (1.0, -0.94)), ((-1.0, 0.94), (1.0, 1.0)), ((-1.0, -1.0), (-0.94, 1.0)), ((0.94, -1.0), (1.0, 1.0))):
        box(w, (a[0], a[1], 2.06), (b[0], b[1], 2.95))
    r = m.p("Shingle")
    apex = Vector((0, 0, 4.0))
    cs = [Vector((-1.25, -1.25, 3.3)), Vector((1.25, -1.25, 3.3)), Vector((1.25, 1.25, 3.3)), Vector((-1.25, 1.25, 3.3))]
    for k in range(4):
        a, b = cs[k], cs[(k + 1) % 4]
        j = len(r.v)
        r.v += [a, b, apex]
        r.f.append((j, j + 1, j + 2)); r.uv.append([(0, 0), (2, 0), (1, 1)]); r.cn.append(None)
    if lod < 2:
        for x in (-0.25, 0.25):
            cyl(d, (x, -1.6, 0.0), (x, -1.0, 2.1), 0.03, 0.03, 4, False)
        for k in range(6):
            z = 0.3 + 0.3 * k
            yy = -1.6 + 0.6 * z / 2.1
            cyl(d, (-0.25, yy, z), (0.25, yy, z), 0.02, 0.02, 4, False)
    return m.out()


def p_gazebo(spec, lod):
    """Kiosque-rotonde à colonnes et coupole (n° 146), Ø 6,6 m, H ~7,2 m."""
    m = M()
    s = m.p("Stone_Carved")
    n = S(lod, 20)
    lathe(s, [(3.3, 0.0), (3.3, 0.3), (3.1, 0.3), (3.1, 0.5), (0.001, 0.5)], n)
    for k in range(8):
        a = k * math.pi / 4
        x, y = 2.75 * math.cos(a), 2.75 * math.sin(a)
        lathe_at(s, x, y, [(0.22, 0.5), (0.18, 0.62), (0.16, 4.3), (0.24, 4.5)], S(lod, 10))
    cyl(s, (0, 0, 4.5), (0, 0, 5.0), 3.1, 3.1, n, cap=False)
    cyl(s, (0, 0, 4.5), (0, 0, 5.0), 2.6, 2.6, n, cap=False)
    lathe(m.p("Shingle_Slate"), [(3.25, 5.0), (3.0, 5.6), (2.4, 6.3), (1.4, 6.85), (0.3, 7.05), (0.001, 7.08)], n)
    lathe(s, [(0.001, 4.98), (2.6, 4.98)], n)
    if lod < 2:
        lathe(m.p("Brass"), [(0.08, 7.05), (0.12, 7.15), (0.001, 7.3)], 6)
    return m.out()


# ====================================================================== T5
# Atelier décors, tome 5 (n° 171-185).
def p_log_fallen(spec, lod):
    """Tronc abattu couché (n° 172) : _1 8 m, _2 11 m, Ø ~0,6 m, chicots de branches,
    bout cassé ; légèrement enfoncé."""
    m = M()
    L = 8.0 if spec["id"].endswith("1") else 11.0
    rng = random.Random(spec["seed"])
    b = m.p("Bark_Dark")
    pts = [Vector((-L / 2 + L * k / 6, rng.uniform(-0.12, 0.12), 0.28 - 0.06 * k / 6)) for k in range(7)]
    API["tube"](b, pts, [0.34 - 0.12 * k / 6 for k in range(7)], S(lod, 10), 0.5, True)
    if lod < 2:
        for k in range(4):
            x = -L / 2 + L * (0.25 + 0.17 * k)
            a = rng.uniform(0, 6.28)
            d = Vector((rng.uniform(-0.3, 0.3), math.cos(a), abs(math.sin(a)) + 0.2)).normalized()
            p0 = Vector((x, 0, 0.3))
            cyl(b, p0, p0 + d * rng.uniform(0.6, 1.3), 0.08, 0.03, 5)
    return m.out()


def p_stump(spec, lod):
    """Souche à racines (n° 172), Ø ~0,9 m, H 0,6 m, dessus scié et grisé."""
    m = M()
    b = m.p("Bark_Dark")
    cyl(b, (0, 0, -0.1), (0, 0, 0.6), 0.42, 0.36, S(lod, 12), cap=False)
    cyl(m.p("Wood_Weathered"), (0, 0, 0.6), (0, 0, 0.6), 0.36, 0.36, S(lod, 12))
    for k in range({0: 6, 1: 4, 2: 0}[lod]):
        a = k * 2 * math.pi / 6 + 0.3
        d = Vector((math.cos(a), math.sin(a), 0))
        API["tube"](b, [d * 0.3 + Vector((0, 0, 0.3)), d * 0.75 + Vector((0, 0, 0.05)), d * 1.0 + Vector((0, 0, -0.05))],
                    [0.14, 0.08, 0.03], 5, 0.5, True)
    return m.out()


def p_pebble_river(spec, lod):
    """Galets de lit de ruisseau moussus (n° 173) : grappe de 4 à 9 galets, 0,2-0,8 m."""
    m = M()
    rng = random.Random(spec["seed"])
    n = {0: 9, 1: 5, 2: 3}[lod]
    big = 1.0 + 0.4 * (int(spec["id"][-1]) - 1)
    for k in range(n):
        tex = "Rock_Mossy" if rng.random() < 0.5 else "Rock_Grey"
        c = Vector((rng.uniform(-0.6, 0.6) * big, rng.uniform(-0.5, 0.5) * big, 0))
        r = rng.uniform(0.12, 0.35) * big
        _stone(m.p(tex), c, r, r * rng.uniform(0.45, 0.7), k + spec["seed"], lod)
    return m.out()


def p_hut_cone(spec, lod):
    """Hutte conique orkh de perches et de chaume (n° 174), Ø 4 m, H 5 m, porte basse."""
    m = M()
    n = S(lod, 14)
    lathe(m.p("Thatch"), [(2.05, 0.0), (1.9, 0.6), (1.3, 2.2), (0.6, 3.8), (0.12, 4.6), (0.001, 4.7)], n)
    d = m.p("Wood_Dark")
    for k in range({0: 6, 1: 3, 2: 0}[lod]):   # perches dépassant de la pointe
        a = k * 2 * math.pi / 6
        cyl(d, (math.cos(a) * 0.15, math.sin(a) * 0.15, 4.2), (math.cos(a) * 0.45, math.sin(a) * 0.45, 5.1), 0.04, 0.03, 4, False)
    # porte : encadrement de perches et peau tendue sur le flanc -Y
    box(m.p("Fur"), (-0.45, -2.0, 0.0), (0.45, -1.9, 1.5))
    for x in (-0.5, 0.5):
        cyl(d, (x, -2.0, 0.0), (x * 0.8, -1.75, 1.7), 0.05, 0.05, 4, False)
    if lod < 2:
        cyl(m.p("Rope"), (0, 0, 1.2), (0, 0, 1.25), 1.72, 1.72, n, cap=False)
    return m.out()


def p_chimney_drystone(spec, lod):
    """Cheminée de pierres sèches isolée, reste d'une hutte brûlée (n° 175), H 4,5 m,
    base 1,2 m, sommet ébréché."""
    m = M()
    s = m.p("Stone_Wall")
    i0 = len(s.v)
    box(s, (-0.6, -0.6, 0.0), (0.6, 0.6, 1.4))
    box(s, (-0.45, -0.45, 1.4), (0.45, 0.45, 3.6))
    box(s, (-0.38, -0.38, 3.6), (0.3, 0.38, 4.5))
    _jitter(s, i0, 0.04, spec["seed"] % 7)
    for k in range(i0, len(s.v)):
        s.v[k] = Vector((s.v[k].x, s.v[k].y, max(s.v[k].z, 0.0)))
    box(m.p("Rock_Red"), (-0.35, -0.62, 0.0), (0.35, -0.55, 0.8))   # âtre noirci
    if lod < 2:
        rng = random.Random(spec["seed"])
        for k in range(5):
            _stone(m.p("Rock_Grey"), Vector((rng.uniform(-1.4, 1.4), rng.uniform(-1.4, -0.7), 0)), 0.2, 0.15, k, lod)
    return m.out()


def p_stakes_chevaux(spec, lod):
    """Chevaux de frise (n° 176) : module 4 m de pieux géants (5-6 m) en X, liés de cordes."""
    m = M()
    d = m.p("Wood_Weathered")
    cyl(d, (-2.0, 0, 0.6), (2.0, 0, 0.6), 0.18, 0.18, S(lod, 8))
    for k in range(4):
        x = -1.5 + k
        for sgn in (-1, 1):
            a = math.radians(38)
            tip = Vector((x, sgn * math.cos(a) * 2.8, 0.6 + math.sin(a) * 2.8))
            base = Vector((x, -sgn * math.cos(a) * 0.6, 0.0))
            cyl(d, base, tip, 0.1, 0.02, S(lod, 6))
    if lod < 2:
        for k in range(4):
            x = -1.5 + k
            cyl(m.p("Rope"), (x - 0.12, 0, 0.6), (x + 0.12, 0, 0.6), 0.2, 0.2, S(lod, 8), cap=False)
    return m.out()


def p_ballista(spec, lod):
    """Baliste sur affût à roues (n° 177), 4 x 3 m, arc de 4 m, carreau engagé."""
    m = M()
    d = m.p("Wood_Dark")
    box(d, (-0.9, -1.6, 0.5), (0.9, 1.6, 0.7))
    for x in (-1.0, 1.0):
        for y in (-1.1, 1.1):
            wheel(m, (x, y, 0.5), 0.5, 0.15, lod, 6, axis="x")
    cyl(d, (0, 0, 0.7), (0, 0, 1.4), 0.15, 0.12, S(lod, 6))
    box(d, (-0.15, -1.8, 1.4), (0.15, 1.6, 1.6))                    # fût
    pts = [Vector((2.0 * math.sin(t), 1.1 - 0.5 * math.cos(t) ** 2, 1.55)) for t in [(k / 8 - 0.5) * 2.2 for k in range(9)]]
    API["tube"](d, pts, [0.06, 0.08, 0.1, 0.12, 0.13, 0.12, 0.1, 0.08, 0.06], S(lod, 6), 0.5)
    if lod < 2:
        cyl(m.p("Rope"), pts[0], Vector((0, -0.6, 1.62)), 0.015, 0.015, 3, False)
        cyl(m.p("Rope"), pts[-1], Vector((0, -0.6, 1.62)), 0.015, 0.015, 3, False)
        cyl(m.p("Iron"), (0, -0.6, 1.66), (0, 2.1, 1.66), 0.03, 0.03, 5)
        cyl(m.p("Iron"), (0, 2.1, 1.66), (0, 2.4, 1.66), 0.06, 0.001, 5)
    return m.out()


def p_war_drum(spec, lod):
    """Grand tambour de guerre sur fût (n° 178), Ø 1,5 m, H 1,2 m, peau tendue, cordages."""
    m = M()
    lathe(m.p("Wood_Dark"), [(0.55, 0.0), (0.72, 0.4), (0.75, 1.0), (0.74, 1.12)], S(lod, 16))
    cyl(m.p("Leather"), (0, 0, 1.12), (0, 0, 1.15), 0.76, 0.76, S(lod, 16))
    if lod < 2:
        for k in range(12 if lod == 0 else 6):
            a = k * 2 * math.pi / (12 if lod == 0 else 6)
            a2 = a + math.pi / (12 if lod == 0 else 6)
            cyl(m.p("Rope"), (0.74 * math.cos(a), 0.74 * math.sin(a), 1.12), (0.73 * math.cos(a2), 0.73 * math.sin(a2), 0.45), 0.012, 0.012, 3, False)
        for x in (-0.35, 0.35):
            cyl(m.p("Bone"), (x, -0.9, 0.02), (x * 0.6, -0.3, 0.06), 0.03, 0.05, 5)
    return m.out()


def p_treasure_pile(spec, lod):
    """Trésor (n° 179) : coffre ouvert débordant d'or, tas de pièces, sacs, coupe."""
    m = M()
    w, b = m.p("Wood_Dark"), m.p("Brass")
    box(w, (-0.5, -0.3, 0.0), (0.5, 0.3, 0.5))
    box(w, (-0.5, 0.25, 0.5), (0.5, 0.33, 1.0))                       # couvercle relevé
    lathe_at(b, 0, 0, [(0.48, 0.5), (0.35, 0.6), (0.001, 0.68)], S(lod, 10))
    lathe_at(b, 0.8, -0.2, [(0.5, 0.0), (0.3, 0.18), (0.001, 0.28)], S(lod, 12))
    if lod < 2:
        lathe_at(m.p("Leather"), -0.85, 0.1, [(0.18, 0.0), (0.22, 0.15), (0.08, 0.32), (0.1, 0.36)], S(lod, 8))
        lathe_at(b, 0.4, -0.65, [(0.05, 0.0), (0.015, 0.03), (0.015, 0.12), (0.06, 0.2)], S(lod, 8))
        for k in range(6):
            a = k * 1.1
            lathe_at(m.p(("Glass_Green", "Glass_Amber", "Glass_Clear")[k % 3]), 0.8 + 0.3 * math.cos(a), -0.2 + 0.3 * math.sin(a),
                     [(0.001, 0.12), (0.03, 0.15), (0.001, 0.19)], 5)
    return m.out()


def p_lantern_globe(spec, lod):
    """Lanterne-globe de verre suspendue (n° 180), Ø 0,5 m ; pivot au crochet (z = 0)."""
    m = M()
    i = m.p("Brass")
    cyl(i, (0, 0, 0.0), (0, 0, -0.35), 0.01, 0.01, 4, False)
    lathe(i, [(0.001, -0.33), (0.1, -0.35), (0.12, -0.38)], S(lod, 10))
    lathe(m.p("Glass_Amber"), [(0.12, -0.38), (0.24, -0.5), (0.25, -0.62), (0.2, -0.76), (0.1, -0.82)], S(lod, 14))
    lathe(i, [(0.1, -0.82), (0.12, -0.85), (0.001, -0.9)], S(lod, 10))
    candle(m, Vector((0, 0, -0.8)), 0.12, 0.025, lod)
    return m.out()


def p_torch_pole(spec, lod):
    """Mât-torche de camp (n° 181), H 3 m : perche fichée, panier de fer, flamme éteinte."""
    m = M()
    cyl(m.p("Wood_Dark"), (0, 0, 0.0), (0.03, 0.02, 2.7), 0.05, 0.04, S(lod, 6))
    lathe_at(m.p("Iron"), 0.03, 0.02, [(0.04, 2.65), (0.14, 2.8), (0.16, 3.0), (0.14, 3.0)], S(lod, 8))
    lathe_at(m.p("Rock_Red"), 0.03, 0.02, [(0.13, 2.92), (0.001, 2.98)], S(lod, 8))
    return m.out()


def p_bones_scatter(spec, lod):
    """Ossements épars (n° 182) : _1 crâne et os longs, _2 cage thoracique, _3 grand tas."""
    m = M()
    b = m.p("Bone")
    rng = random.Random(spec["seed"])
    v = spec["id"][-1]
    if v in ("1", "3"):
        n = 3 if v == "1" else 7
        for k in range(n):
            c = Vector((rng.uniform(-0.6, 0.6), rng.uniform(-0.5, 0.5), 0.1))
            lathe_at(b, c.x, c.y, [(0.001, 0.0), (0.1, 0.06), (0.11, 0.14), (0.06, 0.2), (0.001, 0.21)], S(lod, 8))
        for k in range({"1": 4, "3": 10}[v]):
            a = rng.uniform(0, 6.28)
            c = Vector((rng.uniform(-0.8, 0.8), rng.uniform(-0.7, 0.7), 0.04))
            d = Vector((math.cos(a), math.sin(a), 0.02)) * rng.uniform(0.25, 0.45)
            cyl(b, c - d, c + d, 0.03, 0.03, 5)
            if lod < 2:
                for e in (c - d, c + d):
                    lathe_at(b, e.x, e.y, [(0.001, 0.0), (0.045, 0.03), (0.001, 0.07)], 5)
    else:
        cyl(b, (-0.9, 0, 0.12), (0.9, 0, 0.12), 0.05, 0.04, 6)          # échine
        for k in range({0: 9, 1: 5, 2: 3}[lod]):
            x = -0.6 + 1.2 * k / 8
            r = 0.5 * math.sin(math.pi * (k + 1) / 10) + 0.15
            pts = [Vector((x, r * math.cos(t), 0.12 + r * math.sin(t) * 0.9)) for t in [j * math.pi / 6 for j in range(7)]]
            API["tube"](b, pts, [0.025] * 7, 4, 0.5)
        b.v = [q - Vector((0, 0, 0.08)) for q in b.v]   # posée au sol
    return m.out()


def p_termite_mound(spec, lod):
    """Termitière / cône de terre (n° 183), H ~2,5 m, cheminées secondaires."""
    m = M()
    o = m.p("Rock_Ochre")
    i0 = len(o.v)
    lathe(o, [(0.9, 0.0), (0.75, 0.6), (0.45, 1.6), (0.25, 2.3), (0.08, 2.55), (0.001, 2.6)], S(lod, 12))
    if lod < 2:
        for k in range(3):
            a = k * 2.1
            lathe_at(o, 0.45 * math.cos(a), 0.45 * math.sin(a), [(0.3, 0.0), (0.2, 0.7), (0.08, 1.2 + 0.2 * k), (0.001, 1.3 + 0.2 * k)], S(lod, 8))
    _jitter(o, i0, 0.05, spec["seed"] % 9)
    for k in range(i0, len(o.v)):
        o.v[k] = Vector((o.v[k].x, o.v[k].y, max(o.v[k].z, 0.0)))
    return m.out()


def p_wagon_steps(spec, lod):
    """Marchepied de roulotte (n° 184), 1 m : trois marches de bois sur limons."""
    m = M()
    d, w = m.p("Wood_Dark"), m.p("Wood_Plank")
    for x in (-0.3, 0.3):
        cyl(d, (x, -0.45, 0.0), (x, 0.3, 1.0), 0.035, 0.035, 4, False)
    for k in range(3):
        z = 0.25 + 0.3 * k
        y = -0.45 + 0.75 * z
        box(w, (-0.32, y - 0.1, z - 0.03), (0.32, y + 0.1, z + 0.02))
    return m.out()


def p_doll_rag(spec, lod):
    """Poupée de chiffon (n° 185), 0,3 m, assise."""
    m = M()
    c = m.p("Canvas")
    lathe(c, [(0.06, 0.02), (0.07, 0.08), (0.05, 0.16), (0.02, 0.17)], S(lod, 8))
    lathe(c, [(0.001, 0.17), (0.05, 0.2), (0.05, 0.25), (0.001, 0.28)], S(lod, 8))
    for x in (-0.03, 0.03):
        cyl(m.p("Cloth_Red"), (x, -0.02, 0.03), (x, -0.16, 0.02), 0.022, 0.02, 5)
    for x in (-0.065, 0.065):
        cyl(c, (x, 0, 0.14), (x * 1.6, -0.03, 0.06), 0.016, 0.014, 5)
    if lod < 2:
        box(m.p("Rope"), (-0.05, -0.01, 0.24), (0.05, 0.05, 0.3))
    return m.out()


# ======================================================================= Tome 6
def p_bunting_garland(spec, lod):
    """Guirlande de fanions sur deux mâts (n° 195) : portée 8 m, mâts 4,5 m, corde en
    chaînette, fanions triangulaires de quatre couleurs alternées."""
    m = M()
    d = m.p("Wood_Dark")
    for x in (-4.0, 4.0):
        cyl(d, (x, 0, 0), (x, 0, 4.5), 0.07, 0.05, S(lod, 6))
    n = {0: 24, 1: 12, 2: 6}[lod]
    sag = 1.0
    pts = [Vector((-4.0 + 8.0 * k / n, 0, 4.4 - sag * math.sin(math.pi * k / n))) for k in range(n + 1)]
    if lod < 2:
        API["tube"](m.p("Rope"), pts, [0.012] * len(pts), 4, 0.5)
    cols = ("Cloth_Red", "Cloth_Ochre", "Cloth_Blue", "Cloth_Green")
    nf = {0: 16, 1: 10, 2: 6}[lod]
    for k in range(nf):
        t0, t1 = (k + 0.15) / nf, (k + 0.85) / nf
        a = Vector((-4.0 + 8.0 * t0, 0, 4.4 - sag * math.sin(math.pi * t0)))
        b = Vector((-4.0 + 8.0 * t1, 0, 4.4 - sag * math.sin(math.pi * t1)))
        c = (a + b) / 2 - Vector((0, 0, 0.42))
        p = m.p(cols[k % 4])
        i = len(p.v)
        p.v += [a, b, c]
        p.f.append((i, i + 1, i + 2))
        p.uv.append([(0, 1), (1, 1), (0.5, 0)])
        p.cn.append(None)
    return m.out()


def p_bookcase_tall(spec, lod):
    """Grande bibliothèque murale (n° 196), 2,4 x 0,5 x 3 m : montants, six tablettes,
    rangées de livres de hauteurs variées, corniche."""
    m = M()
    d = m.p("Wood_Dark")
    for x in (-1.2, 0.0, 1.2):
        box(d, (x - 0.04, -0.25, 0), (x + 0.04, 0.25, 2.9))
    box(d, (-1.2, 0.22, 0), (1.2, 0.25, 2.9))
    zs = [0.08 + 0.47 * k for k in range(7)]
    for z in zs:
        box(d, (-1.2, -0.25, z - 0.04), (1.2, 0.25, z))
    box(d, (-1.28, -0.3, 2.9), (1.28, 0.3, 3.0))
    b = m.p("Book_Covers")
    rng = random.Random(spec["seed"])
    for z in zs[:-1]:
        for half in (-1, 1):
            x = 0.04 if half > 0 else -1.16
            end = 1.16 if half > 0 else -0.04
            step = {0: 1, 1: 2, 2: 6}[lod]
            while x < end - 0.03:
                wd = rng.uniform(0.03, 0.07) * step
                if x + wd > end:
                    break
                if rng.random() < 0.08 and lod == 0:
                    x += wd
                    continue
                hgt = rng.uniform(0.26, 0.4)
                box(b, (x, -0.21, z), (x + wd - 0.004, rng.uniform(0.0, 0.05), z + hgt), uv=2.0)
                x += wd
    return m.out()


def p_desk_scribe(spec, lod):
    """Pupitre de scribe incliné (n° 197), 1,1 x 0,7 x 1,1 m : plateau en pente,
    rebord porte-plume, tablette basse, livre ouvert et encrier."""
    m = M()
    d = m.p("Wood_Dark")
    for x in (-0.5, 0.5):
        for y in (-0.3, 0.3):
            h = 0.85 if y < 0 else 1.05
            box(d, (x - 0.035, y - 0.035, 0), (x + 0.035, y + 0.035, h))
    box(d, (-0.5, -0.3, 0.18), (0.5, 0.3, 0.21))
    xf = Matrix.Translation((0, 0, 0.95)) @ Matrix.Rotation(math.radians(18), 4, "X")
    box(d, (-0.55, -0.36, -0.02), (0.55, 0.36, 0.02), xf=xf)
    box(d, (-0.55, -0.4, -0.02), (0.55, -0.36, 0.05), xf=xf)
    if lod < 2:
        box(m.p("Paper"), (-0.32, -0.2, 0.02), (0.32, 0.2, 0.035), xf=xf)
        lathe_at(m.p("Glass_Green"), 0.42, 0.25, [(0.03, 1.09), (0.035, 1.12), (0.015, 1.16)], S(lod, 8))
    return m.out()


def p_paper_stack(spec, lod):
    """Piles de documents (n° 198) : _1 trois liasses ficelées, _2 pile haute et rouleaux."""
    m = M()
    p = m.p("Paper")
    rng = random.Random(spec["seed"])
    tall = spec["id"].endswith("_2")
    piles = [(-0.18, 0.0, 0.28), (0.12, 0.05, 0.18), (0.0, -0.22, 0.12)] if not tall else [(0.0, 0.0, 0.55)]
    for cx, cy, h in piles:
        z = 0.0
        while z < h:
            dz = rng.uniform(0.02, 0.05) if lod < 2 else h
            xf = Matrix.Translation((cx + rng.uniform(-0.01, 0.01), cy, z)) @ Matrix.Rotation(rng.uniform(-0.12, 0.12), 4, "Z")
            box(p, (-0.15, -0.11, 0), (0.15, 0.11, min(dz, h - z)), uv=3.0, xf=xf)
            z += dz
        if lod == 0 and not tall:
            box(m.p("Rope"), (cx - 0.01, cy - 0.12, 0), (cx + 0.01, cy + 0.12, h + 0.005))
    if tall:
        for k in range({0: 3, 1: 2, 2: 1}[lod]):
            y = -0.25 - 0.07 * k
            cyl(p, (-0.2, y, 0.035), (0.2, y, 0.035), 0.035, 0.035, S(lod, 8))
    return m.out()


def p_curtain_drape(spec, lod):
    """Tenture de velours drapée (n° 199), 2,4 x 3,5 m : tringle, plis verticaux,
    embrasses relevant le pan sur les côtés, frange au sol."""
    m = M()
    cyl(m.p("Brass"), (-1.25, 0.0, 3.45), (1.25, 0.0, 3.45), 0.025, 0.025, S(lod, 8))
    c = m.p("Cloth_Red")
    nx = {0: 24, 1: 14, 2: 6}[lod]
    nz = {0: 8, 1: 5, 2: 2}[lod]
    i0 = len(c.v)
    for j in range(nz + 1):
        z = 3.4 * (1 - j / nz)
        for k in range(nx + 1):
            u = k / nx
            side = abs(u - 0.5) * 2
            pinch = max(0.0, side - 0.55) * (1 - abs(z - 1.2) / 2.2) if z < 3.4 else 0
            x = (-1.2 + 2.4 * u) * (1 - 0.35 * max(0.0, pinch))
            y = -0.06 * math.sin(k * math.pi / 2 * (24 / nx) ** 0) - 0.04 * pinch
            c.v.append(Vector((x, y, z)))
    for j in range(nz):
        for k in range(nx):
            a = i0 + j * (nx + 1) + k
            c.f.append((a, a + 1, a + nx + 2, a + nx + 1))
            c.uv.append([(k / nx, 1 - j / nz), ((k + 1) / nx, 1 - j / nz), ((k + 1) / nx, 1 - (j + 1) / nz), (k / nx, 1 - (j + 1) / nz)])
            c.cn.append(None)
    if lod < 2:
        for x in (-0.9, 0.9):
            cyl(m.p("Cloth_Ochre"), (x * 1.0, -0.12, 1.2), (x * 1.0, 0.04, 1.2), 0.05, 0.05, 6)
    return m.out()


def p_pyre_stake(spec, lod):
    """Bûcher à poteau (n° 200) : fagots et bûches en cercle Ø 2 m, poteau de 3 m au
    centre, corde."""
    m = M()
    w = m.p("Bark_Broad")
    rng = random.Random(spec["seed"])
    n = {0: 22, 1: 12, 2: 6}[lod]
    for k in range(n):
        a = 2 * math.pi * k / n + rng.uniform(-0.1, 0.1)
        r0, r1 = 0.95, rng.uniform(0.05, 0.25)
        z1 = rng.uniform(0.6, 0.95)
        cyl(w, (r0 * math.cos(a), r0 * math.sin(a), 0.05), (r1 * math.cos(a), r1 * math.sin(a), z1), 0.07, 0.06, S(lod, 6))
    if lod < 2:
        for k in range(8):
            a = 2 * math.pi * k / 8 + 0.3
            c = Vector((0.6 * math.cos(a), 0.6 * math.sin(a), 0.12))
            dd = Vector((-math.sin(a), math.cos(a), 0)) * 0.4
            cyl(w, c - dd, c + dd, 0.09, 0.09, S(lod, 6))
    cyl(m.p("Wood_Dark"), (0, 0, 0), (0, 0, 3.0), 0.11, 0.09, S(lod, 8))
    if lod < 2:
        cyl(m.p("Rope"), (0, 0, 1.5), (0, 0, 1.65), 0.125, 0.125, S(lod, 8), False)
    return m.out()


def p_map_table(spec, lod):
    """Table de campagne pliante avec carte déroulée (n° 201), 1,8 x 1 x 0,85 m :
    tréteaux en X, plateau, carte imaginaire lestée de pierres, dague, bougeoir."""
    m = M()
    d = m.p("Wood_Dark")
    for x in (-0.7, 0.7):
        for s in (-1, 1):
            cyl(d, (x, -0.42 * s, 0), (x, 0.42 * s, 0.8), 0.03, 0.03, S(lod, 6))
    box(d, (-0.9, -0.5, 0.8), (0.9, 0.5, 0.85))
    p = m.p("Parchment_Map")
    _quad(p, (-0.62, -0.4, 0.856), (0.62, -0.4, 0.856), (0.62, 0.4, 0.856), (-0.62, 0.4, 0.856))
    if lod < 2:
        cyl(p, (-0.66, -0.4, 0.875), (-0.66, 0.4, 0.875), 0.022, 0.022, S(lod, 8))
        for x, y in ((0.55, 0.33), (0.55, -0.33)):
            lathe_at(m.p("Rock_Grey"), x, y, [(0.04, 0.85), (0.05, 0.88), (0.001, 0.91)], 6)
        candle(m, Vector((-0.8, 0.38, 0.85)), 0.16, 0.025, lod)
    return m.out()


def p_wardrobe_painted(spec, lod):
    """Armoire peinte (n° 202), 1,6 x 0,6 x 2,2 m : caisse, deux portes à panneaux,
    corniche, pieds tournés, poignées de laiton."""
    m = M()
    w = m.p("Wood_Painted")
    box(w, (-0.8, -0.3, 0.15), (0.8, 0.3, 2.05))
    box(w, (-0.86, -0.36, 2.05), (0.86, 0.36, 2.2))
    box(w, (-0.84, -0.34, 0.12), (0.84, 0.34, 0.2))
    for x in (-0.72, 0.72):
        for y in (-0.24, 0.24):
            lathe_at(m.p("Wood_Dark"), x, y, [(0.05, 0.0), (0.07, 0.06), (0.04, 0.12)], S(lod, 8))
    if lod < 2:
        o = m.p("Wood_Dark")
        for x0, x1 in ((-0.74, -0.04), (0.04, 0.74)):
            for z0, z1 in ((0.3, 1.0), (1.15, 1.95)):
                box(o, (x0 + 0.06, -0.32, z0), (x1 - 0.06, -0.3, z1))
        box(o, (-0.01, -0.32, 0.2), (0.01, -0.3, 2.05))
        for x in (-0.09, 0.09):
            lathe_at(m.p("Brass"), x, -0.32, [(0.001, 1.04), (0.02, 1.06), (0.001, 1.1)], 6)
    return m.out()


# ======================================================================= Tome 7
def p_obelisk_ruined(spec, lod):
    """Obélisque de grès de nécropole (n° 203) : _1 debout, fût brisé en biseau à 5,5 m ;
    _2 tronçon resté sur son socle, le reste couché dans le sable."""
    m = M()
    o = m.p("Rock_Ochre")
    box(o, (-0.9, -0.9, 0), (0.9, 0.9, 0.5))
    box(o, (-0.7, -0.7, 0.5), (0.7, 0.7, 0.8))
    standing = spec["id"].endswith("_1")
    top = 5.5 if standing else 1.6

    def shaft(part, z0, z1, h0, h1, xf=None, cut=0.0):
        c = [(-h0, -h0, z0), (h0, -h0, z0), (h0, h0, z0), (-h0, h0, z0),
             (-h1, -h1, z1 - cut), (h1, -h1, z1 + cut), (h1, h1, z1 + cut * 0.4), (-h1, h1, z1 - cut * 0.6)]
        c = [Vector(v) for v in c]
        if xf is not None:
            c = [xf @ v for v in c]
        for f in ((0, 1, 5, 4), (1, 2, 6, 5), (2, 3, 7, 6), (3, 0, 4, 7), (4, 5, 6, 7)):
            _quad(part, *[c[k] for k in f])

    shaft(o, 0.8, top, 0.5, 0.5 - 0.03 * top, cut=0.25)
    if not standing:
        xf = Matrix.Translation((0.6, 0.0, 0.35)) @ Matrix.Rotation(math.radians(88), 4, "Y") @ Matrix.Rotation(0.2, 4, "Z")
        shaft(o, 0.0, 4.2, 0.45, 0.3, xf=xf, cut=0.15)
        if lod < 2:
            xf2 = Matrix.Translation((4.75, 0.3, 0.25)) @ Matrix.Rotation(math.radians(90), 4, "Y")
            shaft(o, 0.0, 0.9, 0.3, 0.001, xf=xf2)   # pyramidion tombé
    return m.out()


def p_colossus_feet(spec, lod):
    """Pieds d'un colosse brisé sur leur socle (n° 204), socle 4,4 x 3 m, H 3,4 m :
    deux pieds nus, jambes rompues à mi-mollet, éclats au pied du socle. Aucun visage
    (statues neutres, décision Q18)."""
    m = M()
    o = m.p("Rock_Ochre")
    box(o, (-2.2, -1.5, 0), (2.2, 1.5, 0.9))
    box(o, (-2.35, -1.65, 0), (2.35, 1.65, 0.25))
    i0 = len(o.v)
    for sx, h in ((-0.85, 3.4), (0.85, 2.5)):
        box(o, (sx - 0.55, -0.3, 0.9), (sx + 0.55, 0.85, 1.5))           # talon, cou-de-pied
        box(o, (sx - 0.55, -1.2, 0.9), (sx + 0.55, -0.3, 1.18))          # avant-pied
        lathe_at(o, sx, 0.35, [(0.6, 0.9), (0.62, 1.6), (0.55, 2.2), (0.62, h - 0.3), (0.55, h)], S(lod, 10))
        if lod < 2:
            for k in range(5):
                x = sx - 0.42 + 0.21 * k
                cyl(o, (x, -1.2, 1.0), (x, -1.45, 1.0), 0.11 - 0.01 * abs(k - 1), 0.09, S(lod, 6))
    _jitter(o, i0, 0.03, spec["seed"] % 11)
    if lod < 2:
        for k in range(4):
            _stone(m.p("Rock_Ochre"), Vector((-1.8 + 1.2 * k, -2.2 + 0.2 * k, 0)), 0.45, 0.35, spec["seed"] + k, lod)
    return m.out()


def p_mausoleum_small(spec, lod):
    """Petit mausolée de pierre (n° 205), 3 x 4 x 4,2 m : socle, murs, pilastres, porte
    de bronze sous arc, fronton, toit à deux pans de dalles, marches."""
    m = M()
    w, c = m.p("Stone_Wall"), m.p("Stone_Carved")
    box(c, (-1.7, -2.2, 0), (1.7, 2.2, 0.4))
    box(c, (-1.0, -2.75, 0), (1.0, -2.2, 0.2))
    box(c, (-1.0, -2.5, 0.2), (1.0, -2.2, 0.4))
    box(w, (-1.5, -2.0, 0.4), (1.5, 2.0, 3.0))
    for x in (-1.55, -0.75, 0.75, 1.55):
        box(c, (x - 0.12, -2.12, 0.4), (x + 0.12, -1.95, 3.0))
    box(c, (-1.65, -2.15, 3.0), (1.65, 2.15, 3.2))
    _arch_band(c, -0.55, 0.55, 2.0, 0.55, 0.12, 0.14, lod, y0=(-2.08, -1.96))
    box(m.p("Brass"), (-0.55, -2.03, 0.4), (0.55, -2.0, 2.0))
    r = m.p("Stone_Carved")
    for sg in (-1, 1):
        _quad(r, (sg * 1.75, -2.25, 3.2), (sg * 1.75, 2.25, 3.2), (0, 2.25, 4.2), (0, -2.25, 4.2))
        if sg > 0:
            r.f[-1] = tuple(reversed(r.f[-1]))
    for y in (-2.25, 2.25):
        tri = (Vector((-1.75, y, 3.2)), Vector((1.75, y, 3.2)), Vector((0, y, 4.2)))
        i = len(r.v)
        r.v += list(tri)
        r.f.append((i, i + 1, i + 2) if y > 0 else (i, i + 2, i + 1))
        r.uv.append([(0, 0), (1, 0), (0.5, 1)])
        r.cn.append(None)
    return m.out()


def p_stair_spiral(spec, lod):
    """Escalier de pierre en colimaçon (n° 206), Ø 2,6 m, H 3,2 m : noyau central,
    marches en éventail (un tour), main courante de corde au LOD0."""
    m = M()
    c = m.p("Stone_Carved")
    cyl(c, (0, 0, 0), (0, 0, 3.4), 0.18, 0.18, S(lod, 10))
    n = 16
    for k in range(n):
        a0, a1 = 2 * math.pi * k / n, 2 * math.pi * (k + 1.15) / n
        z = 0.2 * (k + 1)
        pts = [Vector((0.15 * math.cos(a0), 0.15 * math.sin(a0), z)), Vector((1.3 * math.cos(a0), 1.3 * math.sin(a0), z)),
               Vector((1.3 * math.cos(a1), 1.3 * math.sin(a1), z)), Vector((0.15 * math.cos(a1), 0.15 * math.sin(a1), z))]
        low = [p - Vector((0, 0, 0.18)) for p in pts]
        _quad(c, *pts)
        _quad(c, low[3], low[2], low[1], low[0])
        if lod < 2:
            _quad(c, low[0], low[1], pts[1], pts[0])
            _quad(c, low[1], low[2], pts[2], pts[1])
    if lod == 0:
        rail = [Vector((1.25 * math.cos(2 * math.pi * t / 24), 1.25 * math.sin(2 * math.pi * t / 24), 0.9 + 2.8 * t / 24)) for t in range(25)]
        API["tube"](m.p("Rope"), rail, [0.025] * 25, 4, 0.5)
    return m.out()


# ======================================================================= Tome 8
def p_window_rose(spec, lod):
    """Rosace de vitrail dans son cadre de pierre (n° 207), Ø 3 m, base à 0 (pièce de
    façade posée sur un mur ou un pignon) : disque de verre, anneau mouluré, rayons."""
    m = M()
    g = m.p("Glass_Rose")
    n = S(lod, 32)
    R, zc = 1.3, 1.5
    i = len(g.v)
    g.v.append(Vector((0, 0, zc)))
    for k in range(n):
        a = 2 * math.pi * k / n
        g.v.append(Vector((R * math.cos(a), 0, zc + R * math.sin(a))))
    for k in range(n):
        a0, a1 = 2 * math.pi * k / n, 2 * math.pi * (k + 1) / n
        g.f.append((i, i + 1 + (k + 1) % n, i + 1 + k))
        g.uv.append([(0.5, 0.5), (0.5 + 0.48 * math.cos(a1), 0.5 + 0.48 * math.sin(a1)), (0.5 + 0.48 * math.cos(a0), 0.5 + 0.48 * math.sin(a0))])
        g.cn.append(None)
    c = m.p("Stone_Carved")
    xf = Matrix.Translation((0, 0, zc)) @ Matrix.Rotation(math.pi / 2, 4, "X")
    i0 = len(c.v)
    API["tube"](c, [Vector((0, 0, -0.12)), Vector((0, 0, 0.12))], [R + 0.2, R + 0.2], n, 1.0)
    API["tube"](c, [Vector((0, 0, 0.12)), Vector((0, 0, -0.12))], [R, R], n, 1.0)
    for k in range(n):   # faces avant et arrière de l'anneau
        a0, a1 = 2 * math.pi * k / n, 2 * math.pi * (k + 1) / n
        for y, sg in ((-0.12, 1), (0.12, -1)):
            q = [Vector((R * math.cos(a0), R * math.sin(a0), y)), Vector((R * math.cos(a1), R * math.sin(a1), y)),
                 Vector(((R + 0.2) * math.cos(a1), (R + 0.2) * math.sin(a1), y)), Vector(((R + 0.2) * math.cos(a0), (R + 0.2) * math.sin(a0), y))]
            _quad(c, *(q if sg > 0 else q[::-1]))
    c.v[i0:] = [xf @ v for v in c.v[i0:]]
    if lod < 2:
        for k in range(8):
            a = 2 * math.pi * k / 8
            cyl(c, (0.25 * math.cos(a), 0, zc + 0.25 * math.sin(a)), (R * math.cos(a), 0, zc + R * math.sin(a)), 0.035, 0.035, 4, False)
        cyl(c, (0, -0.04, zc), (0, 0.04, zc), 0.26, 0.26, S(lod, 16), False)
    return m.out()


def p_bed_pallet(spec, lod):
    """Paillasse de dortoir (n° 208), 2 x 0,9 m : cadre bas de planches, matelas de toile
    bourré de paille, couverture repliée."""
    m = M()
    d = m.p("Wood_Plank")
    box(d, (-1.0, -0.45, 0.1), (1.0, 0.45, 0.2))
    for x in (-0.95, 0.95):
        for y in (-0.4, 0.4):
            box(d, (x - 0.04, y - 0.04, 0), (x + 0.04, y + 0.04, 0.1))
    box(m.p("Canvas"), (-0.96, -0.41, 0.2), (0.96, 0.41, 0.32))
    if lod < 2:
        box(m.p("Cloth_Ochre"), (0.2, -0.43, 0.32), (0.9, 0.43, 0.38))
        box(m.p("Canvas"), (-0.92, -0.3, 0.32), (-0.6, 0.3, 0.42))   # traversin
    return m.out()


def p_earthwork_plank(spec, lod):
    """Levée de terre étayée de planches (n° 209), module 6 m : talus de terre, parement
    de planches verticales tenu par des pieux, côté défenseur en pente douce."""
    m = M()
    e = m.p("Earth")
    i0 = len(e.v)
    prof = [(-0.35, 0.0), (-0.35, 1.6), (0.3, 1.7), (2.2, 0.0)]
    for y0, y1 in ((-3.0, 3.0),):
        for k in range(len(prof) - 1):
            (a, za), (b, zb) = prof[k], prof[k + 1]
            _quad(e, (y0, a, za), (y1, a, za), (y1, b, zb), (y0, b, zb))
        for x, sg in ((-3.0, 1), (3.0, -1)):
            pts = [Vector((x, a, z)) for a, z in prof]
            i = len(e.v)
            e.v += pts
            e.f.append(tuple(i + j for j in (range(4) if sg < 0 else reversed(range(4)))))
            e.uv.append([(0, 0), (0, 1), (1, 1), (1, 0)])
            e.cn.append(None)
    _jitter(e, i0, 0.04, spec["seed"] % 7)
    for k in range(i0, len(e.v)):
        e.v[k] = Vector((e.v[k].x, e.v[k].y, max(e.v[k].z, 0.0)))
    w = m.p("Wood_Weathered")
    nb = {0: 24, 1: 12, 2: 4}[lod]
    rng = random.Random(spec["seed"])
    for k in range(nb):
        x0 = -3.0 + 6.0 * k / nb
        box(w, (x0 + 0.01, -0.45, 0), (x0 + 6.0 / nb - 0.01, -0.36, 1.7 + rng.uniform(-0.1, 0.1)))
    for x in (-2.8, -1.4, 0.0, 1.4, 2.8):
        cyl(m.p("Wood_Dark"), (x, -0.52, 0), (x, -0.52, 2.0), 0.07, 0.06, S(lod, 6))
    return m.out()


# ======================================================================= Tome 9
def p_gear_large(spec, lod):
    """Grand engrenage de bois cerclé de fer sur chevalet (n° 210), Ø 2,6 m : roue dentée
    debout, axe, deux montants en A, pignon de renvoi."""
    m = M()
    d, i = m.p("Wood_Dark"), m.p("Iron")
    R, zc = 1.15, 1.45
    n = S(lod, 24)
    cyl(d, (0, -0.12, zc), (0, 0.12, zc), R, R, n)
    cyl(i, (0, -0.13, zc), (0, 0.13, zc), R + 0.02, R + 0.02, n, cap=False)
    teeth = {0: 20, 1: 12, 2: 0}[lod]
    for k in range(teeth):
        a = 2 * math.pi * k / teeth
        xf = Matrix.Translation((0, 0, zc)) @ Matrix.Rotation(-a, 4, "Y")
        box(d, (R - 0.02, -0.1, -0.07), (R + 0.16, 0.1, 0.07), xf=xf)
    cyl(i, (0, -0.45, zc), (0, 0.45, zc), 0.08, 0.08, S(lod, 8))
    for y in (-0.38, 0.38):
        for sx in (-1, 1):
            cyl(d, (sx * 0.8, y, 0), (0, y, zc + 0.1), 0.06, 0.06, S(lod, 6))
        box(d, (-0.9, y - 0.06, 0), (0.9, y + 0.06, 0.12))
    if lod < 2:
        cyl(d, (1.55, -0.1, 0.7), (1.55, 0.1, 0.7), 0.35, 0.35, S(lod, 12))
        box(d, (1.4, -0.3, 0), (1.7, 0.3, 0.4))
    return m.out()


def p_crane_wood(spec, lod):
    """Grue de chantier en bois (n° 211), H 6,5 m : mât sur croix de base, bras incliné
    haubané, poulie, corde et crochet, contrepoids de pierres."""
    m = M()
    d = m.p("Wood_Dark")
    for a in (0, math.pi / 2):
        box(d, (-1.4, -0.12, 0), (1.4, 0.12, 0.25), xf=Matrix.Rotation(a, 4, "Z"))
    cyl(d, (0, 0, 0), (0, 0, 6.0), 0.16, 0.13, S(lod, 8))
    for k in range(4):
        a = math.pi / 2 * k + math.pi / 4
        cyl(d, (1.1 * math.cos(a), 1.1 * math.sin(a), 0.2), (0, 0, 2.2), 0.06, 0.06, S(lod, 6), False)
    tip = Vector((3.8, 0, 6.4))
    cyl(d, (0, 0, 4.2), tip, 0.1, 0.08, S(lod, 6))
    cyl(d, (0, 0, 6.0), (-1.4, 0, 5.4), 0.08, 0.08, S(lod, 6))
    box(m.p("Rock_Grey"), (-1.8, -0.35, 4.9), (-1.1, 0.35, 5.5))
    r = m.p("Rope")
    cyl(r, (0, 0, 6.0), tip, 0.015, 0.015, 4, False)
    cyl(r, tip, (3.8, 0, 2.6), 0.015, 0.015, 4, False)
    if lod < 2:
        cyl(m.p("Iron"), (3.8, -0.08, 6.3), (3.8, 0.08, 6.3), 0.14, 0.14, S(lod, 10))
        API["tube"](m.p("Iron"), [Vector((3.8, 0, 2.6)), Vector((3.8, 0, 2.35)), Vector((3.95, 0, 2.3)), Vector((4.0, 0, 2.42))], [0.025] * 4, 5, 0.5)
    return m.out()


# ====================================================================== Tome 10
def p_armillary_sphere(spec, lod):
    """Sphère armillaire d'astrologue sur pied (n° 212), H 2,2 m : trépied, colonne,
    anneaux de laiton (méridien, équateur, écliptique), globe central."""
    m = M()
    b, d = m.p("Brass"), m.p("Wood_Dark")
    for k in range(3):
        a = 2 * math.pi * k / 3
        cyl(d, (0.55 * math.cos(a), 0.55 * math.sin(a), 0), (0, 0, 0.6), 0.04, 0.035, S(lod, 6))
    cyl(d, (0, 0, 0.55), (0, 0, 1.2), 0.06, 0.05, S(lod, 8))
    zc, R = 1.65, 0.5
    n = {0: 24, 1: 14, 2: 6}[lod]
    for ax, tilt in (("X", math.pi / 2), ("Y", math.pi / 2), ("X", math.radians(23))):
        if lod == 2 and tilt != math.pi / 2:
            continue
        pts = [Vector((R * math.cos(2 * math.pi * k / n), R * math.sin(2 * math.pi * k / n), 0)) for k in range(n + 1)]
        xf = Matrix.Translation((0, 0, zc)) @ Matrix.Rotation(tilt, 4, ax)
        API["tube"](b, [xf @ p for p in pts], [0.02] * (n + 1), 4 if lod < 2 else 3, 0.5)
    pts = [Vector((R * math.cos(2 * math.pi * k / n), R * math.sin(2 * math.pi * k / n), zc)) for k in range(n + 1)]
    API["tube"](b, pts, [0.025] * (n + 1), 4 if lod < 2 else 3, 0.5)
    if lod < 2:
        lathe_at(m.p("Wood_Painted"), 0, 0, [(0.001, zc - 0.16), (0.12, zc - 0.11), (0.16, zc), (0.12, zc + 0.11), (0.001, zc + 0.16)], S(lod, 12))
        cyl(b, (0, 0, zc - R - 0.05), (0, 0, zc + R + 0.06), 0.012, 0.012, 4)
    cyl(b, (0, 0, 1.15), (0, 0, zc - R), 0.03, 0.03, 6)
    return m.out()


def p_battering_ram(spec, lod):
    """Bélier de siège couvert sur roues (n° 213), 6 x 2,6 x 3 m : châssis, quatre roues,
    toit de planches à deux pans tendu de peaux, tronc ferré suspendu à des chaînes."""
    m = M()
    d = m.p("Wood_Dark")
    for y in (-1.0, 1.0):
        box(d, (-2.8, y - 0.12, 0.6), (2.8, y + 0.12, 0.85))
        for x in (-2.7, -0.9, 0.9, 2.7):
            box(d, (x - 0.1, y - 0.1, 0.85), (x + 0.1, y + 0.1, 2.3))
        box(d, (-2.8, y - 0.1, 2.2), (2.8, y + 0.1, 2.4))
    for x in (-2.0, 2.0):
        for y in (-1.25, 1.25):
            wheel(m, (x, y, 0.55), 0.55, 0.18, lod, 6, axis="y")
    hide = m.p("Leather")
    for sg in (-1, 1):
        q = [(-3.0, sg * 1.3, 2.3), (3.0, sg * 1.3, 2.3), (3.0, 0, 3.0), (-3.0, 0, 3.0)]
        _quad(hide, *(q if sg < 0 else q[::-1]))
    cyl(m.p("Bark_Broad"), (-3.0, 0, 1.5), (3.3, 0, 1.5), 0.25, 0.22, S(lod, 10))
    cyl(m.p("Iron"), (3.3, 0, 1.5), (3.7, 0, 1.5), 0.26, 0.05, S(lod, 8))
    if lod < 2:
        for x in (-1.5, 1.5):
            cyl(m.p("Iron"), (x, 0, 1.75), (x, 0, 2.3), 0.025, 0.025, 4)
    return m.out()


# ================================================ Camp de l'Oracle (23/11/2026)
def p_tent_pilgrim(spec, lod):
    """Tente de pèlerin à deux pans (retour propriétaire du 23/11/2026 : « océan de
    tentes » du Camp de l'Oracle). Faîtage selon X, entrée à l'extrémité -X (rabat
    relevé), pignon arrière fermé, mâts aux deux bouts, haubans et piquets au LOD0.
    Variantes : _1 toile écrue, _2 toile rapiécée (pièces ocre), _3 peaux tannées,
    Tent_Pilgrim_Long_1 tente longue de famille."""
    m = M()
    sid = spec["id"]
    long_ = "Long" in sid
    L, W, H = (5.0, 2.8, 2.3) if long_ else {"1": (3.0, 2.2, 1.8), "2": (3.2, 2.3, 1.85), "3": (2.6, 2.0, 1.6)}[sid[-1]]
    mat = "Leather" if sid.endswith("_3") and not long_ else "Canvas"
    c = m.p(mat)
    hx, hw = L / 2, W / 2
    # deux pans (légèrement débordants)
    _quad(c, (-hx, -hw, 0.05), (hx, -hw, 0.05), (hx, 0, H), (-hx, 0, H))
    _quad(c, (hx, hw, 0.05), (-hx, hw, 0.05), (-hx, 0, H), (hx, 0, H))
    # pignon arrière fermé (+X)
    i = len(c.v)
    c.v += [Vector((hx, -hw, 0.05)), Vector((hx, hw, 0.05)), Vector((hx, 0, H))]
    c.f.append((i, i + 1, i + 2))
    c.uv.append([(0, 0), (1, 0), (0.5, 1)])
    c.cn.append(None)
    # entrée (-X) : moitié gauche fermée, moitié droite relevée sur le pan
    i = len(c.v)
    c.v += [Vector((-hx, 0, H)), Vector((-hx, -hw, 0.05)), Vector((-hx, -hw * 0.15, 0.05))]
    c.f.append((i, i + 1, i + 2))
    c.uv.append([(0.5, 1), (0, 0), (0.42, 0)])
    c.cn.append(None)
    if lod < 2:
        i = len(c.v)   # rabat roulé contre le pan +Y
        c.v += [Vector((-hx, 0, H)), Vector((-hx - 0.25, hw * 0.9, 0.35)), Vector((-hx - 0.05, hw * 0.55, 0.25))]
        c.f.append((i, i + 1, i + 2))
        c.uv.append([(0.5, 1), (1, 0), (0.7, 0)])
        c.cn.append(None)
    if sid.endswith("_2") or long_:
        pt = m.p("Cloth_Ochre")
        rng = random.Random(spec["seed"])
        for k in range(3 if lod == 0 else (1 if lod == 1 else 0)):
            x0 = rng.uniform(-hx + 0.3, hx - 0.9)
            t0 = rng.uniform(0.15, 0.6)
            for side in (-1, 1):
                if rng.random() < 0.5:
                    continue
                a = Vector((x0, side * hw * (1 - t0), 0.05 + H * t0))
                b = Vector((x0 + 0.6, side * hw * (1 - t0), 0.05 + H * t0))
                t1 = t0 + 0.2
                cc = Vector((x0 + 0.6, side * hw * (1 - t1), 0.05 + H * t1))
                d = Vector((x0, side * hw * (1 - t1), 0.05 + H * t1))
                off = Vector((0, side * 0.012, 0.012))
                q = [a + off, b + off, cc + off, d + off]
                _quad(pt, *(q if side < 0 else [q[1], q[0], q[3], q[2]]))
    w = m.p("Wood_Dark")
    for x in (-hx - 0.02, hx + 0.02):
        cyl(w, (x, 0, 0), (x, 0, H + 0.25), 0.035, 0.03, S(lod, 5))
    if long_:
        cyl(w, (0, 0, 0), (0, 0, H + 0.05), 0.035, 0.03, S(lod, 5))
    if lod == 0:
        r = m.p("Rope")
        for x in (-hx - 0.02, hx + 0.02):
            g = Vector((x + (0.9 if x > 0 else -0.9), 0, 0))
            cyl(r, (x, 0, H + 0.2), tuple(g), 0.01, 0.01, 4, False)
            cyl(w, tuple(g), tuple(g + Vector((0, 0, 0.25))), 0.025, 0.02, 4)
        for x in (-hx * 0.6, hx * 0.6):
            for side in (-1, 1):
                cyl(w, (x, side * (hw + 0.25), 0), (x, side * (hw + 0.25), 0.2), 0.02, 0.015, 4)
    return m.out()


# ======================================================= palais impérial (Lhynn)
# Demande du propriétaire du 25/11/2026 (lore v1.0.140) : palais impérial de Lhynn au
# sommet de la ville en gradins. Monument « signature » : RÉINTERPRÉTATION (pas de
# reproduction fidèle) — façade gothique à porte centrale très haute, portails latéraux,
# statues colossales encapuchonnées à sujet neutre (sans visage ni attribut identifiable),
# soubassement peint à chevrons, flèches à toit d'ardoise. Aucune héraldique de faction.
def _gothic_arch(part, x0, x1, z_spring, y0, y1, lod, top=None):
    """Arc brisé (ogive) de x0 à x1 naissant à z_spring, profondeur Y [y0, y1] ;
    extrados prolongé jusqu'au rectangle [x0-0.8, x1+0.8] x z <= top (écoinçons)."""
    n = {0: 8, 1: 5, 2: 3}[lod]
    w = (x1 - x0) / 2
    R = 2 * w * 0.86   # ogive tiers-point
    zt = top if top is not None else z_spring + R * math.sin(math.acos((R - w) / R)) + 0.8
    for side in (-1, 1):
        cx = (x0 + x1) / 2 - side * (R - w)
        a_end = math.acos((R - w) / R)
        for k in range(n):
            a0, a1 = a_end * k / n, a_end * (k + 1) / n
            pts = []
            for a in (a0, a1):
                xi = cx + side * R * math.cos(a)
                zi = z_spring + R * math.sin(a)
                xo, zo = _ray_rect(cx, z_spring, side * math.cos(a), math.sin(a), x0 - 0.8, x1 + 0.8, zt)
                pts.append((xi, zi, xo, zo))
            (xi0, zi0, xo0, zo0), (xi1, zi1, xo1, zo1) = pts
            if side < 0:   # garde l'ordre des sommets cohérent (normales sortantes)
                xi0, zi0, xo0, zo0, xi1, zi1, xo1, zo1 = xi1, zi1, xo1, zo1, xi0, zi0, xo0, zo0
            for qq in ([(xi0, y0, zi0), (xi1, y0, zi1), (xo1, y0, zo1), (xo0, y0, zo0)],
                       [(xo0, y1, zo0), (xo1, y1, zo1), (xi1, y1, zi1), (xi0, y1, zi0)],
                       [(xi1, y0, zi1), (xi0, y0, zi0), (xi0, y1, zi0), (xi1, y1, zi1)]):
                i = len(part.v)
                part.v += [Vector(v) for v in qq]
                part.f.append((i, i + 1, i + 2, i + 3))
                part.uv.append([(0, 0), (1, 0), (1, 1), (0, 1)])
                part.cn.append(None)
    return zt


def _pinnacle(part, x, y, z, r, h, lod):
    """Pinacle : dé carré puis aiguille pyramidale (4 pans)."""
    box(part, (x - r, y - r, z), (x + r, y + r, z + h * 0.3))
    lathe_at(part, x, y, [(r * 1.15, z + h * 0.3), (0.02, z + h)], 4)


def _colossus(m, x, y, H, lod):
    """Statue colossale encapuchonnée (sujet neutre) : socle à chevrons, robe en
    révolution, capuche sans visage, mains jointes sur le pommeau d'une épée pointe en
    bas. \p H : hauteur totale (socle compris), pied du socle en z = 0."""
    s = m.p("Stone_Carved")
    ped = H * 0.16
    box(m.p("Stone_Lozenge"), (x - 2.0, y - 2.0, 0.0), (x + 2.0, y + 2.0, ped), uv=0.28)
    box(s, (x - 2.2, y - 2.2, ped), (x + 2.2, y + 2.2, ped + 0.4))
    z0 = ped + 0.4
    hb = H - z0   # hauteur de la figure
    sides = S(lod, 14)
    prof = [(1.75, 0.0), (1.55, 0.18), (1.2, 0.45), (1.15, 0.6), (1.4, 0.76), (1.3, 0.82), (0.75, 0.86)]
    if lod == 2:
        prof = [(1.7, 0.0), (1.2, 0.5), (1.35, 0.78), (0.7, 0.86)]
    lathe_at(s, x, y, [(r, z0 + hb * t) for r, t in prof], sides)
    # Capuche (sans visage) : cône arrondi légèrement avancé.
    hood = [(0.78, 0.84), (0.86, 0.9), (0.74, 0.96), (0.35, 0.995), (0.02, 1.0)]
    if lod == 2:
        hood = [(0.8, 0.84), (0.6, 0.96), (0.02, 1.0)]
    lathe_at(s, x, y - 0.12, [(r, z0 + hb * t) for r, t in hood], sides)
    if lod < 2:
        # Avant-bras vers les mains jointes, devant la poitrine (côté -Y = face).
        hz = z0 + hb * 0.58
        for sx in (-1, 1):
            cyl(s, (x + sx * 1.1, y - 0.6, z0 + hb * 0.66), (x + sx * 0.25, y - 1.45, hz), 0.34, 0.28, S(lod, 6))
        cyl(s, (x, y - 1.5, hz - 0.25), (x, y - 1.5, hz + 0.35), 0.42, 0.42, S(lod, 8))
    # Épée pointe en bas, posée devant la robe.
    blade_top = z0 + hb * 0.55
    box(m.p("Iron"), (x - 0.22, y - 1.62, z0 + 0.2), (x + 0.22, y - 1.48, blade_top))
    box(s, (x - 1.1, y - 1.7, blade_top), (x + 1.1, y - 1.4, blade_top + 0.3))


def p_palace_facade(spec, lod):
    """Façade du palais impérial (Lhynn, réinterprétation) : 48 m de large, mur de
    24 m, porte centrale ogivale de 8 x 15,5 m traversante (passable), deux portails
    latéraux à vantaux clos, quatre statues colossales de 15 m devant les contreforts,
    tours d'angle à flèches, rosace et pignon au-dessus de la porte, soubassement peint à
    chevrons, bannières rouges unies. Face (côté parvis) vers -Y ; origine au pied, au
    centre de la porte."""
    m = M()
    s = m.p("Stone_Carved")
    wall = m.p("Stone_Wall")
    loz = m.p("Stone_Lozenge")
    W, HW, T = 24.0, 24.0, 2.0          # demi-largeur, hauteur du mur, demi-épaisseur
    GW, GS = 4.0, 11.0                   # demi-largeur et naissance de l'ogive de la porte
    # Soubassement peint et murs pleins de part et d'autre de la porte.
    for sx in (-1, 1):
        a, b = sorted((sx * (GW + 1.2), sx * W))
        box(loz, (a, -T - 0.4, 0.0), (b, T, 3.2), uv=0.28)
        box(wall, (a, -T, 3.2), (b, T, HW), uv=0.3)
    # Jambages de la porte (traversent toute l'épaisseur) et ogive.
    for sx in (-1, 1):
        a, b = sorted((sx * GW, sx * (GW + 1.2)))
        box(loz, (a, -T - 0.6, 0.0), (b, T, 3.2), uv=0.28)
        box(s, (a, -T - 0.6, 3.2), (b, T, GS))
    top = _gothic_arch(s, -GW, GW, GS, -T - 0.6, T, lod, top=17.5)
    # Massif au-dessus de la porte : rosace et pignon jusqu'à 31 m.
    box(wall, (-GW - 1.2, -T, 17.5), (GW + 1.2, T, HW + 1.0), uv=0.3)
    if lod < 2:
        g = m.p("Glass_Rose")
        n = S(lod, 24)
        R, zc, yv = 2.6, 21.0, -T - 0.02
        i = len(g.v)
        g.v.append(Vector((0, yv, zc)))
        for k in range(n):
            a = 2 * math.pi * k / n
            g.v.append(Vector((R * math.cos(a), yv, zc + R * math.sin(a))))
        for k in range(n):
            a0, a1 = 2 * math.pi * k / n, 2 * math.pi * (k + 1) / n
            g.f.append((i, i + 1 + k, i + 1 + (k + 1) % n))
            g.uv.append([(0.5, 0.5), (0.5 + 0.48 * math.cos(a0), 0.5 + 0.48 * math.sin(a0)),
                         (0.5 + 0.48 * math.cos(a1), 0.5 + 0.48 * math.sin(a1))])
            g.cn.append(None)
        cyl(s, (0, -T - 0.3, zc), (0, -T + 0.05, zc), R + 0.35, R + 0.35, n, cap=False)
    # Pignon triangulaire (au-dessus du mur) sur la travée centrale.
    gz0, gz1, gw = HW + 1.0, 31.0, GW + 1.2
    for y in (-T, T):
        i = len(wall.v)
        wall.v += [Vector((-gw, y, gz0)), Vector((gw, y, gz0)), Vector((0, y, gz1))]
        wall.f.append((i, i + 1, i + 2) if y < 0 else (i, i + 2, i + 1))
        wall.uv.append([(0, 0), (1, 0), (0.5, 1)])
        wall.cn.append(None)
    for sx in (-1, 1):
        i = len(wall.v)
        wall.v += [Vector((sx * gw, -T, gz0)), Vector((0, -T, gz1)), Vector((0, T, gz1)), Vector((sx * gw, T, gz0))]
        wall.f.append((i, i + 1, i + 2, i + 3) if sx > 0 else (i, i + 3, i + 2, i + 1))
        wall.uv.append([(0, 0), (1, 0), (1, 1), (0, 1)])
        wall.cn.append(None)
    _pinnacle(s, 0, 0, gz1 - 0.6, 0.5, 4.0, lod)
    # Passage de la porte : voûte et vantaux ouverts, plaqués contre les jambages.
    d = m.p("Wood_Dark")
    for sx in (-1, 1):
        a, b = sorted((sx * (GW - 0.25), sx * (GW - 0.05)))
        box(d, (a, -T + 0.3, 0.0), (b, T - 0.2, GS - 0.5))
    # Portails latéraux (vantaux clos) à x = ±14.
    for sx in (-1, 1):
        cx = sx * 14.0
        for jx in (-1, 1):
            a, b = sorted((cx + jx * 2.0, cx + jx * 2.9))
            box(s, (a, -T - 0.8, 3.2), (b, -T + 0.2, 8.5))
        _gothic_arch(s, cx - 2.0, cx + 2.0, 8.5, -T - 0.8, -T + 0.2, lod, top=12.2)
        box(d, (cx - 2.0, -T - 0.12, 3.2), (cx + 2.0, -T + 0.05, 8.5))
        if lod == 0:
            for z in (4.4, 6.0, 7.6):
                box(m.p("Iron"), (cx - 1.9, -T - 0.16, z), (cx + 1.9, -T - 0.12, z + 0.1))
        # Bannière rouge unie (sans emblème) sous la corniche.
        if lod < 2:
            c = m.p("Cloth_Red")
            for yy, flip in ((-T - 0.25, False), (-T - 0.22, True)):
                i = len(c.v)
                c.v += [Vector((cx - 2.2, yy, 22.6)), Vector((cx + 2.2, yy, 22.6)), Vector((cx + 2.2, yy, 15.2)), Vector((cx - 2.2, yy, 15.2))]
                c.f.append((i, i + 3, i + 2, i + 1) if not flip else (i, i + 1, i + 2, i + 3))
                c.uv.append([(0, 1), (0, 0), (1, 0), (1, 1)] if not flip else [(0, 1), (1, 1), (1, 0), (0, 0)])
                c.cn.append(None)
    # Contreforts (à x = ±8,5 et ±19,5) coiffés de pinacles ; statues devant.
    for x in (-19.5, -8.5, 8.5, 19.5):
        box(s, (x - 1.1, -T - 1.2, 3.2), (x + 1.1, -T, HW))
        if lod < 2:
            _pinnacle(s, x, -T - 0.6, HW, 0.9, 6.0, lod)
        _colossus(m, x, -T - 4.4, 15.0, lod)
    # Tours d'angle et leurs flèches.
    for sx in (-1, 1):
        a, b = sorted((sx * (W - 2.6), sx * (W + 0.6)))
        box(wall, (a, -T - 0.6, 0.0), (b, T + 0.6, HW + 5.0), uv=0.3)
        cxa = (a + b) / 2
        lathe_at(m.p("Shingle_Slate"), cxa, 0, [(2.2, HW + 5.0), (0.05, HW + 13.0)], S(lod, 8))
        if lod < 2:
            cyl(m.p("Brass"), (cxa, 0, HW + 13.0), (cxa, 0, HW + 14.2), 0.08, 0.03, 4)
    # Parapet crénelé (LOD0) sur les deux murs.
    if lod == 0:
        for sx in (-1, 1):
            x = GW + 1.6
            while x < W - 3.0:
                box(s, (sx * x - 0.45, -T - 0.1, HW), (sx * x + 0.45, -T + 0.5, HW + 1.2))
                x += 1.8
    return m.out()


def p_palace_spire(spec, lod):
    """Tour à flèche du palais impérial (réinterprétation) : fût carré de 7 m jusqu'à
    28 m, contreforts d'angle à pinacles, deux étages de lancettes, galerie crénelée,
    flèche octogonale d'ardoise et épi de laiton ; H ≈ 48 m, origine au pied, au centre."""
    m = M()
    s = m.p("Stone_Carved")
    wall = m.p("Stone_Wall")
    box(m.p("Stone_Lozenge"), (-3.6, -3.6, 0.0), (3.6, 3.6, 2.6), uv=0.28)
    box(wall, (-3.5, -3.5, 2.6), (3.5, 3.5, 28.0), uv=0.3)
    for cx in (-3.5, 3.5):
        for cy in (-3.5, 3.5):
            box(s, (cx - 0.7, cy - 0.7, 0.0), (cx + 0.7, cy + 0.7, 29.0))
            if lod < 2:
                _pinnacle(s, cx, cy, 29.0, 0.6, 4.0, lod)
    if lod < 2:
        gl = m.p("Glass_Clear")
        for z0, z1 in ((9.0, 14.5), (18.0, 24.0)):
            for k in range(4):
                a = k * math.pi / 2
                ux, uy = math.cos(a), math.sin(a)
                # verre en retrait sur chaque face (plan à 3,52 m du centre)
                c = Vector((ux * 3.52, uy * 3.52, 0))
                t = Vector((-uy, ux, 0))
                i = len(gl.v)
                gl.v += [c - t * 0.9 + Vector((0, 0, z0)), c + t * 0.9 + Vector((0, 0, z0)),
                         c + t * 0.9 + Vector((0, 0, z1)), c - t * 0.9 + Vector((0, 0, z1))]
                gl.f.append((i, i + 1, i + 2, i + 3))
                gl.uv.append([(0, 0), (1, 0), (1, 1), (0, 1)])
                gl.cn.append(None)
    # Galerie : corniche et merlons.
    box(s, (-4.0, -4.0, 28.0), (4.0, 4.0, 28.6))
    if lod == 0:
        for k in range(-3, 4):
            for y0 in (-4.0, 3.5):
                box(s, (k * 1.05 - 0.35, y0, 28.6), (k * 1.05 + 0.35, y0 + 0.5, 29.6))
                box(s, (y0, k * 1.05 - 0.35, 28.6), (y0 + 0.5, k * 1.05 + 0.35, 29.6))
    lathe(m.p("Shingle_Slate"), [(3.1, 28.6), (2.4, 33.0), (0.05, 46.5)], S(lod, 8))
    cyl(m.p("Brass"), (0, 0, 46.3), (0, 0, 48.0), 0.12, 0.03, S(lod, 6))
    return m.out()


def p_stairs_monumental(spec, lod):
    """Volée d'escalier monumental (palais impérial, réinterprétation) : 8 m de
    passage, 10 marches de 0,18 x 0,30 m (pente 0,6 : une volée = 3 m de long et 1,8 m
    de haut), parapets pleins de 0,5 m à dés et vasques ; départ en y = 0, montée vers
    +Y. Posée bout à bout sur la rampe du générateur (Settlements.cpp)."""
    m = M()
    s = m.p("Stone_Wall")
    c = m.p("Stone_Carved")
    for k in range(10):
        box(s, (-4.0, k * 0.3, -0.35), (4.0, (k + 1) * 0.3 + 0.02, (k + 1) * 0.18))
        if lod == 0:   # nez de marche mouluré
            box(c, (-4.0, k * 0.3 - 0.03, (k + 1) * 0.18 - 0.05), (4.0, k * 0.3 + 0.02, (k + 1) * 0.18))
    for x in (-4.5, 4.0):
        steps = 10 if lod == 0 else (5 if lod == 1 else 2)
        for k in range(steps):
            y0, y1 = 3.0 * k / steps, 3.0 * (k + 1) / steps
            box(c, (x, y0, -0.35), (x + 0.5, y1, 1.8 * (k + 1) / steps + 1.0))
        if lod < 2:
            box(c, (x - 0.1, 2.4, 1.8), (x + 0.6, 3.0, 3.1))
            lathe_at(m.p("Stone_Carved"), x + 0.25, 2.7, [(0.22, 3.1), (0.38, 3.35), (0.3, 3.6)], S(lod, 8))
    return m.out()


def p_ship_cog(spec, lod):
    """Navire marchand à quai (cogue ; port de Lhynn, T7 pl.4 ; objet générique) : coque
    bordée de 24 m sur 7 m, tonture relevée aux extrémités, quille à z = 0 (flottaison vers
    1,6 m), pont, châteaux avant et arrière, mât de 20 m, vergue et voile ferlée, haubans.
    Étrave vers +Y ; origine sous la quille, au milieu."""
    m = M()
    hull = m.p("Wood_Dark")
    strake = m.p("Wood_Painted")
    L, B, H = 24.0, 7.0, 4.2
    ns = {0: 14, 1: 9, 2: 6}[lod]
    npt = {0: 9, 1: 7, 2: 5}[lod]

    def section(t):
        hw = max(0.25, B / 2 * (1.0 - abs(t) ** 2.4) ** 0.5)
        bottom = 1.4 * abs(t) ** 3
        gun = H + 1.3 * t * t
        pts = []
        for k in range(npt):
            u = -1.0 + 2.0 * k / (npt - 1)
            pts.append(Vector((hw * u, t * L / 2, bottom + (gun - bottom) * abs(u) ** 1.7)))
        return pts, gun, hw

    secs = [section(-1.0 + 2.0 * j / (ns - 1)) for j in range(ns)]
    for j in range(ns - 1):
        a, b = secs[j][0], secs[j + 1][0]
        for k in range(npt - 1):
            part = strake if k in (0, npt - 2) else hull
            i = len(part.v)
            part.v += [a[k], a[k + 1], b[k + 1], b[k]]
            part.f.append((i, i + 1, i + 2, i + 3))
            part.uv.append([(0, 0), (0.25, 0), (0.25, 1), (0, 1)])
            part.cn.append(None)
    # Étrave et étambot : éventails fermant les extrémités.
    for j in (0, ns - 1):
        pts = secs[j][0]
        c = sum(pts, Vector()) / len(pts)
        for k in range(npt - 1):
            i = len(hull.v)
            hull.v += [c, pts[k], pts[k + 1]]
            hull.f.append((i, i + 1, i + 2) if j else (i, i + 2, i + 1))
            hull.uv.append([(0.5, 0.5), (0, 0), (1, 0)])
            hull.cn.append(None)
    # Pont (bordé), châteaux.
    deck = m.p("Wood_Plank")
    for j in range(ns - 1):
        g0, w0 = secs[j][1], secs[j][2]
        g1, w1 = secs[j + 1][1], secs[j + 1][2]
        y0, y1 = (-1.0 + 2.0 * j / (ns - 1)) * L / 2, (-1.0 + 2.0 * (j + 1) / (ns - 1)) * L / 2
        z0, z1 = g0 - 0.45, g1 - 0.45
        i = len(deck.v)
        deck.v += [Vector((-w0 * 0.93, y0, z0)), Vector((w0 * 0.93, y0, z0)), Vector((w1 * 0.93, y1, z1)), Vector((-w1 * 0.93, y1, z1))]
        deck.f.append((i, i + 1, i + 2, i + 3))
        deck.uv.append([(0, 0), (1, 0), (1, 1), (0, 1)])
        deck.cn.append(None)
    box(deck, (-2.6, -11.6, H + 0.9), (2.6, -6.5, H + 3.0))      # château arrière
    box(strake, (-2.7, -11.7, H + 3.0), (2.7, -6.4, H + 3.4))
    if lod < 2:
        box(deck, (-1.9, 7.2, H + 1.0), (1.9, 10.8, H + 2.6))     # château avant
    # Mât, hune, vergue, voile ferlée, haubans.
    wood = m.p("Wood_Weathered")
    cyl(wood, (0, 0.5, H - 0.4), (0, 0.5, H + 16.0), 0.32, 0.2, S(lod, 8))
    if lod < 2:
        cyl(wood, (0, 0.5, H + 12.8), (0, 0.5, H + 13.6), 0.9, 0.9, S(lod, 8))
    cyl(wood, (-6.5, 0.5, H + 12.0), (6.5, 0.5, H + 12.0), 0.16, 0.16, S(lod, 6), False)
    cyl(m.p("Canvas"), (-6.0, 0.62, H + 11.75), (6.0, 0.62, H + 11.75), 0.42, 0.42, S(lod, 8))
    if lod < 2:
        rope = m.p("Rope")
        top = Vector((0, 0.5, H + 15.6))
        for end in ((0, 11.8, H + 1.5), (0, -11.5, H + 3.2), (3.2, 0.5, H + 0.2), (-3.2, 0.5, H + 0.2)):
            API["tube"](rope, [top, Vector(end)], [0.04, 0.04], 4, 0.5)
        cyl(wood, (0, 11.0, H + 1.0), (0, 14.0, H + 3.4), 0.18, 0.1, S(lod, 6))   # beaupré
    return m.out()


# ================================================================ Feyhin Lokcthat (T2)
# v1.0.141 — arène des gladiateurs (T2 p.9-10), cathédrale du sommet (T2 p.7), pile de long
# pont (T2 p.7), gargouille (T2 p.10). Éléments génériques réinterprétés, sans blason.
# Modules d'anneau de l'arène : secteur de 15° autour de −Y (blender ; +Z glTF = vers
# l'extérieur), pivot au milieu du secteur (rayon ARENA_PIV du centre de l'arène, qui est
# en +Y) ; le générateur (Landmarks.cpp BuildArena) les pose à ARENA_PIV du centre.
ARENA_PISTE = 22.0      # rayon de la piste (palissade)
ARENA_OUT = 37.5        # rayon extérieur (mur à arcades)
ARENA_PIV = 29.75       # pivot des modules d'anneau (milieu de 22..37,5)
ARENA_HALF = math.radians(7.5)
ARENA_ROWS = 9          # rangs de gradins (1,5 m de profondeur, 0,95 m de haut)


def _polar(r, a, z):
    """Point du repère de module d'anneau : rayon r depuis le centre de l'arène, angle a
    (rad) autour de −Y, hauteur z."""
    return Vector((r * math.sin(a), ARENA_PIV - r * math.cos(a), z))


def _ring_quad(part, r0, r1, a0, a1, z0, z1, kind, uv=0.5):
    """Face d'un anneau : 'top' (horizontale à z1 entre r0 et r1), 'in' (verticale à r0),
    'out' (verticale à r1), 'bot' (dessous à z0), 'side0' / 'side1' (bords d'angle a0 / a1)."""
    if kind == "top":
        q = [_polar(r0, a0, z1), _polar(r0, a1, z1), _polar(r1, a1, z1), _polar(r1, a0, z1)]
    elif kind == "bot":
        q = [_polar(r0, a1, z0), _polar(r0, a0, z0), _polar(r1, a0, z0), _polar(r1, a1, z0)]
    elif kind == "in":
        q = [_polar(r0, a1, z0), _polar(r0, a1, z1), _polar(r0, a0, z1), _polar(r0, a0, z0)]
    elif kind == "out":
        q = [_polar(r1, a0, z0), _polar(r1, a0, z1), _polar(r1, a1, z1), _polar(r1, a1, z0)]
    elif kind == "side0":
        q = [_polar(r0, a0, z0), _polar(r0, a0, z1), _polar(r1, a0, z1), _polar(r1, a0, z0)]
    else:
        q = [_polar(r1, a1, z0), _polar(r1, a1, z1), _polar(r0, a1, z1), _polar(r0, a1, z0)]
    L = (q[1] - q[0]).length
    W = (q[3] - q[0]).length
    part.quad(q[0], q[1], q[2], q[3], [(0, 0), (0, L * uv), (W * uv, L * uv), (W * uv, 0)])
    part.smooth = False


def _ring_block(part, r0, r1, a0, a1, z0, z1, n, faces=("top", "in", "out", "bot")):
    """Bloc d'anneau (r0..r1, a0..a1, z0..z1) en n tranches d'angle ; flancs d'angle ajoutés
    avec 'side' dans faces."""
    for k in range(n):
        b0 = a0 + (a1 - a0) * k / n
        b1 = a0 + (a1 - a0) * (k + 1) / n
        for f in faces:
            if f in ("top", "in", "out", "bot"):
                _ring_quad(part, r0, r1, b0, b1, z0, z1, f)
    if "side" in faces:
        _ring_quad(part, r0, r1, a0, a1, z0, z1, "side0")
        _ring_quad(part, r0, r1, a0, a1, z0, z1, "side1")


def _arena_rows(m, a0, a1, lod, n):
    """Gradins de l'anneau entre a0 et a1 : palissade de planches (piste), neuf rangs de
    pierre à assise de bois, dessous fermé à z = 0 (plafond des geôles)."""
    s = m.p("Stone_Wall")
    wood = m.p("Wood_Plank")
    # Palissade : planches de 2,6 m au bord de la piste, chaperon.
    _ring_block(wood, ARENA_PISTE, ARENA_PISTE + 0.3, a0, a1, 0.0, 2.6, n, ("in", "top", "side"))
    _ring_block(m.p("Wood_Dark"), ARENA_PISTE - 0.05, ARENA_PISTE + 0.35, a0, a1, 2.6, 2.75, n, ("in", "top", "out"))
    r = ARENA_PISTE + 0.3
    for k in range(ARENA_ROWS):
        r1 = 23.0 + 1.5 * (k + 1)
        z1 = 2.75 + 0.95 * k
        _ring_block(s, r, r1, a0, a1, 0.0, z1, n, ("top", "in", "bot", "side"))
        if lod == 0:   # assise de planches au bord du rang
            _ring_block(wood, r1 - 0.55, r1 - 0.05, a0, a1, z1, z1 + 0.06, n, ("top", "in"))
        r = r1
    # Dernier rang jusqu'au mur.
    zlast = 2.75 + 0.95 * (ARENA_ROWS - 1)
    _ring_block(s, r, ARENA_OUT - 1.0, a0, a1, 0.0, zlast, n, ("top", "bot", "side"))
    return zlast


def _arena_outer_wall(m, a0, a1, lod, n, z0=0.0):
    """Mur extérieur à arcades aveugles (1 m d'épaisseur, 12,5 m) et parapet."""
    s = m.p("Stone_Wall")
    c = m.p("Stone_Carved")
    _ring_block(s, ARENA_OUT - 1.0, ARENA_OUT, a0, a1, z0, 11.8, n, ("in", "out", "top", "bot", "side"))
    _ring_block(c, ARENA_OUT - 1.1, ARENA_OUT + 0.15, a0, a1, 11.8, 12.5, n, ("in", "out", "top", "bot"))
    if lod < 2:
        # Bandeau et pilastres en saillie sur la face extérieure (rythme des arcades).
        _ring_block(c, ARENA_OUT, ARENA_OUT + 0.18, a0, a1, 6.0, 6.45, n, ("out", "top", "bot"))
        for a in (a0 + 0.02, (a0 + a1) / 2, a1 - 0.02):
            h = 0.012
            _ring_block(c, ARENA_OUT, ARENA_OUT + 0.3, a - h, a + h, 0.0, 11.8, 1, ("out", "side"))
        if lod == 0:   # arcs aveugles : renfoncements sombres en plein cintre (2 par secteur)
            d = m.p("Wood_Dark")
            for k in range(2):
                am = a0 + (a1 - a0) * (k + 0.5) / 2
                hw = (a1 - a0) * 0.32
                _ring_block(d, ARENA_OUT + 0.01, ARENA_OUT + 0.02, am - hw, am + hw, 0.6, 4.4, 2, ("out",))
                for j in range(6):
                    t0, t1 = math.pi * j / 6, math.pi * (j + 1) / 6
                    rr = hw
                    b0, b1 = am - rr * math.cos(t0), am - rr * math.cos(t1)
                    lo, hi = sorted((b0, b1))
                    zt = 4.4 + 1.3 * min(math.sin(t0), math.sin(t1))
                    _ring_block(d, ARENA_OUT + 0.01, ARENA_OUT + 0.02, lo, hi, 4.4, zt + 0.15, 1, ("out",))


def p_arena_stands(spec, lod):
    """Module de gradins de l'arène (15°) : palissade de 2,6 m, neuf rangs, mur extérieur à
    arcades de 12,5 m ; dessous fermé à z = 0 (plafond des geôles creusées dessous)."""
    m = M()
    n = {0: 4, 1: 2, 2: 1}[lod]
    _arena_rows(m, -ARENA_HALF, ARENA_HALF, lod, n)
    _arena_outer_wall(m, -ARENA_HALF, ARENA_HALF, lod, n)
    return m.out()


def p_arena_gate(spec, lod):
    """Module d'entrée de l'arène (15°) : passage couvert de 5 m de large et 6 m de haut à
    travers les gradins ; gradins conservés de part et d'autre (rang par rang, au-delà des
    parois) ; piliers de gros blocs de part et d'autre à chaque bout (8 m côté piste, posés
    sur la piste ; 14 m dehors), linteaux."""
    m = M()
    n = {0: 2, 1: 1, 2: 1}[lod]
    hw = 2.5
    s = m.p("Stone_Wall")
    wood = m.p("Wood_Plank")
    r = ARENA_PISTE + 0.3
    for k in range(ARENA_ROWS):
        r1 = 23.0 + 1.5 * (k + 1)
        z1 = 2.75 + 0.95 * k
        a = math.asin(min(0.99, (hw + 0.6) / r))
        if a < ARENA_HALF - 0.005:
            for lo, hi in ((-ARENA_HALF, -a), (a, ARENA_HALF)):
                _ring_block(s, r, r1, lo, hi, 0.0, z1, n, ("top", "in", "bot", "side"))
                if lod == 0:
                    _ring_block(wood, r1 - 0.55, r1 - 0.05, lo, hi, z1, z1 + 0.06, n, ("top", "in"))
        r = r1
    eo = math.asin((hw + 0.6) / ARENA_OUT)
    _arena_outer_wall(m, -ARENA_HALF, -eo, lod, n)
    _arena_outer_wall(m, eo, ARENA_HALF, lod, n)
    blk = m.p("Rock_Ochre")
    y_in, y_out = ARENA_PIV - ARENA_PISTE, ARENA_PIV - ARENA_OUT
    # Parois et couvrement du passage (de la piste au dehors), mur au-dessus de l'entrée.
    for x in (-hw - 0.6, hw):
        box(s, (x, y_out, 0.0), (x + 0.6, y_in, 6.6))
    box(s, (-hw - 0.6, y_out, 6.0), (hw + 0.6, y_in, 6.8))
    box(s, (-hw - 0.6, y_out, 6.8), (hw + 0.6, y_out + 1.0, 11.8))
    box(m.p("Stone_Carved"), (-hw - 0.8, y_out - 0.2, 11.8), (hw + 0.8, y_out + 1.0, 12.5))
    # Piliers de gros blocs (T2 p.10) : assises irrégulières, linteau.
    rng = random.Random(spec.get("seed", 1))
    for (y0, H) in ((y_in, 8.0), (y_out - 1.6, 14.0)):
        for sx in (-1, 1):
            x0 = hw if sx > 0 else -hw - 1.7
            z = 0.0
            while z < H - 0.01:
                h = min(H - z, rng.uniform(1.1, 1.7))
                j = rng.uniform(-0.08, 0.08) if lod == 0 else 0.0
                box(blk, (x0 + j, y0 + j, z), (x0 + 1.7 - j, y0 + 1.6 - j, z + h - (0.04 if lod == 0 else 0.0)), uv=0.4)
                z += h
        box(blk, (-hw - 2.0, y0 - 0.1, H), (hw + 2.0, y0 + 1.7, H + 1.4), uv=0.4)
    return m.out()


def p_arena_foundation(spec, lod):
    """Fondations d'un module de gradins (15°), origine à leur pied : jupes de pierre de
    5,25 m sous la palissade et sous le mur extérieur ; posées par le générateur à −5,2 m
    au-dessus de la fosse des geôles (le sol creusé ne laisse aucun jour)."""
    m = M()
    n = {0: 3, 1: 2, 2: 1}[lod]
    s = m.p("Stone_Wall")
    _ring_block(s, ARENA_PISTE - 0.05, ARENA_PISTE + 0.6, -ARENA_HALF, ARENA_HALF, 0.0, 5.25, n, ("in", "out", "side"))
    _ring_block(s, ARENA_OUT - 1.0, ARENA_OUT + 0.05, -ARENA_HALF, ARENA_HALF, 0.0, 5.25, n, ("in", "out", "side"))
    return m.out()


def p_arena_post_brazier(spec, lod):
    """Poteau de bois de la palissade surmonté d'une corbeille à feu de fer (T2 p.10),
    H 4,9 m, braises."""
    m = M()
    w = m.p("Wood_Dark")
    box(w, (-0.16, -0.16, 0.0), (0.16, 0.16, 4.3))
    i = m.p("Iron")
    lathe(i, [(0.08, 4.25), (0.45, 4.6), (0.5, 4.9)], S(lod, 10))
    box(i, (-0.2, -0.2, 4.1), (0.2, 0.2, 4.3))
    if lod < 2:
        lathe(m.p("Brick_Red"), [(0.0, 4.82), (0.42, 4.85), (0.2, 5.0), (0.0, 5.05)], S(lod, 8))
    return m.out()


def p_bridge_stone_pier(spec, lod):
    """Pile de long pont (T2 p.7) : 1,6 m le long du pont (X), 6,4 m en travers, 6 m de haut
    (origine au pied) ; avant-becs triangulaires des deux côtés jusqu'à 4 m, chaperon."""
    m = M()
    s = m.p("Stone_Wall")
    c = m.p("Stone_Carved")
    box(s, (-0.8, -3.2, 0.0), (0.8, 3.2, 6.0))
    for sy in (-1, 1):
        y0 = sy * 3.2
        y1 = sy * 4.6
        pts = [Vector((-0.8, y0, 0.0)), Vector((0.8, y0, 0.0)), Vector((0.0, y1, 0.0))]
        top = [p + Vector((0, 0, 4.0)) for p in pts]
        part = s
        i = len(part.v)
        # Trois flancs du prisme et chaperon en pointe.
        for a, b in ((0, 2), (2, 1)):
            part.quad(pts[a], pts[b], top[b], top[a], [(0, 0), (1.6, 0), (1.6, 4.0), (0, 4.0)])
        apex = Vector((0.0, y0 + (y1 - y0) * 0.3, 5.2))
        for a, b in ((0, 2), (2, 1)):
            i = len(c.v)
            c.v += [top[a], top[b], apex]
            c.f.append((i, i + 1, i + 2) if sy > 0 else (i, i + 2, i + 1))
            c.uv.append([(0, 0), (1, 0), (0.5, 1)])
            c.cn.append(None)
    box(c, (-0.95, -3.35, 5.7), (0.95, 3.35, 6.0))
    return m.out()


def p_church_nave(spec, lod):
    """Nef de cathédrale gothique (T2 p.7, réinterprétation générique) : 30 m (Y) x 14 m (X),
    murs de 15 m à contreforts et lancettes, toit d'ardoise très pentu (faîte à 27 m),
    pignon de façade (−Y) à rosace, chevet à trois pans (+Y). Origine au pied, au centre."""
    m = M()
    s = m.p("Stone_Wall")
    c = m.p("Stone_Carved")
    sl = m.p("Shingle_Slate")
    W, L, H, R = 7.0, 15.0, 15.0, 27.0
    box(s, (-W, -L, 0.0), (W, L - 4.0, H), uv=0.3)
    # Chevet à trois pans (+Y).
    ch = [Vector((-W, L - 4.0, 0)), Vector((-W * 0.55, L, 0)), Vector((W * 0.55, L, 0)), Vector((W, L - 4.0, 0))]
    for a, b in zip(ch, ch[1:]):
        s.quad(a, b, b + Vector((0, 0, H)), a + Vector((0, 0, H)), [(0, 0), (1, 0), (1, 1), (0, 1)])
    # Toit : deux pans d'ardoise et croupe du chevet, pignon de façade plein.
    e = 0.6
    A0, A1 = Vector((-W - e, -L - 0.4, H - 0.3)), Vector((W + e, -L - 0.4, H - 0.3))
    B0, B1 = Vector((-W - e, L - 4.0, H - 0.3)), Vector((W + e, L - 4.0, H - 0.3))
    T0, T1 = Vector((0, -L - 0.4, R)), Vector((0, L - 4.0, R))
    sl.quad(A0, B0, T1, T0, [(0, 0), (4, 0), (4, 3), (0, 3)])
    sl.quad(B1, A1, T0, T1, [(0, 0), (4, 0), (4, 3), (0, 3)])
    apexB = Vector((0, L + 0.4, H - 0.3))
    for a, b in ((B0, T1), (T1, B1)):
        i = len(sl.v)
        sl.v += [a, apexB, b]
        sl.f.append((i, i + 1, i + 2))
        sl.uv.append([(0, 0), (1, 0), (0.5, 1)])
        sl.cn.append(None)
    i = len(s.v)
    s.v += [Vector((-W, -L, H)), Vector((W, -L, H)), Vector((0, -L, R - 0.4))]
    s.f.append((i, i + 1, i + 2))
    s.uv.append([(0, 0), (1, 0), (0.5, 1)])
    s.cn.append(None)
    # Contreforts (6 par côté) à ressauts et pinacles.
    for k in range(6):
        y = -L + 2.0 + k * 4.6
        for sx in (-1, 1):
            x0 = sx * W
            box(c, (min(x0, x0 + sx * 1.6), y - 0.5, 0.0), (max(x0, x0 + sx * 1.6), y + 0.5, H * 0.6))
            box(c, (min(x0, x0 + sx * 0.9), y - 0.45, H * 0.6), (max(x0, x0 + sx * 0.9), y + 0.45, H + 0.6))
            if lod < 2:
                _pinnacle(c, x0 + sx * 0.45, y, H + 0.6, 0.35, 2.6, lod)
    # Lancettes (verre) entre les contreforts, rosace de façade.
    if lod < 2:
        gl = m.p("Glass_Amber")
        for k in range(5):
            y = -L + 4.3 + k * 4.6
            for sx in (-1, 1):
                x = sx * (W + 0.02)
                q = [Vector((x, y - 0.9, 3.5)), Vector((x, y + 0.9, 3.5)), Vector((x, y + 0.9, 11.0)), Vector((x, y - 0.9, 11.0))]
                if sx < 0:
                    q = [q[1], q[0], q[3], q[2]]
                gl.quad(q[0], q[1], q[2], q[3], [(0, 0), (1, 0), (1, 1), (0, 1)])
        rose = [Vector((3.2 * math.cos(a), -L - 0.03, 18.5 + 3.2 * math.sin(a))) for a in [k * 2 * math.pi / S(lod, 16) for k in range(S(lod, 16))]]
        i = len(gl.v)
        gl.v += rose
        gl.f.append(tuple(range(i, i + len(rose)))[::-1])
        gl.uv.append([(0.5 + 0.5 * math.cos(k * 2 * math.pi / len(rose)), 0.5 + 0.5 * math.sin(k * 2 * math.pi / len(rose))) for k in range(len(rose))])
        gl.cn.append(None)
    # Faîtage et crête.
    box(c, (-0.15, -L - 0.4, R - 0.2), (0.15, L - 4.0, R + 0.25))
    return m.out()


def p_spire_church(spec, lod):
    """Clocher-porche à haute flèche de pierre (T2 p.7, flèche dominant le piton) : tour
    carrée de 6,4 m jusqu'à 24 m (abat-sons), galerie, flèche octogonale à crochets jusqu'à
    52 m, portail en ogive au pied (−Y). Origine au pied, au centre."""
    m = M()
    s = m.p("Stone_Wall")
    c = m.p("Stone_Carved")
    box(s, (-3.2, -3.2, 0.0), (3.2, 3.2, 24.0), uv=0.3)
    for cx in (-3.2, 3.2):
        for cy in (-3.2, 3.2):
            box(c, (cx - 0.6, cy - 0.6, 0.0), (cx + 0.6, cy + 0.6, 24.6))
            if lod < 2:
                _pinnacle(c, cx, cy, 24.6, 0.5, 3.4, lod)
    # Portail en ogive (renfoncement sombre) et abat-sons.
    d = m.p("Wood_Dark")
    box(d, (-1.4, -3.25, 0.0), (1.4, -3.21, 4.6))
    if lod < 2:
        i = len(d.v)
        d.v += [Vector((-1.4, -3.25, 4.6)), Vector((1.4, -3.25, 4.6)), Vector((0, -3.25, 6.6))]
        d.f.append((i, i + 1, i + 2))
        d.uv.append([(0, 0), (1, 0), (0.5, 1)])
        d.cn.append(None)
        for k in range(4):
            a = k * math.pi / 2
            ux, uy = math.cos(a), math.sin(a)
            ctr = Vector((ux * 3.22, uy * 3.22, 0))
            t = Vector((-uy, ux, 0))
            q = [ctr - t * 1.0 + Vector((0, 0, 16.5)), ctr + t * 1.0 + Vector((0, 0, 16.5)),
                 ctr + t * 1.0 + Vector((0, 0, 21.5)), ctr - t * 1.0 + Vector((0, 0, 21.5))]
            d.quad(q[0], q[1], q[2], q[3], [(0, 0), (1, 0), (1, 1), (0, 1)])
    box(c, (-3.7, -3.7, 24.0), (3.7, 3.7, 24.6))
    lathe(c, [(3.0, 24.6), (2.6, 27.0), (0.06, 51.0)], 8)
    if lod == 0:   # crochets sur les arêtes de la flèche
        for k in range(8):
            a = (k + 0.5) * math.pi / 4
            for j in range(5):
                z = 28.0 + j * 4.4
                r = 2.6 * (51.0 - z) / 24.0 + 0.1
                box(c, (r * math.cos(a) - 0.12, r * math.sin(a) - 0.12, z), (r * math.cos(a) + 0.12, r * math.sin(a) + 0.12, z + 0.5))
    cyl(m.p("Brass"), (0, 0, 50.6), (0, 0, 52.0), 0.1, 0.03, S(lod, 6))
    return m.out()


def p_gargoyle(spec, lod):
    """Gargouille de pierre (T2 p.10 : bête accroupie sur un toit, générique) : corps
    trapu en saillie vers −Y, gueule ouverte (gargouille d'écoulement), ailerons repliés,
    socle ; 1,5 m de long, origine au pied du socle."""
    m = M()
    s = m.p("Stone_Carved")
    box(s, (-0.3, -0.2, 0.0), (0.3, 0.4, 0.25))
    body = [Vector((0, 0.3, 0.45)), Vector((0, 0.0, 0.55)), Vector((0, -0.45, 0.6)), Vector((0, -0.95, 0.62))]
    API["tube"](s, body, [0.26, 0.3, 0.24, 0.16], S(lod, 8), 0.5, True)
    if lod < 2:
        for sx in (-1, 1):   # pattes avant et ailerons
            cyl(s, (sx * 0.18, -0.35, 0.25), (sx * 0.2, -0.45, 0.55), 0.07, 0.06, S(lod, 6))
            API["tube"](s, [Vector((sx * 0.15, 0.15, 0.7)), Vector((sx * 0.4, 0.25, 0.95)), Vector((sx * 0.35, 0.45, 1.05))],
                        [0.08, 0.05, 0.02], S(lod, 5), 0.5)
        cyl(s, (0, -0.95, 0.66), (0, -1.25, 0.6), 0.12, 0.08, S(lod, 6))   # mâchoire supérieure
        cyl(s, (0, -0.95, 0.56), (0, -1.2, 0.5), 0.08, 0.05, S(lod, 6))    # mâchoire inférieure
        API["tube"](s, [Vector((0, 0.35, 0.4)), Vector((0, 0.6, 0.3)), Vector((0, 0.75, 0.42))], [0.08, 0.05, 0.02], S(lod, 5), 0.5)
    return m.out()


def register(api):
    API.update(api)
    out = {}
    for k, v in dict(globals()).items():
        if k.startswith("p_") and callable(v):
            out[k[2:]] = v
    return out
