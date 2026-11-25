"""Pièces sculptées par projection radiale (sans Blender).

Les maillages produits par TRELLIS.2 (des dizaines de millions de triangles,
topologie irrégulière : bords ouverts, arêtes non manifold) ne se décimant pas
proprement, on ne garde d'eux que la FORME et la COULEUR :

* la surface TRELLIS (ajustée au gabarit du corps par ``blender_scripts/fit_high.py``)
  est résumée en un champ de rayons autour du centre de la tête, sur une grille
  sphérique (azimut, élévation) ;
* les VISAGES reprennent la topologie propre de la tête procédurale
  (``heads.py`` : anneau du cou raccordé au corps, UV uniques), dont chaque sommet
  est porté sur la peau TRELLIS ;
* les COIFFURES, BARBES et MOUSTACHES sont des surfaces en grille sphérique sur la
  zone où TRELLIS a placé des poils (couleur sombre) ; leur bord est posé au ras
  de la peau (aucun jour entre cheveux et cuir chevelu) ;
* l'albedo et la normal map sont cuits depuis les points TRELLIS.

Les éléments qui ne sont pas visibles depuis le centre de la tête (queue de
cheval, chignon, tresses) gardent leur géométrie procédurale (``hair.py``).
"""

from __future__ import annotations

from pathlib import Path

import numpy as np
from PIL import Image
from scipy.ndimage import distance_transform_edt, median_filter, binary_closing, binary_opening
from scipy.spatial import cKDTree

from geom import Mesh, grid
from heads import head_point

# Grille sphérique : 0,75° par case.
NT, NP = 480, 240
PHI_MIN, PHI_MAX = -np.pi / 2, np.pi / 2


# ----------------------------------------------------------------------
# Données TRELLIS
# ----------------------------------------------------------------------
class Cloud:
    """Surface TRELLIS ajustée : sommets, normales, couleurs (sRGB 0..1)."""

    def __init__(self, path: Path, colorspace: str = "srgb") -> None:
        d = np.load(path)
        self.v = d["v"].astype(np.float64)
        c = d["c"].astype(np.float64)
        if c.max() > 1.5:
            c = c / 255.0
        elif colorspace == "linear":
            c = np.where(c <= 0.0031308, c * 12.92, 1.055 * np.power(np.clip(c, 0, 1), 1 / 2.4) - 0.055)
        self.c = np.clip(c, 0, 1)
        # Normales : l'orientation des triangles TRELLIS n'est pas cohérente
        # (enroulements mélangés) -> normales estimées localement (ACP) au besoin.
        self.n = None
        self._tree = None

    def pca_normals(self, sel: np.ndarray, center: np.ndarray, k: int = 16) -> np.ndarray:
        """Normales des points ``sel`` par analyse en composantes principales de leurs
        ``k`` voisins, orientées vers l'extérieur de la tête."""
        p = self.v[sel]
        tree = cKDTree(p)
        _d, idx = tree.query(p, k=k)
        nb = p[idx] - p[idx].mean(axis=1, keepdims=True)
        cov = np.einsum("nki,nkj->nij", nb, nb)
        _w, vec = np.linalg.eigh(cov)
        n = vec[:, :, 0]
        flip = np.einsum("ij,ij->i", n, p - center) < 0
        n[flip] *= -1
        return n

    @property
    def tree(self) -> cKDTree:
        if self._tree is None:
            self._tree = cKDTree(self.v)
        return self._tree

    def lum(self) -> np.ndarray:
        return self.c @ np.array([0.299, 0.587, 0.114])

    def hair_mask(self, tone: str = "black") -> np.ndarray:
        """Points de poils. Références SDXL : cheveux « noir de jais » (``black``)
        ou gris (``grey``, quelques coiffures longues). La peau est plus claire et
        plus saturée (teinte chaude)."""
        c = self.c
        mx, mn = c.max(1), c.min(1)
        sat = np.where(mx > 1e-4, (mx - mn) / np.maximum(mx, 1e-4), 0)
        L = self.lum()
        if tone == "grey":
            return (L < 0.14) | ((sat < 0.14) & (L < 0.55))
        return (L < 0.15) | ((L < 0.22) & (sat < 0.25))


