#!/usr/bin/env python3
"""Test des calques de la carte de l'éditeur de lore (v1.0.99), avec Playwright.

Scénario (``lune-noire-editor.html``, section Carte) :

1. valeurs par défaut : calques historiques comme avant (villes, lieux, routes, points
   importants, eau, îles actifs ; régions inactif), calques ajoutés inactifs, calques
   secrets (dragons, Ophidiens ; v1.0.107 : lieux et villes cachés) inactifs, grottes,
   Gouffre des Revenus et Thurvhal absents de la carte ;
2. « Charger la carte du jeu » : ``world_master.json`` du dépôt, ``generated_cells`` lues ;
3. chaque case du panneau cochée puis décochée (état, légende, aucune erreur), chaque
   sous-filtre (taille de ville, type de lieu) décoché : plus aucun marqueur de ce type ;
4. contrôles ciblés : grottes, entrées, lieux et ville cachés n'apparaissent qu'avec leur
   calque secret ;
   niveau d'une case de départ (1-10) et d'une province ; survol d'une pastille de quête :
   info-bulle et tracé des étapes (SVG), effacé à la sortie ; info-bulles raid, balise,
   peuple lointain ;
5. mémorisation : après rechargement de la page, l'état revient (localStorage), sauf les
   calques secrets ; localStorage qui lève une exception : l'éditeur fonctionne ;
6. performance : temps de ``drawCarte`` (rendu forcé) aux zooms ×2, ×6, ×12, sans et avec
   tous les calques, et du survol (``carteOnHover``) ; seuils larges (lenteur grossière) ;
7. export après toutes ces manipulations : identique octet pour octet au fichier.

``--captures <chemin sans extension>`` écrit ``<chemin>.png`` (carte avec plusieurs calques
et panneau) et ``<chemin>_niveaux.png``, ``_quetes.png``, ``_secrets.png``.

Lancement (depuis la racine du dépôt, Playwright installé comme pour
``test_editor_roundtrip.py``) ::

    python3 tools/lore/test_editor_layers.py
    python3 tools/lore/test_editor_layers.py --editeur legacy/world-builder-html/lune-noire-editor-v20.html \\
            --donnees legacy/world-builder-html/lune-noire-data-v20.json

Code de sortie 0 si tout passe, 1 sinon. Aucune écriture dans le dépôt.
"""

from __future__ import annotations

import argparse
import json
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import test_editor_roundtrip as rt  # noqa: E402

MASTER = rt.RACINE / "game/data/world/royaumes/world_master.json"
HISTORIQUES = {"villes": True, "lieux": True, "routes": True, "poi": True, "water": True, "islands": True, "regions": False}
AJOUTES = ["provinces", "niveaux", "densite", "jouables", "quetes", "raids", "balises", "peuples", "dragons", "ophidiens",
           "caches"]
SECRETS = ["dragons", "ophidiens", "caches"]
# v1.0.107 : lieux et ville cachés (champ secret) du Gouffre des Revenus.
CACHES_LIEUX = {"lieu_gouffre_des_revenus", "lieu_fente_des_revenus"}
CACHE_VILLE = "ville_thurvhal"
# Seuils de performance (ms) : détectent une lenteur grossière, pas une mesure fine.
SEUIL_DESSIN_MS = 600
SEUIL_SURVOL_MS = 25


class Echec(Exception):
    """Vérification ratée (message en français)."""


def verifier(cond, msg):
    """Lève Echec(msg) si ``cond`` est faux."""
    if not cond:
        raise Echec(msg)


def preparer(page, zoom=2):
    """Affiche la section Carte au zoom demandé et la fait défiler en haut de la fenêtre."""
    page.evaluate(f"() => {{ showSection('carte'); window._carteState.zoom = {zoom}; drawCarte(); }}")


def etat(page):
    """État courant des calques (``_carteState.filters``)."""
    return page.evaluate("() => Object.assign({}, window._carteState.filters)")


def lieux_dessines(page):
    """Identifiants des lieux repérables sur la carte (marqueurs du dernier dessin)."""
    return set(page.evaluate("() => _carteHits.filter(h => h.kind === 'lieux').map(h => h.rec.id)"))


def villes_dessinees(page):
    """Identifiants des villes repérables sur la carte (marqueurs du dernier dessin)."""
    return set(page.evaluate("() => _carteHits.filter(h => h.kind === 'villes').map(h => h.rec.id)"))


