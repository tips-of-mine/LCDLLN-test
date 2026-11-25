"""Import des pièces sculptées (SDXL + TRELLIS.2) dans le catalogue d'apparence.

Chaîne complète (cf. README.md, section « Visages et coiffures sculptés ») :

1. ComfyUI (poste du propriétaire) : image de référence SDXL (tête générique,
   aucune image de la BD) puis maillage coloré TRELLIS.2 (licence MIT).
2. Blender en arrière-plan, ``blender_scripts/fit_high.py`` : ajustement au
   gabarit du corps (repères : sommet du crâne, pointe du nez, largeur du front)
   et export du nuage ``<pièce>_high.npz`` (sommets, triangles, couleurs).
3. Ce script (numpy, scipy, Pillow ; sans Blender) : projection sur la tête
   procédurale (visages), surfaces de poils (coiffures, barbes, moustaches),
   crâne commun aux visages d'un genre, cuisson des textures, atlas, yeux
   teintables, poids de peau et écriture des pièces .glb (squelette du corps).

Le résultat est décrit dans ``sculpted_parts.json`` : ``build_character_appearance.py``
reprend ces options telles quelles au lieu de régénérer les pièces procédurales
des emplacements concernés.

Usage (racine du dépôt) ::

    python tools/asset_pipeline/character_appearance/trellis_import.py <dossier> humains

``<dossier>`` contient les ``<pièce>_high.npz`` et ``pieces.json`` (liste des pièces :
genre, emplacement, option, libellé, nom de la pièce).
"""

from __future__ import annotations

import json
import sys
from pathlib import Path

import numpy as np
from scipy.ndimage import binary_dilation, binary_opening, gaussian_filter, label
from PIL import Image, ImageDraw

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import hair as proc_hair  # noqa: E402
import sculpt_project as sp  # noqa: E402
import textures  # noqa: E402
from glb_io import Primitive, blend_weights, load_skeleton, rigid_weights, write_part_glb  # noqa: E402
from heads import face_variants  # noqa: E402

REPO = HERE.parents[2]
DATA = REPO / "game" / "data"
MODELS = DATA / "models" / "characters"
TEX = DATA / "textures" / "characters"
MANIFEST = HERE / "sculpted_parts.json"

GENDER_PREFIX = {"male": "Male", "female": "Female"}
# Point entre les yeux (espace Blender, cf. build_character_appearance.EYE_WORLD).
EYE_WORLD = {"male": np.array([0.0, -0.022, 1.705]), "female": np.array([0.0, -0.022, 1.652])}
NECK_Z = {"male": 1.555, "female": 1.5095}      # bas du cou des têtes (raccord au corps)
FACE_GRIDS = [(81, 57), (53, 38), (34, 24)]       # LOD0..2 : 8 960 / 3 848 / 1 518 triangles
BUDGETS = {"hair": (10500, 4200, 1600), "beard": (6000, 2400, 1000), "moustache": (1200, 500, 250)}
FACE_ATLAS = 2048            # atlas des visages : 4 cases de 1024 (3 visages)
FACE_CELL = 1024
HAIR_ATLAS = 2048            # atlas pilosité : 4 x 4 cases de 512
HAIR_CELL = 512
HAIR_BASE = 0.80             # luminance moyenne de l'albedo neutre des poils (cf. tint_for)
SHORT_HAIR = ("cropped", "short", "swept")    # coiffures courtes : rien sous les oreilles
# Coiffures attachées : la calotte vient de TRELLIS, la queue, le chignon ou les
# tresses (invisibles depuis le centre de la tête, donc hors de la projection)
# reprennent la géométrie procédurale de hair.py.
TIED_EXTRAS = {"ponytail": ["ponytail_low"], "high_tail": ["ponytail_high"], "bun": ["bun_back"],
               "braid": ["braid"], "twin_braids": ["braid_left", "braid_right"]}
EXTRA_RES = [(18, 12), (12, 8), (8, 6)]
FACIAL_THICKNESS = {"short_beard": 0.012, "full_beard": 0.025, "long_beard": 0.060, "chin_strap": 0.008,
                    "goatee": 0.020, "thin": 0.005, "thick": 0.008, "handlebar": 0.010, "drooping": 0.010}
