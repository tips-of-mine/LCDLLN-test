"""Géométrie paramétrique (numpy) pour les pièces de personnalisation.

Toutes les pièces sont construites à partir de surfaces paramétriques
(grilles u × v) ou de tubes le long d'un chemin. La résolution est un
paramètre : les 3 niveaux de détail (LOD0/1/2) sont obtenus en régénérant la
même pièce avec une grille plus grossière, sans décimation (pas de trou ni de
déchirure d'UV).

Unités : espace Blender (mètres, Z haut, +Y avant). Les UV suivent la
convention glTF (origine en haut à gauche, v vers le bas).
"""

from __future__ import annotations

from dataclasses import dataclass

import numpy as np


@dataclass
class Mesh:
    """Maillage triangulé simple avec UV par sommet."""

    v: np.ndarray        # (n, 3)
    f: np.ndarray        # (m, 3) indices
    uv: np.ndarray       # (n, 2)

    def copy(self) -> "Mesh":
        return Mesh(self.v.copy(), self.f.copy(), self.uv.copy())

    def normals(self) -> np.ndarray:
        """Normales lissées (moyenne pondérée par l'aire des faces)."""
        n = np.zeros_like(self.v)
        a, b, c = self.v[self.f[:, 0]], self.v[self.f[:, 1]], self.v[self.f[:, 2]]
        fn = np.cross(b - a, c - a)
        for k in range(3):
            np.add.at(n, self.f[:, k], fn)
        ln = np.linalg.norm(n, axis=1, keepdims=True)
        n = np.where(ln > 1e-12, n / np.maximum(ln, 1e-12), np.array([0.0, 0.0, 1.0]))
        return n

    def flipped(self) -> "Mesh":
        """Inverse l'ordre des sommets des faces (retourne la face avant)."""
        return Mesh(self.v.copy(), self.f[:, ::-1].copy(), self.uv.copy())


def merge(meshes: list) -> Mesh:
    """Concatène plusieurs maillages (sans soudure)."""
    vs, fs, uvs = [], [], []
    off = 0
    for m in meshes:
        vs.append(m.v)
        fs.append(m.f + off)
        uvs.append(m.uv)
        off += len(m.v)
    return Mesh(np.concatenate(vs), np.concatenate(fs), np.concatenate(uvs))


def grid(nu: int, nv: int, wrap_u: bool = False) -> tuple:
    """Paramètres (u, v) d'une grille et ses faces triangulées.

    u ∈ [0, 1] (nu colonnes, bouclées si ``wrap_u`` : la dernière colonne
    rejoint la première), v ∈ [0, 1] (nv lignes). Les faces sont orientées de
    façon à ce que la normale suive ``∂P/∂u × ∂P/∂v``.
    """
    cols = nu if wrap_u else nu
    us = np.linspace(0.0, 1.0, nu, endpoint=not wrap_u)
    vs = np.linspace(0.0, 1.0, nv)
    U, V = np.meshgrid(us, vs)               # (nv, nu)
    faces = []
    for j in range(nv - 1):
        for i in range(cols if wrap_u else cols - 1):
            i2 = (i + 1) % nu
            a = j * nu + i
            b = j * nu + i2
            c = (j + 1) * nu + i2
            d = (j + 1) * nu + i
            faces.append((a, b, c))
            faces.append((a, c, d))
    return U.ravel(), V.ravel(), np.array(faces, dtype=np.int64)


def surface(fn, nu: int, nv: int, wrap_u: bool = False, uv_fn=None) -> Mesh:
    """Construit une surface paramétrique ``fn(u, v) -> (n, 3)``.

    ``uv_fn(u, v, P) -> (n, 2)`` fournit les UV de texture (défaut : u, v).
    """
    U, V, F = grid(nu, nv, wrap_u)
    P = fn(U, V)
    uv = uv_fn(U, V, P) if uv_fn is not None else np.stack([U, V], axis=1)
    return Mesh(np.asarray(P, dtype=np.float64), F, np.asarray(uv, dtype=np.float64))