def ecran(page, x, y):
    """Point écran d'un point (x, y) du canvas, après défilement pour le rendre visible."""
    return page.evaluate(
        """([x, y]) => {
          const sc = document.getElementById('carteScroll');
          sc.scrollIntoView({block: 'start'});
          sc.scrollLeft = Math.max(0, x - sc.clientWidth / 2); sc.scrollTop = Math.max(0, y - sc.clientHeight / 2);
          const rc = document.getElementById('carteCanvas').getBoundingClientRect();
          return [rc.left + x, rc.top + y];
        }""", [x, y])


def survol(page, xy):
    """Déplace la souris sur ``xy`` (écran) et renvoie le texte de l'info-bulle."""
    page.mouse.move(xy[0] - 30, xy[1] - 30)
    page.mouse.move(*xy, steps=3)
    page.wait_for_timeout(40)
    return page.evaluate("() => { const t = document.getElementById('carteTip'); return t.style.display === 'none' ? '' : t.innerText; }")


def defauts(page):
    """1. Valeurs par défaut et grottes cachées."""
    fl = etat(page)
    for k, v in HISTORIQUES.items():
        verifier(fl.get(k) is v, f"calque {k} : défaut {fl.get(k)} au lieu de {v}")
    for k in AJOUTES:
        verifier(fl.get(k) is False, f"calque ajouté {k} actif par défaut")
    secrets = page.evaluate("() => Object.keys(_carteGeo.layers.secretLieux)")
    verifier(len(secrets) >= 16, f"lieux secrets : {len(secrets)} (11 grottes, 3 entrées et 2 lieux cachés attendus)")
    verifier(not (lieux_dessines(page) & set(secrets)), "grotte ou entrée secrète dessinée sans son calque")
    # v1.0.107 : ville cachée absente du calque Villes, même au plus fort zoom.
    preparer(page, 12)
    verifier(CACHE_VILLE not in villes_dessinees(page), "ville cachée dessinée sans son calque secret")
    verifier(page.evaluate("v => !carteVilleVisible(db.villes.find(x => x.id === v))", CACHE_VILLE), "ville cachée visible")
    preparer(page)
    print(f"   défauts conformes ; {len(secrets)} lieux secrets cachés")
    return secrets


def charger_master(page):
    """2. Chargement de world_master.json par le bouton du panneau."""
    attendu = len(set(json.loads(MASTER.read_text(encoding="utf-8"))["generated_cells"]))
    page.set_input_files("#carteMasterInput", str(MASTER))
    page.wait_for_function("() => window._carteJouables && window._carteJouables.count > 0", timeout=10_000)
    n = page.evaluate("() => window._carteJouables.count")
    verifier(n == attendu, f"cases jouables : {n} au lieu de {attendu}")
    verifier(etat(page)["jouables"] is True, "calque Cases jouables non activé après chargement")
    print(f"   carte du jeu chargée : {n} cases jouables")


def basculer_tout(page):
    """3. Chaque case du panneau cochée puis décochée ; sous-filtres."""
    cles = page.evaluate("() => [...document.querySelectorAll('#carteLayersPanel input[id^=calque_]')].map(e => e.id.slice(7))")
    verifier(set(AJOUTES) | set(HISTORIQUES) <= set(cles), f"cases absentes du panneau : {set(AJOUTES) | set(HISTORIQUES) - set(cles)}")
    for k in cles:
        avant = etat(page)[k]
        for _ in range(2):
            page.click(f"#calque_{k}")
            verifier(etat(page)[k] is (not avant), f"calque {k} : clic sans effet")
            verifier(page.is_checked(f"#calque_{k}") is (not avant), f"calque {k} : case non synchronisée")
            avant = not avant
    print(f"   {len(cles)} calques cochés puis décochés")
    # Sous-filtres : décocher un type de lieu retire ses marqueurs, recocher les rend.
    page.evaluate("() => { document.querySelectorAll('#carteLayersPanel details').forEach(d => d.open = true); }")
    for kind in ("lieu", "ville"):
        vals = page.evaluate(f"() => [...document.querySelectorAll('.calque-sub-{kind}')].map(e => e.getAttribute('data-val'))")
        verifier(vals, f"sous-filtres {kind} absents")
        champ = "type" if kind == "lieu" else "taille"
        coll = "lieux" if kind == "lieu" else "villes"
        for v in vals:
            sel = f".calque-sub-{kind}[data-val='{v}']"
            page.click(sel)
            reste = page.evaluate("([coll, champ, v]) => _carteHits.filter(h => h.kind === coll && (h.rec[champ] || '') === v).length",
                                  [coll, champ, v])
            verifier(reste == 0, f"sous-filtre {kind} « {v} » décoché : {reste} marqueur(s) encore dessiné(s)")
            page.click(sel)
        print(f"   {len(vals)} sous-filtres {kind} vérifiés")