# épaisseur max. de la pilosité faciale au-dessus de la peau (m) : supprime les
# « rideaux » sous la mâchoire et les mèches détachées du visage.
TIED_THICKNESS = 0.010                           # épaisseur max. de la calotte des coiffures attachées (m)          # (échantillons, côtés) par LOD, cf. hair.py
LICENCE = ("Pièces sculptées générées pour LCDLLN (références SDXL génériques, aucune image "
           "de la BD ; maillages TRELLIS.2, licence MIT) — CC0 du projet")


def rel(p: Path) -> str:
    return p.relative_to(DATA).as_posix()


def head_center(gender: str) -> np.ndarray:
    ew = EYE_WORLD[gender]
    return np.array([0.0, ew[1] - 0.010, ew[2] + 0.005])


def smoothstep(e0: float, e1: float, x: np.ndarray) -> np.ndarray:
    t = np.clip((x - e0) / (e1 - e0), 0.0, 1.0)
    return t * t * (3 - 2 * t)


def scalp_weight(v: np.ndarray, gender: str) -> np.ndarray:
    """Zone du crâne (au-dessus du front, arrière de la tête) qui adopte le crâne
    commun du genre ; le visage n'est pas touché."""
    ew = EYE_WORLD[gender]
    above_brow = smoothstep(ew[2] + 0.030, ew[2] + 0.060, v[:, 2])
    behind = smoothstep(ew[1] - 0.045, ew[1] - 0.085, v[:, 1]) * smoothstep(ew[2] - 0.040, ew[2] - 0.005, v[:, 2])
    return np.maximum(above_brow, behind)


def jaw_line(a: np.ndarray) -> np.ndarray:
    """Élévation (rad) du bas de la mâchoire selon l'azimut ``a`` = |θ| (rad) :
    -58° sous le menton, -38° vers l'oreille. Plus bas, la direction radiale vise
    le cou, loin de la peau du visage."""
    return np.radians(-58.0 + 20.0 * np.clip((a - np.radians(25)) / np.radians(65), 0, 1))


def jaw_weight(th: np.ndarray, ph: np.ndarray) -> np.ndarray:
    """Poids (0..1) de la zone barbe d'un visage d'homme (menton, mâchoire, joues
    basses), unifiée sur la mâchoire commune du genre. Angles en radians vus du
    centre de la tête ; les lèvres (hors lèvre supérieure) et le nez restent propres à chaque visage."""
    a = np.abs(th)
    chin = smoothstep(np.radians(-38), np.radians(-46), ph)
    cheek = smoothstep(np.radians(26), np.radians(38), a) * smoothstep(np.radians(-12), np.radians(-24), ph)
    below = smoothstep(jaw_line(a) - np.radians(10), jaw_line(a), ph)     # 0 sous la mâchoire (cou)
    # Lèvre supérieure (sous le nez) : la moustache s'y pose sur les trois visages.
    lip = (smoothstep(np.radians(-25), np.radians(-28), ph) * smoothstep(np.radians(-35), np.radians(-32), ph)
           * (1.0 - smoothstep(np.radians(30), np.radians(40), a)))
    return np.maximum(np.maximum(chin, cheek) * below, lip) * (1.0 - smoothstep(np.radians(100), np.radians(115), a))


def region_for(slot: str, option: str):
    """Zone angulaire (autour du centre de la tête) où l'on cherche les poils."""
    long_hair = option in ("long", "medium", "bob")

    def hair(th, ph):
        a = np.abs(th)
        front = a < np.radians(55)
        side = (a >= np.radians(55)) & (a < np.radians(115))
        back = a >= np.radians(115)
        low_side = np.radians(-55 if long_hair else -22)
        low_back = np.radians(-80 if long_hair else -45)
        return (front & (ph > np.radians(9))) | (side & (ph > low_side)) | (back & (ph > low_back))

    # Repères (élévation vue du centre de la tête, homme) : pointe du nez -19°,
    # base du nez -27°, lèvre supérieure -33°, bouche -37°, menton -48°.
    def beard(th, ph):
        a = np.abs(th)
        if option == "goatee":       # menton seul (tiré de la barbe pleine)
            return (a < np.radians(18)) & (ph < np.radians(-44)) & (ph > jaw_line(a))
        nose = (a < np.radians(16)) & (ph > np.radians(-29))
        mouth = (a < np.radians(24)) & (ph <= np.radians(-29)) & (ph > np.radians(-44))   # lèvres dégagées
        return (a < np.radians(105)) & (ph < np.radians(-10)) & (ph > jaw_line(a)) & ~nose & ~mouth

    def moustache(th, ph):
        a = np.abs(th)
        if option == "thin":         # liseré au-dessus de la lèvre
            return (a < np.radians(24)) & (ph < np.radians(-26.5)) & (ph > np.radians(-31.5))
        if option == "drooping":     # moustache + coins tombants (tirés de la barbe pleine)
            band = (a < np.radians(28)) & (ph < np.radians(-25)) & (ph > np.radians(-33.5))
            sides = (a > np.radians(14)) & (a < np.radians(24)) & (ph <= np.radians(-33.5)) & (ph > np.radians(-50))
            return band | sides
        return (a < np.radians(36)) & (ph < np.radians(-25)) & (ph > np.radians(-33.5))

    return {"hair": hair, "beard": beard, "moustache": moustache}[slot]