def frames_along(path: np.ndarray, up_hint=(0.0, 0.0, 1.0)) -> tuple:
    """Repères (tangente, normale, binormale) le long d'un chemin (transport parallèle)."""
    t = np.gradient(path, axis=0)
    t /= np.maximum(np.linalg.norm(t, axis=1, keepdims=True), 1e-12)
    up = np.asarray(up_hint, dtype=np.float64)
    n0 = np.cross(t[0], up)
    if np.linalg.norm(n0) < 1e-6:
        n0 = np.cross(t[0], np.array([1.0, 0.0, 0.0]))
    n0 /= np.linalg.norm(n0)
    normals = [n0]
    for k in range(1, len(path)):
        n = normals[-1] - np.dot(normals[-1], t[k]) * t[k]
        ln = np.linalg.norm(n)
        normals.append(n / ln if ln > 1e-9 else normals[-1])
    N = np.array(normals)
    B = np.cross(t, N)
    return t, N, B


def tube(path: np.ndarray, radius, sides: int, cap_end: bool = True, flatten: float = 1.0,
         up_hint=(0.0, 0.0, 1.0), uv_scale=(1.0, 1.0), twist: float = 0.0) -> Mesh:
    """Tube le long de ``path`` (k, 3) avec un rayon variable ``radius(t)``.

    ``flatten`` < 1 aplatit la section (corne, mèche plate). ``twist``
    (radians sur toute la longueur) fait tourner la section. L'extrémité finale
    est refermée en pointe si ``cap_end``.
    """
    k = len(path)
    t = np.linspace(0.0, 1.0, k)
    r = radius(t) if callable(radius) else np.full(k, float(radius))
    T, N, B = frames_along(path, up_hint)
    ang = np.linspace(0.0, 2 * np.pi, sides, endpoint=False)
    verts, uvs = [], []
    for i in range(k):
        a = ang + twist * t[i]
        ring = path[i] + r[i] * (np.outer(np.cos(a), N[i]) + flatten * np.outer(np.sin(a), B[i]))
        verts.append(ring)
        uvs.append(np.stack([np.linspace(0, 1, sides, endpoint=False) * uv_scale[0],
                             np.full(sides, t[i] * uv_scale[1])], axis=1))
    V = np.concatenate(verts)
    UV = np.concatenate(uvs)
    faces = []
    for i in range(k - 1):
        for s in range(sides):
            a = i * sides + s
            b = i * sides + (s + 1) % sides
            c = (i + 1) * sides + (s + 1) % sides
            d = (i + 1) * sides + s
            faces.append((a, b, c))
            faces.append((a, c, d))
    if cap_end:
        tip = len(V)
        V = np.vstack([V, path[-1] + T[-1] * r[-1] * 0.5])
        UV = np.vstack([UV, [0.5, uv_scale[1]]])
        base = (k - 1) * sides
        for s in range(sides):
            faces.append((base + s, base + (s + 1) % sides, tip))
    # Fermeture du début (disque).
    center = len(V)
    V = np.vstack([V, path[0]])
    UV = np.vstack([UV, [0.5, 0.0]])
    for s in range(sides):
        faces.append((center, (s + 1) % sides, s))
    return Mesh(V, np.array(faces, dtype=np.int64), UV)


def bezier(points: np.ndarray, samples: int) -> np.ndarray:
    """Courbe de Bézier (degré quelconque) échantillonnée."""
    pts = np.asarray(points, dtype=np.float64)
    n = len(pts) - 1
    t = np.linspace(0.0, 1.0, samples)[:, None]
    from math import comb
    out = np.zeros((samples, 3))
    for i, p in enumerate(pts):
        out += comb(n, i) * (t ** i) * ((1 - t) ** (n - i)) * p
    return out


def smoothstep(e0: float, e1: float, x):
    """Transition douce de 0 (x <= e0) à 1 (x >= e1)."""
    t = np.clip((np.asarray(x, dtype=np.float64) - e0) / (e1 - e0), 0.0, 1.0)
    return t * t * (3 - 2 * t)


def gauss2(x, y, cx, cy, sx, sy):
    """Gaussienne 2D non normalisée (pic = 1)."""
    return np.exp(-(((x - cx) / sx) ** 2 + ((y - cy) / sy) ** 2))


def orient_outward(m: Mesh, center) -> Mesh:
    """Oriente globalement les faces vers l'extérieur par rapport à ``center``."""
    a, b, c = m.v[m.f[:, 0]], m.v[m.f[:, 1]], m.v[m.f[:, 2]]
    fn = np.cross(b - a, c - a)
    if float(np.sum(np.einsum("ij,ij->i", fn, (a + b + c) / 3 - np.asarray(center)))) < 0:
        return m.flipped()
    return m