def controles(page, secrets):
    """4. Secrets, niveaux, quêtes (survol et tracé), raids, balises, peuples."""
    for k, attendu in (("dragons", 11), ("ophidiens", 3), ("caches", 2)):
        page.click(f"#calque_{k}")
        n = page.evaluate("k => Object.keys(_carteGeo.layers.secretLieux).filter(i => _carteGeo.layers.secretLieux[i] === k)"
                          ".filter(i => _carteHits.some(h => h.rec.id === i)).length", k)
        verifier(n == attendu, f"calque {k} : {n} lieux dessinés au lieu de {attendu}")
        page.click(f"#calque_{k}")
    verifier(not (lieux_dessines(page) & set(secrets)), "lieux secrets restés dessinés après décochage")
    # v1.0.107 : le calque « caches » dessine aussi la ville cachée (info-bulle « secret »).
    page.click("#calque_caches")
    verifier(CACHE_VILLE in villes_dessinees(page) and CACHES_LIEUX <= lieux_dessines(page), "calque caches : gouffre, Fente ou Thurvhal absents")
    tip = page.evaluate("v => carteTipLayerExtra('villes', db.villes.find(x => x.id === v))", CACHE_VILLE)
    verifier("secret" in tip and "ville cachée" in tip, f"info-bulle de la ville cachée : « {tip[:80]} »")
    page.click("#calque_caches")
    verifier(CACHE_VILLE not in villes_dessinees(page), "ville cachée restée dessinée après décochage")

    niv = page.evaluate("() => [_carteGeo.layers.niveau['N002_W010'], _carteGeo.layers.niveau['S010_E002'], _carteGeo.layers.niveau['N011_E008']]")
    verifier(niv[0] and (niv[0]["min"], niv[0]["max"]) == (1, 10), f"niveau de N002_W010 (départ de Haeldorf) : {niv[0]}")
    verifier(niv[2] and (niv[2]["min"], niv[2]["max"]) == (1, 15), f"niveau de N011_E008 (poche des Terres du Nord) : {niv[2]}")

    preparer(page, 5)
    for k in ("quetes", "raids", "balises", "peuples", "niveaux"):
        page.click(f"#calque_{k}")
    # Survol d'une pastille de quête : info-bulle + tracé SVG.
    h = page.evaluate("() => { const h = _carteLayerHits.filter(x => x.quests).sort((a, b) => b.quests.length - a.quests.length)[0];"
                      " return [h.x, h.y, h.quests.length]; }")
    tip = survol(page, ecran(page, h[0], h[1]))
    verifier("quête" in tip, f"info-bulle de quête absente : « {tip[:80]} »")
    svg = page.evaluate("() => [document.querySelectorAll('#carteOverlay circle').length, document.querySelectorAll('#carteOverlay .carte-quest-path').length]")
    verifier(svg[0] >= h[2], f"tracé des étapes : {svg[0]} étape(s) dessinée(s) pour {h[2]} quête(s)")
    print(f"   survol de quêtes : {h[2]} quête(s), {svg[0]} étapes, {svg[1]} tracé(s)")
    page.mouse.move(5, 5)
    page.dispatch_event("#carteCanvas", "mouseleave")
    verifier(page.evaluate("() => document.querySelectorAll('#carteOverlay *').length") == 0, "tracé des quêtes non effacé")
    # Info-bulles des autres marqueurs.
    for motif, js in (("Raid", "h => h.html.indexOf('Raid') >= 0"), ("Balise", "h => h.html.indexOf('Balise') >= 0"),
                      ("direction", "h => h.html.indexOf('direction') >= 0")):
        h = page.evaluate(f"() => {{ const h = _carteLayerHits.find({js}); return h ? [h.x, h.y] : null; }}")
        verifier(h is not None, f"aucun marqueur « {motif} »")
        tip = survol(page, ecran(page, h[0], h[1]))
        verifier(motif in tip, f"info-bulle « {motif} » absente : « {tip[:80]} »")
    # Info-bulle d'une case (calque de fond Niveaux).
    xy = page.evaluate("() => { const p = carteCellCenter('S010_E002'); return [p.x + 6, p.y - 12]; }")
    tip = survol(page, ecran(page, *xy))
    verifier("Niveau" in tip, f"info-bulle de niveau absente : « {tip[:80]} »")
    print("   info-bulles quête, raid, balise, peuple, niveau")