def tied_extras(option: str, shape, ew: np.ndarray, lod: int) -> list:
    """Géométrie procédurale (hair.py) des queues, chignons et tresses."""
    n, sides = EXTRA_RES[lod]
    out = []
    for e in TIED_EXTRAS.get(option, []):
        if e == "ponytail_low":
            out.append(proc_hair._ponytail(shape, ew, False, 0.22, n, sides))
        elif e == "ponytail_high":
            out.append(proc_hair._ponytail(shape, ew, True, 0.26, n, sides))
        elif e == "bun_back":
            out.append(proc_hair._bun(shape, ew, False, sides * 2, sides))
        elif e == "braid":
            out.append(proc_hair._braid(shape, ew, 0.30, n * 2, sides))
        elif e in ("braid_left", "braid_right"):
            x = (0.05 if e == "braid_left" else -0.05) * shape.scale
            out.append(proc_hair._braid(shape, ew, 0.26, n * 2, sides, x=x))
    return out


def close_holes(mask: np.ndarray, it: int) -> np.ndarray:
    """Referme les trous d'un masque angulaire de poils (couture arrière comprise).

    ``it`` : nombre d'itérations de fermeture morphologique (cellules de 0,75°)."""
    from scipy.ndimage import binary_closing, binary_fill_holes
    pad = it + 2
    rr = np.concatenate([mask[:, -pad:], mask, mask[:, :pad]], axis=1)
    rr = binary_fill_holes(binary_closing(rr, iterations=it))
    return mask | rr[:, pad:pad + mask.shape[1]]


def largest_region(mask: np.ndarray) -> np.ndarray:
    """Garde la plus grande zone connexe d'un masque angulaire (écarte les îlots de
    poils isolés, p. ex. autour des oreilles). La couture arrière est raccordée."""
    lab, n = label(mask)
    if n <= 1:
        return mask
    # Raccord de la couture (colonne 0 <-> dernière colonne).
    for a, b in zip(lab[:, 0], lab[:, -1]):
        if a and b and a != b:
            lab[lab == b] = a
    sizes = np.bincount(lab.ravel())
    sizes[0] = 0
    return lab == int(np.argmax(sizes))


def seat_on_cap(m, cap: "sp.Field", center: np.ndarray, sink: float = 0.004) -> None:
    """Rapproche une queue, un chignon ou une tresse de la calotte sculptée.

    Le point de la pièce le plus proche du centre de la tête est amené à ``sink``
    (m) sous la surface de la calotte, le long de la direction radiale : l'attache
    ne laisse aucun jour visible. Modifie ``m.v`` en place."""
    th, ph, r = sp.spherical(m.v, center)
    k = int(np.argmin(r))
    gap = r[k] - (float(cap.sample(th[k:k + 1], ph[k:k + 1])[0]) - sink)
    m.v -= sp.direction(th[k], ph[k]) * gap


def uv_mask(uvs: np.ndarray, faces: np.ndarray, size: int) -> np.ndarray:
    im = Image.new("L", (size, size), 0)
    d = ImageDraw.Draw(im)
    for tri in faces:
        d.polygon([(float(uvs[k, 0] * size), float(uvs[k, 1] * size)) for k in tri], fill=255)
    return np.asarray(im) > 0


def to_u8(img: np.ndarray) -> np.ndarray:
    return (np.clip(img, 0, 1) * 255 + 0.5).astype(np.uint8)