def spherical(p: np.ndarray, center: np.ndarray) -> tuple:
    d = p - center
    r = np.linalg.norm(d, axis=1)
    u = d / np.maximum(r[:, None], 1e-12)
    th = np.arctan2(u[:, 0], u[:, 1])                  # 0 = +Y (avant)
    ph = np.arcsin(np.clip(u[:, 2], -1, 1))
    return th, ph, r


def to_cells(th: np.ndarray, ph: np.ndarray) -> tuple:
    i = np.clip(((th + np.pi) / (2 * np.pi) * NT).astype(int), 0, NT - 1)
    j = np.clip(((ph - PHI_MIN) / (PHI_MAX - PHI_MIN) * NP).astype(int), 0, NP - 1)
    return i, j


def direction(th: np.ndarray, ph: np.ndarray) -> np.ndarray:
    return np.stack([np.sin(th) * np.cos(ph), np.cos(th) * np.cos(ph), np.sin(ph)], axis=-1)


class Field:
    """Rayon r(azimut, élévation) sur la grille sphérique (NaN = vide)."""

    def __init__(self, center: np.ndarray, grid_r: np.ndarray) -> None:
        self.center = center
        self.r = grid_r

    @classmethod
    def from_points(cls, p: np.ndarray, center: np.ndarray, mode: str) -> "Field":
        th, ph, r = spherical(p, center)
        i, j = to_cells(th, ph)
        g = np.full((NP, NT), np.inf if mode == "min" else -np.inf)
        if mode == "min":
            np.minimum.at(g, (j, i), r)
        else:
            np.maximum.at(g, (j, i), r)
        g[~np.isfinite(g)] = np.nan
        return cls(center, g)

    def mask(self) -> np.ndarray:
        return ~np.isnan(self.r)

    def filled(self, smooth: int = 3) -> "Field":
        """Cases vides remplies par la case pleine la plus proche, puis médiane."""
        m = self.mask()
        if not m.any():
            return self
        # Grille répétée en azimut pour le raccord -pi/+pi.
        rr = np.concatenate([self.r[:, -NT // 4:], self.r, self.r[:, :NT // 4]], axis=1)
        mm = ~np.isnan(rr)
        _d, (iy, ix) = distance_transform_edt(~mm, return_indices=True)
        out = rr[iy, ix]
        if smooth > 1:
            out = median_filter(out, size=smooth)
        return Field(self.center, out[:, NT // 4:NT // 4 + NT])

    def sample(self, th: np.ndarray, ph: np.ndarray) -> np.ndarray:
        """Interpolation bilinéaire (azimut périodique)."""
        x = (th + np.pi) / (2 * np.pi) * NT - 0.5
        y = np.clip((ph - PHI_MIN) / (PHI_MAX - PHI_MIN) * NP - 0.5, 0, NP - 1.001)
        x0 = np.floor(x).astype(int)
        y0 = np.floor(y).astype(int)
        fx, fy = x - x0, y - y0
        x0 %= NT
        x1 = (x0 + 1) % NT
        g = self.r
        return (g[y0, x0] * (1 - fx) * (1 - fy) + g[y0, x1] * fx * (1 - fy)
                + g[y0 + 1, x0] * (1 - fx) * fy + g[y0 + 1, x1] * fx * fy)


def skin_field(cloud: Cloud, center: np.ndarray) -> Field:
    """Peau d'une tête chauve : surface la plus éloignée du centre dans chaque
    direction (TRELLIS modélise aussi l'intérieur : dents, globes oculaires…).
    Les oreilles (zone latérale, à hauteur des yeux) sont retirées puis la peau
    du crâne y est interpolée : les oreilles restent des pièces à part."""
    raw = Field.from_points(cloud.v, center, "max")
    base = raw.filled(1)
    smooth = median_filter(base.r, size=(9, 21), mode="wrap")
    th = (np.arange(NT) + 0.5) / NT * 2 * np.pi - np.pi
    ph = PHI_MIN + (np.arange(NP) + 0.5) / NP * (PHI_MAX - PHI_MIN)
    TH, PH = np.meshgrid(th, ph)
    ear_zone = (np.abs(np.abs(TH) - np.pi / 2) < np.radians(38)) & (PH > np.radians(-35)) & (PH < np.radians(12))
    g = raw.r.copy()
    g[ear_zone & (base.r - smooth > 0.004)] = np.nan
    f = Field(center, g).filled(5)
    # Sous la mâchoire (rayons rasants, col du tee-shirt) : lissage plus fort.
    low = PH < np.radians(-12)
    strong = median_filter(f.r, size=(11, 11), mode="wrap")
    f.r = np.where(low, strong, f.r)
    return f


def near_field(cloud: Cloud, field: Field, tol: float = 0.003) -> np.ndarray:
    """Points du nuage à moins de ``tol`` mètres de la surface ``field``."""
    th, ph, r = spherical(cloud.v, field.center)
    return np.abs(r - field.sample(th, ph)) < tol


# ----------------------------------------------------------------------
# Visages : tête procédurale portée sur la peau TRELLIS
# ----------------------------------------------------------------------
def face_template(shape, eye_world: np.ndarray, nu: int, nv: int) -> Mesh:
    """Tête procédurale à UV uniques (couture à l'arrière du crâne)."""
    U, V, F = grid(nu, nv, False)
    P = head_point(shape, U - 0.5, V) + eye_world      # U=0.5 -> avant
    uv = np.stack([U, V], axis=1)
    m = Mesh(P, F, uv)
    a, b, c = m.v[m.f[:, 0]], m.v[m.f[:, 1]], m.v[m.f[:, 2]]
    ctr = (a + b + c) / 3 - (eye_world + np.array([0.0, 0.0, -0.02]))
    if np.sum(np.einsum("ij,ij->i", np.cross(b - a, c - a), ctr)) < 0:
        m.f = m.f[:, ::-1].copy()
    return m


def project_face(m: Mesh, skin: Field, neck_z: float, blend: float = 0.075) -> Mesh:
    """Porte chaque sommet sur la peau TRELLIS ; le cou reste raccordé au corps
    (fondu vers la tête procédurale sur ``blend`` mètres au-dessus de ``neck_z``)."""
    th, ph, r = spherical(m.v, skin.center)
    rs = skin.sample(th, ph)
    t = np.clip((m.v[:, 2] - neck_z - 0.02) / blend, 0, 1)
    w = t * t * (3 - 2 * t)
    # Nuque : le buste TRELLIS y est irrégulier (rayons rasants, col) -> on garde
    # le gabarit sous le lobe des oreilles, à l'arrière.
    back = np.clip((skin.center[1] + 0.01 - m.v[:, 1]) / 0.03, 0, 1)
    low = np.clip((skin.center[2] - 0.045 - m.v[:, 2]) / 0.04, 0, 1)
    w = w * (1 - back * low)
    # Garde-fou : la peau TRELLIS ne s'écarte pas de plus de 3,5 cm du gabarit
    # (directions qui rasent les épaules, trous du nuage).
    rs = np.where(np.isfinite(rs) & (np.abs(rs - r) < 0.035), rs, r)
    nr = r * (1 - w) + rs * w
    out = Mesh(skin.center + direction(th, ph) * nr[:, None], m.f.copy(), m.uv.copy())
    return out


# ----------------------------------------------------------------------
# Pilosité : surface en grille sphérique sur la zone de poils
# ----------------------------------------------------------------------
def hair_fields(cloud: Cloud, center: np.ndarray, region, tone: str = "black", zmin: float = -1e9) -> tuple:
    """Champ extérieur des poils et masque angulaire de la zone de poils.

    ``region(th, ph) -> bool`` restreint les directions autorisées (écarte les
    sourcils, les yeux…) ; ``zmin`` écarte les points trop bas (tee-shirt sombre
    des références pour les coiffures courtes)."""
    hm = cloud.hair_mask(tone) & (cloud.v[:, 2] > zmin)
    th, ph, r = spherical(cloud.v[hm], center)
    keep = region(th, ph)
    f_out = Field.from_points(cloud.v[hm][keep], center, "max")
    mask = f_out.mask()
    pad = 8
    rr = np.concatenate([mask[:, -pad:], mask, mask[:, :pad]], axis=1)
    rr = binary_opening(binary_closing(rr, iterations=6), iterations=2)
    from scipy.ndimage import binary_fill_holes
    rr = binary_fill_holes(rr)
    mask = rr[:, pad:pad + NT]
    return f_out.filled(5), mask


def hair_alpha(mask: np.ndarray, m: Mesh, size: int) -> np.ndarray:
    """Masque de découpe (alpha) de la texture d'une pièce de poils : contour fin de
    la zone de poils (grille 0,75°) adouci, à la place des marches de la grille du
    maillage. Les UV de ``m`` sont celles de hair_lods (boîte angulaire)."""
    from scipy.ndimage import gaussian_filter, map_coordinates
    tri_id, bary = rasterize(m.uv, m.f, size)
    sel = tri_id >= 0
    pos = np.einsum("ij,ijk->ik", bary[sel], m.v[m.f[tri_id[sel]]])
    th, ph, _r = spherical(pos, m.center)          # m.center : posé par hair_lods
    soft = gaussian_filter(mask.astype(np.float64), sigma=1.2, mode="wrap")
    x = (th + np.pi) / (2 * np.pi) * NT - 0.5
    y = (ph - PHI_MIN) / (PHI_MAX - PHI_MIN) * NP - 0.5
    a = map_coordinates(soft, [y, x % NT], order=1, mode="nearest")
    out = np.ones((size, size))
    out[sel] = np.clip((a - 0.35) / 0.3, 0, 1)
    _d, (iy, ix) = distance_transform_edt(~sel, return_indices=True)
    return out[iy, ix]


def hair_lods(hair: Field, mask: np.ndarray, under: Field, budgets: list, gap: float = 0.0012) -> list:
    """Surfaces de poils (une par budget de triangles) sur la grille sphérique.

    Les UV dépendent seulement des angles (boîte angulaire de la zone) : les trois
    niveaux de détail partagent la même texture. La couture à l'arrière de la tête
    (azimut ±180°) est refermée avec des sommets dupliqués (UV u = 0 / 1). Le bord
    de la zone est posé à 0,5 mm de la peau ``under`` (aucun jour visible)."""
    J0, I0 = np.nonzero(mask)
    wraps = mask[:, 0].any() and mask[:, -1].any()
    th_all = (I0 + 0.5) / NT * 2 * np.pi - np.pi
    ph_all = PHI_MIN + (J0 + 0.5) / NP * (PHI_MAX - PHI_MIN)
    tmin, tmax = (-np.pi, np.pi) if wraps else (th_all.min(), th_all.max())
    pmin, pmax = ph_all.min(), ph_all.max()
    out = []
    for budget in budgets:
        step = max(1, int(np.ceil(np.sqrt(2.0 * mask.sum() / budget))))
        while True:
            nx = NT // step
            ny = NP // step
            sub = np.zeros((ny, nx + 1), bool)
            for jj in range(ny):
                for ii in range(nx):
                    sub[jj, ii] = mask[jj * step:(jj + 1) * step, ii * step:(ii + 1) * step].any()
            if wraps:
                sub[:, nx] = sub[:, 0]          # colonne dupliquée (couture)
            quad = sub[:-1, :-1] & sub[1:, :-1] & sub[:-1, 1:] & sub[1:, 1:]
            if 2 * int(quad.sum()) <= budget:
                break
            step += 1
        idx = -np.ones(sub.shape, np.int64)
        idx[sub] = np.arange(int(sub.sum()))
        J, I = np.nonzero(sub)
        tc = I * step / NT * 2 * np.pi - np.pi + (0 if wraps else step / NT * np.pi)
        pc = PHI_MIN + (J * step + step / 2) / NP * (PHI_MAX - PHI_MIN)
        tcs = np.where(tc > np.pi, tc - 2 * np.pi, tc)
        rh = hair.sample(tcs, pc)
        rs = under.sample(tcs, pc)
        inner = np.zeros_like(sub)
        inner[1:-1, 1:-1] = sub[1:-1, 1:-1] & sub[:-2, 1:-1] & sub[2:, 1:-1] & sub[1:-1, :-2] & sub[1:-1, 2:]
        if wraps:   # la couture n'est pas un bord
            inner[1:-1, 0] = sub[1:-1, 0] & sub[:-2, 0] & sub[2:, 0] & sub[1:-1, 1] & sub[1:-1, nx - 1]
            inner[:, nx] = inner[:, 0]
        grid_border = (sub & ~inner)[J, I]
        qy, qx = np.nonzero(quad)
        a, b, c, d = idx[qy, qx], idx[qy, qx + 1], idx[qy + 1, qx], idx[qy + 1, qx + 1]
        f = np.concatenate([np.stack([a, c, b], 1), np.stack([b, c, d], 1)])
        # Bord réel du maillage (arêtes d'une seule face) ; la couture dupliquée
        # n'en fait pas partie sauf si elle est aussi au bord de la zone.
        e = np.sort(np.concatenate([f[:, [0, 1]], f[:, [1, 2]], f[:, [2, 0]]]), axis=1)
        uniq, cnt = np.unique(e, axis=0, return_counts=True)
        mesh_border = np.zeros(len(J), bool)
        mesh_border[np.unique(uniq[cnt == 1].ravel())] = True
        seam = (I == 0) | (I == nx) if wraps else np.zeros(len(J), bool)
        border = mesh_border & (~seam | grid_border)
        nr = np.maximum(rh, rs + gap)
        nr[border] = rs[border] + 0.0005
        v = under.center + direction(tcs, pc) * nr[:, None]
        u = np.clip((tc - tmin) / max(tmax - tmin, 1e-6), 0, 1)
        vv = np.clip(1.0 - (pc - pmin) / max(pmax - pmin, 1e-6), 0, 1)
        m = Mesh(v, f, np.stack([u, vv], 1))
        fa, fb, fc = m.v[m.f[:, 0]], m.v[m.f[:, 1]], m.v[m.f[:, 2]]
        if np.sum(np.einsum("ij,ij->i", np.cross(fb - fa, fc - fa), (fa + fb + fc) / 3 - under.center)) < 0:
            m.f = m.f[:, ::-1].copy()
        used = np.unique(m.f)
        remap = -np.ones(len(m.v), np.int64)
        remap[used] = np.arange(len(used))
        mm = Mesh(m.v[used], remap[m.f], m.uv[used])
        mm.center = under.center
        out.append(mm)
    return out


# ----------------------------------------------------------------------
# Cuisson (albedo, normal map) depuis les points TRELLIS
# ----------------------------------------------------------------------
def vertex_normals(v: np.ndarray, f: np.ndarray) -> np.ndarray:
    a, b, c = v[f[:, 0]], v[f[:, 1]], v[f[:, 2]]
    fn = np.cross(b - a, c - a)
    n = np.zeros_like(v)
    for k in range(3):
        np.add.at(n, f[:, k], fn)
    return n / np.maximum(np.linalg.norm(n, axis=1, keepdims=True), 1e-12)


def rasterize(uv: np.ndarray, f: np.ndarray, size: int) -> tuple:
    """Pour chaque texel couvert : triangle et coordonnées barycentriques (UV glTF)."""
    tri_id = np.full((size, size), -1, np.int64)
    bary = np.zeros((size, size, 3))
    P = uv * size - 0.5
    for t, (i0, i1, i2) in enumerate(f):
        a, b, c = P[i0], P[i1], P[i2]
        x0 = int(max(np.floor(min(a[0], b[0], c[0])), 0)); x1 = int(min(np.ceil(max(a[0], b[0], c[0])), size - 1))
        y0 = int(max(np.floor(min(a[1], b[1], c[1])), 0)); y1 = int(min(np.ceil(max(a[1], b[1], c[1])), size - 1))
        if x1 < x0 or y1 < y0:
            continue
        d = (b[1] - c[1]) * (a[0] - c[0]) + (c[0] - b[0]) * (a[1] - c[1])
        if abs(d) < 1e-12:
            continue
        xs, ys = np.meshgrid(np.arange(x0, x1 + 1), np.arange(y0, y1 + 1))
        w0 = ((b[1] - c[1]) * (xs - c[0]) + (c[0] - b[0]) * (ys - c[1])) / d
        w1 = ((c[1] - a[1]) * (xs - c[0]) + (a[0] - c[0]) * (ys - c[1])) / d
        w2 = 1 - w0 - w1
        inside = (w0 >= -0.01) & (w1 >= -0.01) & (w2 >= -0.01)
        tri_id[ys[inside], xs[inside]] = t
        bary[ys[inside], xs[inside]] = np.stack([w0[inside], w1[inside], w2[inside]], 1)
    return tri_id, bary


def bake(cloud: Cloud, sel: np.ndarray, m: Mesh, size: int, center: np.ndarray) -> tuple:
    """Albedo et normal map (espace tangent du moteur : T = dP/du, B = dP/dv sur les
    UV glTF, cf. gbuffer_geometry.frag) de ``m`` depuis les points ``sel`` du nuage."""
    pts, cols = cloud.v[sel], cloud.c[sel]
    nrms = cloud.pca_normals(sel, center)
    tree = cKDTree(pts)
    tri_id, bary = rasterize(m.uv, m.f, size)
    mask = tri_id >= 0
    t = tri_id[mask]
    w = bary[mask]
    pos = np.einsum("ij,ijk->ik", w, m.v[m.f[t]])
    vn = vertex_normals(m.v, m.f)
    nrm = np.einsum("ij,ijk->ik", w, vn[m.f[t]])
    nrm /= np.maximum(np.linalg.norm(nrm, axis=1, keepdims=True), 1e-12)
    dist, idx = tree.query(pos, k=6)
    wk = 1.0 / np.maximum(dist, 1e-5)
    wk /= wk.sum(1, keepdims=True)
    col = np.einsum("ij,ijk->ik", wk, cols[idx])
    hn = np.einsum("ij,ijk->ik", wk, nrms[idx])
    hn /= np.maximum(np.linalg.norm(hn, axis=1, keepdims=True), 1e-12)
    p0, p1, p2 = m.v[m.f[:, 0]], m.v[m.f[:, 1]], m.v[m.f[:, 2]]
    u0, u1, u2 = m.uv[m.f[:, 0]], m.uv[m.f[:, 1]], m.uv[m.f[:, 2]]
    e1, e2, d1, d2 = p1 - p0, p2 - p0, u1 - u0, u2 - u0
    r = d1[:, 0] * d2[:, 1] - d2[:, 0] * d1[:, 1]
    r = np.where(np.abs(r) < 1e-14, 1e-14, r)
    T = ((e1 * d2[:, 1:2] - e2 * d1[:, 1:2]) / r[:, None])[t]
    B = ((e2 * d1[:, 0:1] - e1 * d2[:, 0:1]) / r[:, None])[t]
    T = T - nrm * np.sum(T * nrm, 1, keepdims=True)
    T /= np.maximum(np.linalg.norm(T, axis=1, keepdims=True), 1e-12)
    B = B - nrm * np.sum(B * nrm, 1, keepdims=True) - T * np.sum(B * T, 1, keepdims=True)
    B /= np.maximum(np.linalg.norm(B, axis=1, keepdims=True), 1e-12)
    ts = np.stack([np.sum(hn * T, 1), np.sum(hn * B, 1), np.sum(hn * nrm, 1)], 1)
    # Les normales TRELLIS sont bruitées : on atténue le relief (lissage vers +Z).
    ts[:, :2] *= 0.6
    ts[:, 2] = np.maximum(ts[:, 2], 0.2)
    ts /= np.linalg.norm(ts, axis=1, keepdims=True)
    alb = np.zeros((size, size, 3)); alb[mask] = col
    nm = np.zeros((size, size, 3)); nm[..., 2] = 1.0; nm[mask] = ts
    nm = nm * 0.5 + 0.5
    pim = np.zeros((size, size, 3)); pim[mask] = pos
    _d, (iy, ix) = distance_transform_edt(~mask, return_indices=True)
    bake.last_positions = pim[iy, ix]          # position 3D de chaque texel (retouches)
    return alb[iy, ix], nm[iy, ix], mask


def save_rgb(path: Path, img: np.ndarray) -> None:
    Image.fromarray((np.clip(img, 0, 1) * 255 + 0.5).astype(np.uint8)).save(path, optimize=True)