def memorisation(page, nav, editeur, donnees):
    """5. État mémorisé après rechargement ; secrets jamais mémorisés ; stockage indisponible."""
    page.click("#calque_dragons")
    voulu = etat(page)
    page.reload()
    rt.charger(page, donnees)
    preparer(page)
    apres = etat(page)
    for k, v in voulu.items():
        attendu = False if k in SECRETS else v
        verifier(apres.get(k) is attendu, f"calque {k} après rechargement : {apres.get(k)} au lieu de {attendu}")
    verifier(page.evaluate("() => !!window._carteJouables && window._carteJouables.count > 0"), "cases jouables non mémorisées")
    print("   état retrouvé après rechargement (secrets désactivés)")
    # Navigateur sans stockage (accès qui lève une exception).
    p2, err2 = rt.ouvrir_editeur(nav, editeur)
    p2.add_init_script("Object.defineProperty(window, 'localStorage', { get() { throw new Error('bloqué'); } });")
    p2.reload()
    rt.charger(p2, donnees)
    preparer(p2)
    p2.click("#calque_quetes")
    verifier(etat(p2)["quetes"] is True, "sans stockage : calque non activable")
    verifier(not err2, f"sans stockage : erreurs {err2[:3]}")
    p2.close()
    print("   sans localStorage : fonctionne, valeurs par défaut")


def performance(page):
    """6. Mesures de drawCarte (rendu forcé) et du survol ; renvoie les lignes du rapport."""
    mesure = """z => { window._carteState.zoom = z; drawCarte(); const a = [];
      for (let i = 0; i < 5; i++) { const t0 = performance.now(); drawCarte();
        document.getElementById('carteCanvas').getContext('2d').getImageData(0, 0, 1, 1); a.push(performance.now() - t0); }
      a.sort((x, y) => x - y); return a[2]; }"""
    lignes, pire = [], 0
    for libelle, prep in (("calques historiques", "k => { k.forEach(x => window._carteState.filters[x] = false); }"),
                          ("tous les calques", "k => { k.forEach(x => window._carteState.filters[x] = true); }")):
        page.evaluate(prep, AJOUTES + ["regions"])
        t = {z: page.evaluate(mesure, z) for z in (2, 6, 12)}
        pire = max(pire, t[12])
        lignes.append(f"drawCarte, {libelle} : " + ", ".join(f"×{z} {v:.0f} ms" for z, v in t.items()))
    page.evaluate("() => { window._carteState.zoom = 6; drawCarte(); }")
    survol_ms = page.evaluate("""() => { const c = document.getElementById('carteCanvas'), r = c.getBoundingClientRect(), a = [];
      for (let i = 0; i < 200; i++) { const e = new MouseEvent('mousemove', { clientX: r.left + (i * 37) % 2000, clientY: r.top + (i * 53) % 1500 });
        const t0 = performance.now(); carteOnHover(e); a.push(performance.now() - t0); }
      a.sort((x, y) => x - y); return [a[100], a[190]]; }""")
    lignes.append(f"survol (×6, tous les calques) : médiane {survol_ms[0]:.2f} ms, 95e centile {survol_ms[1]:.2f} ms")
    verifier(pire <= SEUIL_DESSIN_MS, f"drawCarte ×12 : {pire:.0f} ms > {SEUIL_DESSIN_MS} ms")
    verifier(survol_ms[1] <= SEUIL_SURVOL_MS, f"survol : {survol_ms[1]:.1f} ms > {SEUIL_SURVOL_MS} ms")
    page.evaluate("k => { k.forEach(x => window._carteState.filters[x] = false); window._carteState.zoom = 2; drawCarte(); }", AJOUTES + ["regions"])
    return lignes


