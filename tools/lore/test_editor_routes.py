#!/usr/bin/env python3
"""Test du tracé des routes de l'éditeur de lore (v1.0.97), avec Playwright.

Scénario, entièrement à la souris et au clavier dans la carte de l'éditeur
(``lune-noire-editor.html``) :

1. mode « Tracer » (touche R), type « Sentier » ; route Thrival → Lhantarn par deux
   cases cliquées (cases de raccord ajoutées), arrivée sur le marqueur de Lhantarn ;
2. édition : un point glissé vers une autre case, un point inséré en glissant un
   segment, un point supprimé (Alt+clic) ; tracé toujours continu ;
3. embranchement : Maj+clic sur la route, deux clics, arrivée à Duzhmoroth ; la
   nouvelle route part de la case de jonction (ids de ville et de lieu vides) ;
4. suppression de l'embranchement par la boîte modale HTML, puis Ctrl+Z ;
5. mode « Déplacer » (touche D) : Brenhaval (ville portuaire, N017_W010) glissée en
   N017_W011 ; sa route, ses deux routes maritimes, les pontons de ses lignes
   maritimes et celui de son port suivent (ponton sur la case navigable la plus
   proche, N016_W011) ;
6. Ctrl+Z jusqu'à vider la pile commune, puis export : identique au fichier
   d'origine (test d'aller-retour de ``test_editor_roundtrip.py``).

Aucune erreur console attendue (Google Fonts bloqué). Le fichier de données n'est
jamais modifié.

Lancement (depuis la racine du dépôt, Playwright installé comme pour
``test_editor_roundtrip.py``) ::

    python3 tools/lore/test_editor_routes.py
    python3 tools/lore/test_editor_routes.py --captures /chemin/editeur_routes   # + .gif et .png

Code de sortie 0 si tout passe, 1 sinon.
"""

from __future__ import annotations

import argparse
import io
import json
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import test_editor_roundtrip as rt  # noqa: E402

ZOOM = 5


class Echec(Exception):
    """Vérification ratée (message en français)."""


def verifier(cond, msg):
    """Lève Echec(msg) si ``cond`` est faux."""
    if not cond:
        raise Echec(msg)


def point(page, cellule=None, fiche=None, fx=0.0, fy=0.0):
    """Coordonnées écran d'un point de la carte, après défilement pour le rendre visible.

    \\param cellule identifiant de case (centre de la case) ;
    \\param fiche ``(kind, id)`` d'une ville / d'un lieu (son marqueur) ;
    \\param fx, fy décalage en fraction de case (fy positif = vers le sud).
    """
    return page.evaluate(
        """([cell, kind, id, fx, fy]) => {
          const px = BASE_CELL_SIZE * window._carteState.zoom;
          let p;
          if (kind) { const rec = db[kind].find(x => x.id === id); p = cartePlacePixel(rec, kind, px); }
          else p = carteCellCenter(cell);
          const x = p.x + fx * px, y = p.y + fy * px;
          const sc = document.getElementById('carteScroll');
          if (x < sc.scrollLeft + 60 || x > sc.scrollLeft + sc.clientWidth - 60) sc.scrollLeft = Math.max(0, x - sc.clientWidth / 2);
          if (y < sc.scrollTop + 60 || y > sc.scrollTop + sc.clientHeight - 60) sc.scrollTop = Math.max(0, y - sc.clientHeight / 2);
          const rc = document.getElementById('carteCanvas').getBoundingClientRect();
          return [rc.left + x, rc.top + y];
        }""",
        [cellule, fiche[0] if fiche else None, fiche[1] if fiche else None, fx, fy],
    )


def clic(page, xy, modificateurs=()):
    """Clic gauche à ``xy`` (écran), avec touches Shift / Alt maintenues si demandé."""
    for m in modificateurs:
        page.keyboard.down(m)
    page.mouse.move(*xy)
    page.mouse.down()
    page.mouse.up()
    for m in modificateurs:
        page.keyboard.up(m)
    page.wait_for_timeout(60)