def eye_triangles(m: sp.Mesh, ew: np.ndarray) -> np.ndarray:
    """Triangles des globes oculaires (avant du visage, à hauteur des yeux)."""
    fc = m.v[m.f].mean(axis=1)
    sel = np.zeros(len(m.f), bool)
    for sx in (-1, 1):
        near = (np.abs(fc[:, 0] - sx * 0.032) < 0.015) & (np.abs(fc[:, 2] - ew[2]) < 0.008)
        if near.any():
            sel |= near & (fc[:, 1] > fc[near, 1].max() - 0.010)
    return sel


def main(src: Path, race: str) -> None:
    spec = json.loads((src / "pieces.json").read_text(encoding="utf-8"))
    manifest = json.loads(MANIFEST.read_text(encoding="utf-8")) if MANIFEST.exists() else {}
    manifest["licence"] = LICENCE
    manifest.setdefault("races", {})
    stats = manifest.setdefault("stats", {})
    for gender in ("male", "female"):
        pieces = [p for p in spec["pieces"] if p["gender"] == gender]
        if not pieces:
            continue
        G = GENDER_PREFIX[gender]
        skel = load_skeleton(MODELS / "humains" / f"{G}_Ranger" / f"{G}_Ranger.glb")
        hj, nj = skel.joint_index("Head"), skel.joint_index("neck_01")
        ew = EYE_WORLD[gender]
        center = head_center(gender)
        outdir = MODELS / "appearance" / race / gender
        outdir.mkdir(parents=True, exist_ok=True)
        texdir = TEX / "appearance" / race
        texdir.mkdir(parents=True, exist_ok=True)
        slots: dict = {}

        def write(path: Path, prims: list, name: str) -> str:
            stats[rel(path)] = write_part_glb(path, skel, prims, name)
            return rel(path)

        # ---------------- Visages ----------------
        faces = [p for p in pieces if p["slot"] == "face"]
        template = face_variants(race, gender)[0]
        clouds = {p["name"]: sp.Cloud(src / f"{p['name']}_high.npz") for p in faces}
        skins = {n: sp.skin_field(c, center) for n, c in clouds.items()}
        common = sp.Field(center, np.mean(np.stack([s.r for s in skins.values()]), axis=0))
        envelope = sp.Field(center, np.max(np.stack([s.r for s in skins.values()]), axis=0))
        body_tex = TEX / "humains" / f"T_Regular_{G}_BaseColor.png"
        skin_ref = np.asarray(textures.median_skin(body_tex))
        atlas = np.zeros((FACE_ATLAS, FACE_ATLAS, 4))
        atlas[..., :3] = skin_ref
        atlas[..., 3] = 1.0
        natlas = np.zeros((FACE_ATLAS, FACE_ATLAS, 3))
        natlas[...] = (0.5, 0.5, 1.0)
        face_opts = []
        for k, p in enumerate(faces):
            cl, skin = clouds[p["name"]], skins[p["name"]]
            cx, cy = k % 2, k // 2
            lods = []
            for nu, nv in FACE_GRIDS:
                m = sp.project_face(sp.face_template(template, ew, nu, nv), skin, NECK_Z[gender])
                # Crâne commun du genre (les coiffures s'y posent sans jour).
                th, ph, r = sp.spherical(m.v, center)
                w = scalp_weight(m.v, gender)
                if gender == "male":
                    # Mâchoire commune (barbes posées sans jour sur les trois visages).
                    w = np.maximum(w, jaw_weight(th, ph))
                m.v = center + sp.direction(th, ph) * (r * (1 - w) + common.sample(th, ph) * w)[:, None]
                lods.append(m)
            alb, nor, used = sp.bake(cl, sp.near_field(cl, skin), lods[0], FACE_CELL, center)
            pos = sp.bake.last_positions
            # Couleur ramenée sur la peau du corps (les teintes de peau s'appliquent
            # ensuite comme sur le corps) : gain par canal borné, appliqué sur une
            # courbe douce pour ne pas brûler les reflets.
            lum = alb @ np.array([0.299, 0.587, 0.114])
            front = (pos[..., 1] > ew[1] + 0.03) & (np.abs(pos[..., 2] - ew[2] + 0.02) < 0.035)
            skin_px = alb[used & front & (lum > 0.08) & (lum < 0.92)]
            gain = np.clip(skin_ref / np.maximum(np.median(skin_px, axis=0), 1e-3), 0.6, 1.8)
            alb = 1.0 - np.power(np.clip(1.0 - alb, 0, 1), gain)
            # Crâne : TRELLIS y peint souvent des ombres ou un duvet sombre ; on le
            # ramène à la couleur du front (zone en partie couverte par les coiffures).
            forehead = used & (pos[..., 1] > ew[1] + 0.06) & (pos[..., 2] > ew[2] + 0.03) & (pos[..., 2] < ew[2] + 0.055)
            fcol = np.median(alb[forehead], axis=0) if forehead.sum() > 20 else skin_ref
            ws = scalp_weight(pos.reshape(-1, 3), gender).reshape(FACE_CELL, FACE_CELL)
            # + côtés du crâne au-dessus des oreilles (souvent sombres chez TRELLIS).
            th_p, ph_p, _r = sp.spherical(pos.reshape(-1, 3), center)
            th_p, ph_p = th_p.reshape(FACE_CELL, FACE_CELL), ph_p.reshape(FACE_CELL, FACE_CELL)
            sides = smoothstep(np.radians(50), np.radians(65), np.abs(th_p)) * smoothstep(np.radians(-10), np.radians(2), ph_p)
            # + arrière de la tête et de la nuque (col, cheveux ou ombres de la référence).
            back = smoothstep(np.radians(95), np.radians(125), np.abs(th_p))
            # + cou sous la mâchoire (col de vêtement ou ombre portée dans la référence).
            a_p = np.abs(th_p)
            neck = smoothstep(jaw_line(a_p) - np.radians(3), jaw_line(a_p) - np.radians(12), ph_p)
            ws = np.maximum(np.maximum(np.maximum(ws, sides), back), neck)
            # Transition adoucie dans la texture (pas de bord net au niveau des tempes).
            ws = gaussian_filter(np.clip((ws - 0.3) / 0.5, 0, 1), sigma=FACE_CELL / 96)[..., None]
            flum = alb @ np.array([0.299, 0.587, 0.114])
            detail = np.clip(flum / max(float(fcol @ np.array([0.299, 0.587, 0.114])), 1e-3), 0.85, 1.12)[..., None]
            alb = alb * (1 - ws) + fcol * detail * ws
            # Yeux : triangles des globes -> matériau « yeux du visage ». Dans
            # l'ouverture de l'œil (sclère claire, peu saturée), un iris est peint
            # au centre (gris neutre, alpha 0 = zone teintée par la couleur des
            # yeux) avec une pupille sombre : TRELLIS ne dessine pas toujours l'iris.
            alpha = np.ones((FACE_CELL, FACE_CELL))
            eyes0 = eye_triangles(lods[0], ew)
            emask = uv_mask(lods[0].uv, lods[0].f[eyes0], FACE_CELL)
            lum = alb @ np.array([0.299, 0.587, 0.114])
            mx, mn = alb.max(-1), alb.min(-1)
            sat = np.where(mx > 1e-4, (mx - mn) / np.maximum(mx, 1e-4), 0)
            yy, xx = np.mgrid[0:FACE_CELL, 0:FACE_CELL]
            for sx in (-1, 1):
                side = emask & (np.sign(pos[..., 0]) == sx)
                sclera = side & (lum > 0.5) & (sat < 0.25)
                if sclera.sum() < 15:
                    continue
                ys, xs = np.nonzero(sclera)
                ey, ex = 0.5 * (ys.min() + ys.max()), 0.5 * (xs.min() + xs.max())
                # Échelle locale (texels par mètre) autour de l'œil.
                pz = pos[sclera]
                d3 = np.linalg.norm(pz - pz.mean(axis=0), axis=1)
                d2 = np.hypot(ys - ey, xs - ex)
                ok = d3 > 0.001
                tpm = float(np.median(d2[ok] / d3[ok])) if ok.any() else 2000.0
                rr = np.hypot(yy - ey, xx - ex)
                opening = side & (sat < 0.35)
                iris = opening & (rr < 0.0058 * tpm)
                ang = np.arctan2(yy - ey, xx - ex)
                stri = 0.55 + 0.08 * np.sin(ang * 23.0) + 0.05 * np.sin(ang * 7.0)
                alb[iris] = np.clip(stri[iris], 0.25, 0.8)[:, None]
                alpha[iris] = 0.0
                pupil = opening & (rr < 0.0024 * tpm)
                alb[pupil] = 0.05
                alpha[pupil] = 1.0
            y0, x0 = cy * FACE_CELL, cx * FACE_CELL
            atlas[y0:y0 + FACE_CELL, x0:x0 + FACE_CELL, :3] = alb
            atlas[y0:y0 + FACE_CELL, x0:x0 + FACE_CELL, 3] = alpha
            natlas[y0:y0 + FACE_CELL, x0:x0 + FACE_CELL] = nor
            paths = []
            for i, m in enumerate(lods):
                n = sp.vertex_normals(m.v, m.f)
                uv = m.uv * 0.5 + np.array([cx * 0.5, cy * 0.5])
                eyes = eyes0 if i == 0 else eye_triangles(m, ew)
                prims = []
                for mat, sel in (("MI_Face", ~eyes), ("MI_FaceEye", eyes)):
                    if not sel.any():
                        continue
                    used_v, inv = np.unique(m.f[sel].ravel(), return_inverse=True)
                    j, wts = blend_weights(m.v[used_v, 2], nj, hj, ew[2] - 0.15, ew[2] - 0.10)
                    prims.append(Primitive(mat, m.v[used_v], n[used_v], uv[used_v], inv.astype(np.uint32), j, wts))
                paths.append(write(outdir / f"face_{k + 1}_lod{i}.glb", prims, "face"))
            face_opts.append({"id": f"face_{k + 1}", "name": p["label"], "lods": paths})
        fa = texdir / f"T_Face_{G}_BaseColor.png"
        fn = texdir / f"T_Face_{G}_Normal.png"
        Image.fromarray(to_u8(atlas), "RGBA").save(fa, optimize=True)
        Image.fromarray(to_u8(natlas), "RGB").save(fn, optimize=True)
        for o in face_opts:
            o["texture"], o["normal"] = rel(fa), rel(fn)
        slots["face"] = face_opts

        # ---------------- Pilosité ----------------
        hair_pieces = [p for p in pieces if p["slot"] in ("hair", "beard", "moustache")]
        per_row = HAIR_ATLAS // HAIR_CELL
        n_tied = sum(1 for p in hair_pieces if p["slot"] == "hair" and p["option"] in TIED_EXTRAS)
        if len(hair_pieces) + n_tied > per_row * per_row:
            raise SystemExit("trop de pièces de pilosité pour l'atlas")
        next_cell = len(hair_pieces)
        # Texture de mèches procédurale (répétée) pour les queues, chignons, tresses.
        strands = np.asarray(Image.open(TEX / "appearance" / "T_Hair.png").convert("RGB"), dtype=np.float64) / 255.0
        reps = HAIR_CELL // strands.shape[0] + 1
        strands = np.tile(strands, (reps, reps, 1))[:HAIR_CELL, :HAIR_CELL]
        hatlas = np.ones((HAIR_ATLAS, HAIR_ATLAS, 4))
        hatlas[..., :3] = HAIR_BASE
        for k, p in enumerate(hair_pieces):
            cx, cy = k % per_row, k // per_row
            cl = sp.Cloud(src / f"{p['name']}_high.npz")
            tone = p.get("tone", "black")
            short = p["slot"] == "hair" and (p["option"] in SHORT_HAIR or p["option"] in TIED_EXTRAS)
            zmin = ew[2] - (0.085 if short else (0.11 if p["slot"] == "beard" else 0.30))
            if "cut" in p:                     # coupe dérivée (p. ex. carré tiré des cheveux longs)
                zmin = ew[2] - float(p["cut"])
            field, mask = sp.hair_fields(cl, center, region_for(p["slot"], p["option"]), tone, zmin)
            under = common
            if p["slot"] in ("beard", "moustache"):
                field.r = np.clip(field.r, under.r + 0.001, under.r + FACIAL_THICKNESS.get(p["option"], 0.02))
            if p["slot"] == "hair" and p["option"] in TIED_EXTRAS:
                # Cheveux tirés : calotte plaquée (la queue ou le chignon de la
                # référence est remplacé par la géométrie procédurale).
                field.r = np.minimum(field.r, under.r + TIED_THICKNESS)
                mask = close_holes(mask, 14)
                # Oreilles dégagées : pas de poils là où la peau dépasse la calotte.
                ears = binary_dilation(np.nan_to_num(envelope.r - under.r) > TIED_THICKNESS - 0.002, iterations=5)
                mask &= ~ears
                mask = largest_region(binary_opening(mask, iterations=3))
            budgets = list(BUDGETS[p["slot"]])
            if p["slot"] == "hair" and p["option"] in TIED_EXTRAS:
                # Le budget de la pièce inclut la queue, le chignon ou les tresses.
                budgets = [b - sum(len(e.f) for e in tied_extras(p["option"], template, ew, i))
                           for i, b in enumerate(budgets)]
            lods = sp.hair_lods(field, mask, under, budgets)
            alb, _nor, used = sp.bake(cl, cl.hair_mask(tone) & (cl.v[:, 2] > zmin), lods[0], HAIR_CELL, center)
            lum = alb @ np.array([0.299, 0.587, 0.114])
            med = np.median(lum[used]) if used.any() else 0.2
            # Albedo neutre : variations des mèches gardées mais adoucies (la couleur
            # vient de la teinte du matériau).
            norm = np.clip(lum / max(med, 1e-3), 0.0, 2.0)
            grey = np.clip(HAIR_BASE * (0.78 + 0.22 * norm), 0.35, 1.0)
            alpha = sp.hair_alpha(mask, lods[0], HAIR_CELL)
            hatlas[cy * HAIR_CELL:(cy + 1) * HAIR_CELL, cx * HAIR_CELL:(cx + 1) * HAIR_CELL, :3] = grey[..., None]
            hatlas[cy * HAIR_CELL:(cy + 1) * HAIR_CELL, cx * HAIR_CELL:(cx + 1) * HAIR_CELL, 3] = alpha
            ex_cell = None
            if p["slot"] in ("beard", "moustache"):
                field.r = np.clip(field.r, under.r + 0.001, under.r + FACIAL_THICKNESS.get(p["option"], 0.02))
            if p["slot"] == "hair" and p["option"] in TIED_EXTRAS:
                ex_cell = (next_cell % per_row, next_cell // per_row)
                next_cell += 1
                ex, ey = ex_cell
                hatlas[ey * HAIR_CELL:(ey + 1) * HAIR_CELL, ex * HAIR_CELL:(ex + 1) * HAIR_CELL, :3] = strands
                hatlas[ey * HAIR_CELL:(ey + 1) * HAIR_CELL, ex * HAIR_CELL:(ex + 1) * HAIR_CELL, 3] = 1.0
            paths = []
            for i, m in enumerate(lods):
                uv = (m.uv + np.array([cx, cy])) / per_row
                j, wts = rigid_weights(len(m.v), hj)
                prims = [Primitive("MI_Hair", m.v, sp.vertex_normals(m.v, m.f), uv,
                                   m.f.ravel().astype(np.uint32), j, wts)]
                if ex_cell is not None:
                    for e in tied_extras(p["option"], template, ew, i):
                        seat_on_cap(e, field, center)
                        euv = (e.uv / np.maximum(e.uv.max(axis=0), 1.0) * 0.98 + 0.01 + np.array(ex_cell)) / per_row
                        je, we = rigid_weights(len(e.v), hj)
                        prims.append(Primitive("MI_Hair", e.v, e.normals(), euv, e.f.ravel().astype(np.uint32), je, we))
                paths.append(write(outdir / f"{p['slot']}_{p['option']}_lod{i}.glb", prims, p["slot"]))
            slots.setdefault(p["slot"], []).append({"id": p["option"], "name": p["label"], "lods": paths})
            print(f"[sculpt] {gender} {p['slot']}/{p['option']} : {[len(m.f) for m in lods]} triangles")
        ha = texdir / f"T_HairSculpt_{G}.png"
        Image.fromarray(to_u8(hatlas), "RGBA").save(ha, optimize=True)
        manifest["races"].setdefault(race, {})[gender] = {"slots": slots, "hairTexture": rel(ha)}
        print(f"[sculpt] {race}/{gender} : " + ", ".join(f"{k}={len(v)}" for k, v in slots.items()))
    MANIFEST.write_text(json.dumps(manifest, indent=1, ensure_ascii=False) + "\n", encoding="utf-8")
    print(f"[sculpt] manifeste : {MANIFEST.relative_to(REPO)}")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)
    main(Path(sys.argv[1]), sys.argv[2])