def captures(page, base: Path):
    """Écrit les captures demandées (--captures) : vue d'ensemble et trois vues ciblées."""
    base.parent.mkdir(parents=True, exist_ok=True)

    # Active exactement les calques cles (les autres désactivés), règle le zoom z et redessine la carte.
    def regler(cles, zoom):
        page.evaluate("([cles, tous, z]) => { tous.forEach(k => window._carteState.filters[k] = cles.indexOf(k) >= 0);"
                      " window._carteState.zoom = z; showSection('carte'); }",
                      [cles, AJOUTES + list(HISTORIQUES), zoom])

    # Centre la carte sur la case cellule, survole éventuellement un marqueur, écrit la capture <base><nom>.png.
    def vue(cellule, nom, survoler=None):
        page.evaluate("() => document.getElementById('carteScroll').scrollIntoView({block: 'start'})")
        page.evaluate("""c => { const p = carteCellCenter(c), sc = document.getElementById('carteScroll');
            sc.scrollLeft = Math.max(0, p.x - sc.clientWidth / 2); sc.scrollTop = Math.max(0, p.y - sc.clientHeight / 2); }""", cellule)
        if survoler:
            xy = page.evaluate(survoler)
            rc = page.evaluate("() => { const r = document.getElementById('carteCanvas').getBoundingClientRect(); return [r.left, r.top]; }")
            page.mouse.move(rc[0] + xy[0], rc[1] + xy[1])
            page.wait_for_timeout(80)
        page.locator("#carteScroll").screenshot(path=str(base.parent / f"{base.name}{nom}.png"))

    base_cles = ["villes", "lieux", "routes", "water", "islands"]
    page.set_viewport_size({"width": 1600, "height": 1500})
    regler(base_cles + ["regions", "provinces", "jouables", "quetes", "raids", "balises", "peuples"], 2)
    page.evaluate("() => { document.getElementById('carteLayersPanel').scrollIntoView({block: 'start'});"
                  " const sc = document.getElementById('carteScroll'); sc.scrollLeft = 160; sc.scrollTop = 120; }")
    page.screenshot(path=str(base.with_suffix(".png")))
    page.set_viewport_size({"width": 1500, "height": 1000})
    regler(base_cles + ["niveaux", "provinces"], 4)
    vue("S004_E004", "_niveaux")
    regler(base_cles + ["quetes", "raids", "balises"], 6)
    vue("S007_E017", "_quetes", "() => { const h = _carteLayerHits.find(x => x.quests && x.quests.length >= 3) || _carteLayerHits.find(x => x.quests);"
        " return [h.x, h.y]; }")
    regler(base_cles + ["dragons", "ophidiens", "balises", "jouables"], 4)
    vue("N010_E011", "_secrets")
    page.mouse.move(5, 5)


def main() -> int:
    """Lance le scénario (voir le docstring) ; code de sortie 0 si tout passe."""
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--editeur", type=Path, default=rt.EDITEUR)
    ap.add_argument("--donnees", type=Path, default=rt.DONNEES)
    ap.add_argument("--captures", type=Path, default=None, help="chemin sans extension des captures PNG")
    args = ap.parse_args()
    from playwright.sync_api import sync_playwright

    texte_original = args.donnees.read_text(encoding="utf-8")
    ok = True
    with sync_playwright() as p, tempfile.TemporaryDirectory() as tmp:
        nav = p.chromium.launch()
        page, erreurs = rt.ouvrir_editeur(nav, args.editeur, taille=(1500, 1000))
        rt.charger(page, args.donnees)
        preparer(page)
        try:
            secrets = defauts(page)
            charger_master(page)
            basculer_tout(page)
            controles(page, secrets)
            memorisation(page, nav, args.editeur, args.donnees)
            for ligne in performance(page):
                print("   " + ligne)
            if args.captures:
                captures(page, args.captures)
                print(f"   captures : {args.captures.with_suffix('.png')} (+ _niveaux, _quetes, _secrets)")
            print("[OK] calques : défauts, chargement de la carte du jeu, cases, sous-filtres, secrets, info-bulles, mémorisation, performance")
        except Echec as e:
            ok = False
            print(f"[ÉCHEC] {e}")
        _, texte = rt.exporter(page, Path(tmp), "calques.json")
        identique = texte == texte_original
        print(f"[{'OK' if identique else 'ÉCHEC'}] export après les calques identique octet pour octet au fichier")
        ok &= identique
        if erreurs:
            ok = False
            print(f"[ÉCHEC] {len(erreurs)} erreur(s) console :")
            for e in erreurs[:10]:
                print("   ", e)
        else:
            print("[OK] aucune erreur console")
        nav.close()
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