def glisser(page, a, b):
    """Glisser-déposer souris de ``a`` à ``b`` (coordonnées écran), en 12 pas."""
    page.mouse.move(*a)
    page.mouse.down()
    page.mouse.move(a[0] + 6, a[1] + 6, steps=2)
    page.mouse.move(*b, steps=12)
    page.mouse.up()
    page.wait_for_timeout(60)


def route(page, rid):
    """Route ``rid`` telle qu'elle est dans ``db`` (None si absente)."""
    return page.evaluate("id => (db.routes || []).find(r => r.id === id) || null", rid)


def continu(page, cases):
    """Vrai si chaque case du tracé touche la suivante (8 voisines)."""
    return page.evaluate("w => w.every((c, i) => !i || carteCellsAdjacent(w[i - 1], c))", cases)


class Captures:
    """Images de la séquence (zone carte + barre d'outils), assemblées en GIF."""

    # page : page Playwright ; actif : False = aucune capture (prendre() ne fait rien).
    def __init__(self, page, actif):
        self.page, self.actif, self.images = page, actif, []

    def prendre(self, legende=""):
        """Ajoute une image de l'état courant (rien si les captures sont désactivées)."""
        if not self.actif:
            return
        from PIL import Image
        # Seul le dernier message (toast) reste visible sur l'image.
        self.page.evaluate("() => [...document.querySelectorAll('.toast')].slice(0, -1).forEach(e => e.remove())")
        boite = self.page.evaluate(
            """() => { const a = document.getElementById('carteHint').getBoundingClientRect();
                       const b = document.getElementById('carteScroll').getBoundingClientRect();
                       return [Math.max(0, a.left - 8), Math.max(0, a.top - 4), b.right + 8, Math.min(window.innerHeight, b.bottom + 4)]; }""")
        png = self.page.screenshot(clip={"x": boite[0], "y": boite[1], "width": boite[2] - boite[0], "height": boite[3] - boite[1]})
        self.images.append((Image.open(io.BytesIO(png)).convert("RGB"), legende))

    def ecrire(self, base: Path, png_index=-1):
        """Écrit ``base.gif`` (séquence) et ``base.png`` (image ``png_index``)."""
        if not self.actif or not self.images:
            return
        from PIL import ImageDraw
        cadres = []
        for img, leg in self.images:
            im = img.copy()
            if leg:
                d = ImageDraw.Draw(im)
                d.rectangle([0, im.height - 26, im.width, im.height], fill=(10, 10, 16))
                d.text((10, im.height - 20), leg, fill=(240, 216, 144))
            cadres.append(im.resize((im.width * 3 // 4, im.height * 3 // 4)))
        base.parent.mkdir(parents=True, exist_ok=True)
        cadres[0].save(base.with_suffix(".gif"), save_all=True, append_images=cadres[1:], duration=1400, loop=0, optimize=True)
        self.images[png_index][0].save(base.with_suffix(".png"))


def scenario(page, original, cap, dossier):
    """Déroule le scénario (voir le docstring du module) ; lève Echec au premier écart."""
    page.evaluate(f"() => {{ showSection('carte'); window._carteState.zoom = {ZOOM}; drawCarte(); "
                  "document.getElementById('carteHint').scrollIntoView({block: 'start'}); }")
    nb0 = len(original["routes"])
    villes = {v["id"]: v for v in original["villes"]}

    # 1. Création d'un sentier Thrival → Lhantarn.
    page.keyboard.press("r")
    verifier(page.evaluate("() => window._carteState.mode") == "trace", "touche R : mode Tracer non activé")
    page.select_option("#carteTraceType", "path")
    clic(page, point(page, fiche=("villes", "town_truceholm")))
    verifier(page.evaluate("() => !!window._carteTrace.draft"), "clic sur Thrival : pas de tracé commencé")
    clic(page, point(page, "N020_W011"))
    clic(page, point(page, "N021_W013"))
    cap.prendre("Tracer : Thrival puis deux cases cliquees")
    clic(page, point(page, fiche=("villes", "town_lanternhold")))
    rid = page.evaluate("() => window._carteState.selectedRouteId")
    r = route(page, rid)
    verifier(r is not None and page.evaluate("() => db.routes.length") == nb0 + 1, "route non créée")
    verifier(r["type"] == "path" and r["from_ville_id"] == "town_truceholm" and r["to_ville_id"] == "town_lanternhold",
             f"route créée incorrecte : {r}")
    verifier(r["from_place"] == villes["town_truceholm"]["cell"] and r["to_place"] == villes["town_lanternhold"]["cell"],
             "extrémités de la route créée")
    verifier("N021_W012" in r["waypoints"] and continu(page, r["waypoints"]), f"tracé non continu : {r['waypoints']}")
    cap.prendre(f"Route {rid} creee (sentier), selectionnee")
    print(f"   création : {rid} {r['waypoints']}")

    # 2. Édition : déplacer un point, insérer, supprimer.
    glisser(page, point(page, "N020_W011"), point(page, "N019_W012"))
    r = route(page, rid)
    verifier("N019_W012" in r["waypoints"] and "N020_W011" not in r["waypoints"] and continu(page, r["waypoints"]),
             f"déplacement du point : {r['waypoints']}")
    cap.prendre("Point glisse de N020_W011 vers N019_W012")
    print(f"   point déplacé : {r['waypoints']}")
    a, b = r["waypoints"][-2], r["waypoints"][-1]
    pa, pb = point(page, a), point(page, b)
    milieu = ((pa[0] + pb[0]) / 2, (pa[1] + pb[1]) / 2)
    avant = len(r["waypoints"])
    glisser(page, milieu, point(page, "N022_W013"))
    r = route(page, rid)
    verifier("N022_W013" in r["waypoints"] and len(r["waypoints"]) > avant and continu(page, r["waypoints"]),
             f"insertion sur un segment : {r['waypoints']}")
    cap.prendre("Segment glisse : point insere en N022_W013")
    print(f"   point inséré : {r['waypoints']}")
    clic(page, point(page, "N022_W013"), ("Alt",))
    r = route(page, rid)
    verifier("N022_W013" not in r["waypoints"] and continu(page, r["waypoints"]), f"suppression du point : {r['waypoints']}")
    print(f"   point supprimé : {r['waypoints']}")

    # 3. Embranchement depuis un point intérieur de la route.
    jonction = r["waypoints"][len(r["waypoints"]) // 2]
    clic(page, point(page, jonction), ("Shift",))
    d = page.evaluate("() => window._carteTrace.draft && window._carteTrace.draft.branchOf")
    verifier(d and d["routeId"] == rid and d["cell"] == jonction, f"Maj+clic : embranchement non commencé ({d})")
    clic(page, point(page, "N022_W011"))
    cap.prendre(f"Embranchement de {rid} en {jonction}")
    clic(page, point(page, fiche=("villes", "town_duskmoor")))
    bid = page.evaluate("() => window._carteState.selectedRouteId")
    br = route(page, bid)
    verifier(br and bid != rid and br["from_place"] == jonction and br["from_ville_id"] == "" and br["from_lieu_id"] == ""
             and br["to_ville_id"] == "town_duskmoor" and continu(page, br["waypoints"])
             and br["description"].startswith(f"Embranchement de la route {rid}"), f"embranchement incorrect : {br}")
    cap.prendre(f"Embranchement {bid} : {jonction} -> Duzhmoroth")
    print(f"   embranchement : {bid} {br['waypoints']}")

    # 4. Suppression par la boîte modale HTML, puis Ctrl+Z.
    page.click("#carteTraceDeleteBtn")
    page.wait_for_selector("#confirmYes")
    cap.prendre("Suppression : confirmation dans une boite HTML")
    page.click("#confirmYes")
    verifier(route(page, bid) is None, "embranchement non supprimé")
    page.keyboard.press("Control+z")
    verifier(route(page, bid) == br, "Ctrl+Z n'a pas rétabli l'embranchement")
    print("   suppression puis Ctrl+Z : OK")

    # 5. Déplacement de Brenhaval (port) d'une case vers l'ouest.
    page.keyboard.press("Escape")
    page.keyboard.press("d")
    verifier(page.evaluate("() => window._carteState.mode") == "move", "touche D : mode Déplacer non activé")
    glisser(page, point(page, fiche=("villes", "town_brinehaven")), point(page, "N017_W011", fy=0.3))
    etat = page.evaluate("""() => ({ v: db.villes.find(x => x.id === 'town_brinehaven'),
        routes: db.routes.filter(r => r.from_ville_id === 'town_brinehaven' || r.to_ville_id === 'town_brinehaven'),
        stops: db.sea_lines.flatMap(l => l.stops.filter(s => s.ville_id === 'town_brinehaven').map(s => [l.id, s.ponton_cell])) })""")
    v = etat["v"]
    verifier(v["cell"] == "N017_W011", f"Brenhaval non déplacée : {v['cell']}")
    verifier(v["port"]["ponton_cell"] == "N016_W011", f"ponton du port : {v['port']}")
    verifier(all(p == "N016_W011" for _, p in etat["stops"]) and etat["stops"], f"pontons des lignes : {etat['stops']}")
    for rr in etat["routes"]:
        bout = "from" if rr["from_ville_id"] == "town_brinehaven" else "to"
        attendu = "N016_W011" if rr["type"] == "sea_route" else "N017_W011"
        wp = rr.get("waypoints") or []
        verifier(rr[bout + "_place"] == attendu and (not wp or wp[0 if bout == "from" else -1] == attendu)
                 and (not wp or continu(page, wp)), f"route {rr['id']} non suivie : {rr}")
        print(f"   {rr['id']} ({rr['type']}) suit : {wp}")
    cap.prendre("Deplacer : Brenhaval en N017_W011, routes et ponton suivent")
    # Export à ce stade : seuls les routes, les lignes maritimes et Brenhaval changent.
    iv = next(i for i, x in enumerate(original["villes"]) if x["id"] == "town_brinehaven")
    export, _ = rt.exporter(page, dossier, "intermediaire.json")
    autres = [x for x in rt.diff_profond(original, export, limite=1000)
              if not x.startswith(("routes", "sea_lines", f"villes[{iv}]"))]
    verifier(not autres, f"export intermédiaire : différences inattendues {autres[:5]}")
    print("   export intermédiaire : seuls routes, sea_lines et Brenhaval changent")

    # 6. Tout annuler, puis export identique à l'original.
    n = page.evaluate("() => window._carteState.undoStack.length")
    for _ in range(n):
        page.keyboard.press("Control+z")
    verifier(page.evaluate("() => window._carteState.undoStack.length") == 0, "pile d'annulation non vide")
    cap.prendre(f"Ctrl+Z x{n} : tout est annule")
    print(f"   {n} Ctrl+Z")


def main() -> int:
    """Lance le scénario puis le test d'aller-retour ; code de sortie 0 si tout passe."""
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--editeur", type=Path, default=rt.EDITEUR)
    ap.add_argument("--donnees", type=Path, default=rt.DONNEES)
    ap.add_argument("--captures", type=Path, default=None,
                    help="chemin sans extension : écrit <chemin>.gif (séquence) et <chemin>.png")
    args = ap.parse_args()
    from playwright.sync_api import sync_playwright

    original = json.loads(args.donnees.read_text(encoding="utf-8"))
    ok = True
    with sync_playwright() as p, tempfile.TemporaryDirectory() as tmp:
        nav = p.chromium.launch()
        page, erreurs = rt.ouvrir_editeur(nav, args.editeur, taille=(1500, 1000))
        rt.charger(page, args.donnees)
        cap = Captures(page, args.captures is not None)
        try:
            scenario(page, original, cap, Path(tmp))
            print("[OK] tracé, édition, embranchement, suppression, routes qui suivent, annulation")
        except Echec as e:
            ok = False
            print(f"[ÉCHEC] {e}")
        diffs = rt.test_aller_retour(page, original, Path(tmp))
        print(f"[{'OK' if not diffs else 'ÉCHEC'}] export après annulation identique à l'original : {len(diffs)} différence(s)")
        for d in diffs[:30]:
            print("   ", d)
        ok &= not diffs
        if erreurs:
            ok = False
            print(f"[ÉCHEC] {len(erreurs)} erreur(s) console :")
            for e in erreurs[:10]:
                print("   ", e)
        else:
            print("[OK] aucune erreur console")
        if args.captures:
            cap.ecrire(args.captures, png_index=-2)
            print(f"   captures : {args.captures.with_suffix('.gif')} et .png")
        nav.close()
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
