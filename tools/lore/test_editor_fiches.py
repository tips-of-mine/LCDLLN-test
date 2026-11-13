#!/usr/bin/env python3
"""Test des « fiches complètes » de l'éditeur de lore (v1.0.100), avec Playwright.

Vérifie, sur ``lune-noire-data.json`` chargé dans ``lune-noire-editor.html`` (Chromium
sans affichage) :

1. **niveau de référence** : ``regions[].niveau_ref`` et ``raids[].niveau_ref`` présents
   ({min, max} ou null) et égaux aux repères attendus ; le calque Niveaux les lit (aucune
   case ne passe par la table de secours) et donne, case par case, le même résultat
   qu'avec la table de la v1.0.99 (champ retiré : secours) ; formulaires de région et de
   raid : valeur affichée, modification (seul ce chemin change à l'export), fourchette
   invalide refusée, retour à l'identique ;
2. **niveaux par faction** : ``niveau_par_faction`` vide partout ; ajout d'une valeur par
   le tableau du formulaire de région ; le sélecteur de faction du calque fait primer
   cette valeur, « Référence » revient au niveau de référence ; retrait : export identique ;
3. **étapes de quête situées** : ``etapes_struct`` présent sur chaque quête, texte des
   étapes (``steps``) inchangé, références existantes ; éditeur d'étapes du formulaire
   (lignes rendues, ajout, sélection d'une ville, d'un lieu, d'un PNJ et d'une case,
   réordonnancement, suppression ; case inconnue refusée) ; le calque Quêtes utilise les
   étapes en priorité ; nombre de quêtes plaçables avant (sans ``etapes_struct``) / après ;
4. **type « mine »** : ``mine_poi`` dans ``place_types``, les 8 lieux reclassés en v1.0.100
   toujours de ce type (les mines ajoutées depuis sont lues dans les données), proposé par le
   formulaire de lieu et le sous-filtre des lieux ; les POI correspondants du
   ``world_master.json`` gardent leur ``kind`` (salle ou ruine) et le générateur ne lit pas
   le type du lore ;
5. **formulaires** : faction (toutes les clés des sous-objets ont un champ ; un champ de
   fiche de peuple lointain, du comptoir et de ``game_design`` modifiés : seuls ces chemins
   changent ; JSON invalide refusé), race (``class_access`` et notes d'affiliation), PNJ
   (``description``), champ ``territory.ville_id`` (v1.0.109) ; toutes les fiches des
   formulaires de région, raid, quête, faction, race et PNJ enregistrées sans modification : export inchangé ;
6. **suppression** : la boîte HTML affiche le nombre de références (identique au compte
   du test) ; « Annuler » ne change rien ; ``deleteRule`` demande aussi confirmation ;
7. export final identique octet pour octet ; aucune erreur console.

``--captures <chemin sans extension>`` écrit ``<chemin>.png`` (formulaire de quête avec
les étapes situées) et ``<chemin>_region.png`` (formulaire de région avec les niveaux).

Lancement (depuis la racine du dépôt, Playwright installé comme pour
``test_editor_roundtrip.py``) ::

    python3 tools/lore/test_editor_fiches.py
    python3 tools/lore/test_editor_fiches.py --editeur legacy/world-builder-html/lune-noire-editor-v20.html \\
            --donnees legacy/world-builder-html/lune-noire-data-v20.json

Code de sortie 0 si tout passe, 1 sinon. Aucune écriture dans le dépôt.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import test_editor_roundtrip as rt  # noqa: E402
import test_editor_forms as tf  # noqa: E402

MASTER = rt.RACINE / "game/data/world/royaumes/world_master.json"
# Repères attendus (table CARTE_NIVEAUX_REF de la v1.0.99 + Enfers, décision v1.0.71).
NIVEAUX = {
    "terres_des_gigantomorphes": (30, 56), "foret_elfique": (10, 30), "territoires_orcques": (18, 45),
    "royaume_des_nains_de_lest": (10, 30), "baronnie_de_moorks": (18, 45), "via_magna": (10, 30),
    "royaume_drach": (30, 56), "terres_de_lempereur": (10, 30), "syzygie": (10, 30),
    "region_ile_de_la_treve": (45, 60), "region_marches": (1, 15), "region_terres_nord_t00": (1, 15),
    "marais_maudits_t11": (40, 56), "region_marche_est": (15, 28), "terres_desolees": (20, 40),
    "desert_central": (10, 25), "foret_des_lutins": (14, 24), "val_de_southport": (18, 28),
    "desert_des_sels": (24, 36), "confins_du_midi": (22, 34), "region_enfers_t00": (1, 10),
}
# v1.0.103 (lot 28, LORE_BD §16.32) : niveaux [GD] des régions et provinces qui n'en avaient pas
# (absentes de la table v1.0.99 : le calque en diffère sur leurs cases seulement).
NIVEAUX_V1103 = {
    "royaumes_des_nains_du_sud": (10, 30), "region_hishtarland": (25, 40),
    "region_premiere_province_empire": (10, 22), "region_northind": (12, 24),
    "region_grande_plaine_bataille_t10": (45, 55),
}
NIVEAUX.update(NIVEAUX_V1103)
# Raids sans niveau en v1.0.103 : contenu d'extension (Empire du Jade).
RAIDS_NULS = {"raid_invocation_methraton_t7", "raid_bataille_necropolis_jade_t7"}
RAIDS = {"raid_mine_de_la_traverse": (10, 15), "raid_tertre_des_rois_noyes": (45, 50),
         "raid_grottes_des_naufrageurs": (50, 55), "raid_taniere_du_jarl": (55, 60),
         "raid_devoreur_des_brumes": (60, 60)}
# Les 8 lieux reclassés en mine en v1.0.100 (sous-ensemble garanti ; d'autres mines [GD]
# ont été ajoutées depuis, le test lit leur liste dans les données).
MINES = {"lieu_mine_de_wardholt", "lieu_mine_de_la_traverse", "lieu_mine_de_soufre_abandonnee",
         "lieu_galerie_naine_abandonnee", "lieu_mine_de_fer_abandonnee", "lieu_puits_de_mine_inonde",
         "lieu_puits_des_apprentis", "lieu_galeries_effondrees_vieille_veine"}


def quetes_avant_attendues(original):
    """Nombre de quêtes ancrées par ``source_lore`` seul (sans ``etapes_struct``), calculé
    depuis les données comme le fait ``carteBuildLayers`` : une quête compte si l'un des
    identifiants de ``source_lore`` est une ville ou un lieu dont la case existe. Valeurs
    historiques : 72 en v1.0.99 (LORE_BD §16.28), 90 en v1.0.103 (§16.32), 96 en v1.0.108.
    ``original`` : lore chargé (dict). Pas d'effet de bord."""
    cells = {c["id"] for c in original["cells"]}
    recs = {x["id"]: x for x in original["lieux"]}
    recs.update({x["id"]: x for x in original["villes"]})  # l'éditeur cherche d'abord les villes
    return sum(1 for q in original["quests"]
               if any(r in recs and recs[r].get("cell") in cells
                      for r in (q.get("source_lore") if isinstance(q.get("source_lore"), list) else [])))


class Echec(Exception):
    """Vérification ratée (message en français)."""


def verifier(cond, msg):
    """Lève Echec(msg) si ``cond`` est faux."""
    if not cond:
        raise Echec(msg)


def idx(data, coll, rid):
    """Indice de la fiche ``rid`` dans ``data[coll]``."""
    return next(i for i, x in enumerate(data[coll]) if x.get("id") == rid)


def diffs(page, original):
    """Différences de l'export courant avec l'original (chemins diff_profond)."""
    return tf.diffs_export(page, original)


def seulement(page, original, prefixes, msg):
    """Vérifie que l'export ne diffère de l'original que sur des chemins ``prefixes``."""
    d = diffs(page, original)
    verifier(d, f"{msg} : aucune différence à l'export")
    autres = [x for x in d if not any(x.startswith(p) for p in prefixes)]
    verifier(not autres, f"{msg} : différences inattendues {autres[:4]}")
    return d


def identique(page, original, msg):
    """Vérifie que l'export est identique à l'original."""
    d = diffs(page, original)
    verifier(not d, f"{msg} : {d[:4]}")


def references(original, coll, rid):
    """Nombre de valeurs chaîne égales à ``rid`` hors de la fiche elle-même."""
    n = 0

    # Parcours récursif : compte les valeurs chaîne égales à rid.
    def rec(x):
        nonlocal n
        if isinstance(x, dict):
            for v in x.values():
                rec(v)
        elif isinstance(x, list):
            for v in x:
                rec(v)
        elif x == rid:
            n += 1
    for k, v in original.items():
        if k == coll:
            for f in v:
                if f.get("id") != rid:
                    rec(f)
        else:
            rec(v)
    return n


# ── 1-4. Données ──────────────────────────────────────────────────────────────

def donnees(original):
    """Contrôles sur le fichier (champs ajoutés, valeurs, mines). Renvoie un résumé."""
    for rg in original["regions"]:
        verifier("niveau_ref" in rg and "niveau_par_faction" in rg, f"région {rg['id']} sans champ de niveau")
        # v1.0.103 : rempli pour toutes les factions jouables là où la région a un niveau de
        # référence et une case (régions du hors-série, Île de la Trêve, provinces), vide ailleurs.
        jouables = {f["id"] for f in original["factions"] if f.get("playable")}
        npf = rg["niveau_par_faction"]
        if npf:
            verifier(set(npf) == jouables, f"région {rg['id']} : niveau_par_faction incomplet")
            for f_, n_ in npf.items():
                verifier(1 <= n_["min"] <= n_["max"] <= 70 and n_["min"] >= rg["niveau_ref"]["min"],
                         f"région {rg['id']} : niveau de {f_} {n_}")
        else:
            verifier(rg.get("type") == "region" and rg["id"] != "region_ile_de_la_treve",
                     f"région {rg['id']} : niveau_par_faction vide")
        att = NIVEAUX.get(rg["id"])
        verifier(rg["niveau_ref"] == ({"min": att[0], "max": att[1]} if att else None),
                 f"région {rg['id']} : niveau_ref {rg['niveau_ref']} au lieu de {att}")
    for r in original["raids"]:
        att = RAIDS.get(r["id"])
        verifier("niveau_ref" in r, f"raid {r['id']} sans niveau_ref")
        if att:
            verifier(r["niveau_ref"] == {"min": att[0], "max": att[1]}, f"raid {r['id']} : niveau_ref {r.get('niveau_ref')}")
        elif r["id"] in RAIDS_NULS:
            verifier(r["niveau_ref"] is None, f"raid {r['id']} : niveau_ref {r.get('niveau_ref')} (extension)")
        else:  # v1.0.103 : niveau [GD] proposé
            n_ = r["niveau_ref"]
            verifier(n_ and 1 <= n_["min"] <= n_["max"] <= 70, f"raid {r['id']} : niveau_ref {n_}")
    ids = {k: {x["id"] for x in original[k]} for k in ("villes", "lieux", "persons")}
    cells = {c["id"] for c in original["cells"]}
    remplies = 0
    for q in original["quests"]:
        verifier(isinstance(q.get("etapes_struct"), list), f"quête {q['id']} sans etapes_struct")
        es = q["etapes_struct"]
        if es:
            remplies += 1
            verifier([e["texte"] for e in es] == q["steps"], f"quête {q['id']} : textes des étapes différents de steps")
        for e in es:
            verifier(set(e) <= {"texte", "cell", "ville_id", "lieu_id", "pnj_id"}, f"quête {q['id']} : clé inconnue {set(e)}")
            verifier(len(set(e) - {"texte"}) <= 1, f"quête {q['id']} : étape pré-remplie avec plusieurs références")
            for k, coll in (("ville_id", "villes"), ("lieu_id", "lieux"), ("pnj_id", "persons")):
                verifier(k not in e or e[k] in ids[coll], f"quête {q['id']} : {k} {e.get(k)} inconnu")
            verifier("cell" not in e or e["cell"] in cells, f"quête {q['id']} : case inconnue")
    verifier(any(p["id"] == "mine_poi" for p in original["place_types"]), "type mine_poi absent")
    mines = {l["id"] for l in original["lieux"] if l["type"] == "mine_poi"}
    verifier(MINES <= mines, f"mines reclassées en v1.0.100 qui ne sont plus des mines : {sorted(MINES - mines)}")
    # Cohérence avec les POI du master : même kind qu'avant (salle / ruine), aucun type « mine ».
    master = json.loads(MASTER.read_text(encoding="utf-8"))
    kinds = {}
    for p in master["pois"]:
        for m in MINES:
            suf = m[len("lieu_"):]
            if p["id"] in (f"poi_{suf}",) or (suf == "mine_de_la_traverse" and p["id"].startswith("poi_mine_traverse")):
                kinds[p["id"]] = p["kind"]
    verifier(kinds and set(kinds.values()) <= {"salle", "ruine"}, f"POI des mines : {kinds}")
    gen = "\n".join(f.read_text(encoding="utf-8", errors="replace") for f in (rt.RACINE / "tools/world_gen").rglob("*.*")
                    if f.suffix in {".cpp", ".h", ".py"})
    verifier("mine_poi" not in gen and "place_types" not in gen and "lune-noire-data" not in gen,
             "le générateur lit le type de lieu du lore")
    return f"{len(NIVEAUX)} régions et {len(RAIDS)} raids avec niveau_ref ; {remplies} quêtes pré-remplies ; " \
           f"{len(mines)} mines ; POI du master : {len(kinds)} ({', '.join(sorted(set(kinds.values())))})"


# ── 1-2. Calque Niveaux et formulaires de région / raid ───────────────────────

def niveaux(page, original, cap):
    """Calque Niveaux (champ des fiches, secours, faction) et formulaires de région et de raid."""
    page.evaluate("() => { showSection('carte'); window._carteState.filters.niveaux = true; drawCarte(); }")
    r = page.evaluate(("""() => {
      const L1 = carteBuildLayers(), a = {};
      Object.keys(L1.niveau).forEach(k => { const n = L1.niveau[k]; a[k] = n ? n.min + '-' + n.max : null; });
      // Secours : fiches sans champ niveau_ref (fichier v1.0.99) → table CARTE_NIVEAUX_REF.
      const sv = db.regions.map(r => r.niveau_ref); db.regions.forEach(r => { delete r.niveau_ref; });
      const L2 = carteBuildLayers(), b = {};
      Object.keys(L2.niveau).forEach(k => { const n = L2.niveau[k]; b[k] = n ? n.min + '-' + n.max : null; });
      db.regions.forEach((r, i) => { r.niveau_ref = sv[i]; });
      const diff = Object.keys(a).filter(k => a[k] !== b[k]).concat(Object.keys(b).filter(k => !(k in a)));
      const nouv = new Set(%s);
      const diffHors = diff.filter(k => { const c = db.cells.find(x => x.id === k); return !c || !(nouv.has(c.region_id) || nouv.has(c.province)); });
      return { secours: L1.niveauSecours, secours2: L2.niveauSecours, diff: diffHors, ndiff: diff.length, nul: Object.values(a).filter(v => v === null).length,
               rens: Object.values(a).filter(v => v).length, src: (L1.niveau['N006_W013'] || {}).src || '' };
    }""") % json.dumps(sorted(NIVEAUX_V1103)))
    verifier(r["secours"] == 0, f"{r['secours']} fiche(s) lues dans la table de secours")
    verifier(r["secours2"] > 0 and not r["diff"], f"calque différent de la table v1.0.99 : {r['diff'][:5]}")
    verifier(r["nul"] == 0, f"cases sans niveau : {r['nul']} (0 attendue depuis la v1.0.103)")
    print(f"   calque Niveaux : {r['rens']} cases lues dans niveau_ref, {r['nul']} non renseignées ; identique à la table v1.0.99 "
          f"hors des {r['ndiff']} cases des régions renseignées en v1.0.103 (secours vérifié)")

    # Formulaire de région : Hishtarland (25-40 depuis la v1.0.103) → 30-40, refus, retour.
    i = idx(original, "regions", "region_hishtarland")
    page.evaluate(f"() => openRegionForm({i})")
    verifier(page.input_value("#rg_nmin") == "25" and page.input_value("#rg_nmax") == "40", "Hishtarland : 25-40 non affiché")
    tf.ecrire_champ(page, "#rg_nmin", "40")
    tf.ecrire_champ(page, "#rg_nmax", "30")
    refus = tf.enregistrer(page)
    verifier(refus and "minimum" in refus, f"fourchette inversée acceptée ({refus})")
    page.evaluate(f"() => openRegionForm({i})")
    tf.ecrire_champ(page, "#rg_nmin", "30")
    tf.ecrire_champ(page, "#rg_nmax", "")
    verifier(tf.enregistrer(page), "fourchette incomplète acceptée")
    page.evaluate(f"() => openRegionForm({i})")
    tf.ecrire_champ(page, "#rg_nmin", "30")
    tf.ecrire_champ(page, "#rg_nmax", "40")
    verifier(not tf.enregistrer(page), "enregistrement de Hishtarland refusé")
    seulement(page, original, [f"regions[{i}].niveau_ref"], "niveau de Hishtarland")
    cell = next(c["id"] for c in original["cells"] if c.get("region_id") == "region_hishtarland" and not c.get("province"))
    n = page.evaluate(f"() => {{ const n = carteBuildLayers().niveau['{cell}']; return n ? n.min + '-' + n.max : null; }}")
    verifier(n == "30-40", f"calque : {cell} {n} au lieu de 30-40")
    page.evaluate(f"() => openRegionForm({i})")
    tf.ecrire_champ(page, "#rg_nmin", "25")
    tf.ecrire_champ(page, "#rg_nmax", "40")
    tf.enregistrer(page)
    identique(page, original, "Hishtarland remis à 25-40")

    # Niveaux par faction : Val de Suthvarn, faction Légions impériales (ou la première jouable).
    j = idx(original, "regions", "val_de_southport")
    fac = next(f["id"] for f in original["factions"] if f.get("playable"))
    page.evaluate(f"() => openRegionForm({j})")
    verifier(page.input_value("#rg_nmin") == "18" and page.input_value("#rg_nmax") == "28", "Val de Suthvarn : 18-28 non affiché")
    # v1.0.103 : la ligne de la faction existe déjà (18-28) ; on la modifie.
    cles = list(original["regions"][j]["niveau_par_faction"])
    verifier(fac in cles, f"Val de Suthvarn : pas de ligne {fac}")
    page.select_option("#rgNpfSel", fac)
    page.click("button:has-text('+ Faction')")  # refusée : faction déjà présente
    verifier(page.locator(".rg-npf-min").count() == len(cles), "faction ajoutée deux fois")
    page.locator(".rg-npf-min").nth(cles.index(fac)).fill("5")
    page.locator(".rg-npf-max").nth(cles.index(fac)).fill("9")
    if cap:
        page.evaluate("() => document.getElementById('modalBody').scrollTop = 0")
        page.locator(".modal").first.screenshot(path=str(cap.with_name(cap.stem + "_region.png")))
    verifier(not tf.enregistrer(page), "niveau par faction refusé")
    d = seulement(page, original, [f"regions[{j}].niveau_par_faction"], "niveau par faction")
    verifier(page.evaluate(f"() => JSON.stringify(db.regions[{j}].niveau_par_faction['{fac}'])") == json.dumps({"min": 5, "max": 9}, separators=(",", ":")),
             "niveau_par_faction mal écrit")
    vcell = next(c["id"] for c in original["cells"] if c.get("province") == "val_de_southport")
    page.evaluate("() => { renderCarte(document.getElementById('sectionContent')); }")
    page.select_option("#carteNiveauFaction", fac)
    n1 = page.evaluate(f"() => {{ const n = _carteGeo.layers.niveau['{vcell}']; return n ? n.min + '-' + n.max : null; }}")
    # v1.0.103 : le carré de départ de la faction choisie garde 1-10 malgré niveau_par_faction.
    dep = next(f for f in original["factions"] if f["id"] == fac)["game_design"]["depart"]["cell"]
    nd = page.evaluate(f"() => {{ const n = _carteGeo.layers.niveau['{dep}']; return n ? n.min + '-' + n.max : null; }}")
    verifier(nd == "1-10", f"départ de {fac} ({dep}) : {nd} au lieu de 1-10")
    page.select_option("#carteNiveauFaction", "")
    n0 = page.evaluate(f"() => {{ const n = _carteGeo.layers.niveau['{vcell}']; return n ? n.min + '-' + n.max : null; }}")
    verifier(n1 == "5-9" and n0 == "18-28", f"sélecteur de faction : {n1} / {n0}")
    page.evaluate(f"() => openRegionForm({j})")
    page.locator(".rg-npf-min").nth(cles.index(fac)).fill("18")
    page.locator(".rg-npf-max").nth(cles.index(fac)).fill("28")
    tf.enregistrer(page)
    identique(page, original, "niveau par faction remis à 18-28")
    print(f"   formulaire de région : niveau 30-40 (Hishtarland), refus min > max et fourchette incomplète ; "
          f"niveau par faction {fac} 5-9 (calque : {n1} avec la faction, {n0} en référence, départ {nd}) ; retour à l'identique ({len(d)} chemin(s))")

    # Formulaire de raid.
    k = idx(original, "raids", "raid_mine_de_la_traverse")
    page.evaluate(f"() => openRaidForm({k})")
    verifier(page.input_value("#rd_nmin") == "10" and page.input_value("#rd_nmax") == "15", "raid : 10-15 non affiché")
    verifier("Mère des galeries" in page.inner_text("#modalBody") or "galeries" in page.inner_text("#modalBody"), "raid : boss non rappelé")
    tf.ecrire_champ(page, "#rd_nmin", "12")
    verifier(not tf.enregistrer(page), "raid refusé")
    seulement(page, original, [f"raids[{k}].niveau_ref.min"], "niveau du raid")
    lv = page.evaluate("() => { const it = carteBuildLayers().raids.find(x => x.raid.id === 'raid_mine_de_la_traverse'); return it && it.niveauRef ? it.niveauRef.min + '-' + it.niveauRef.max : null; }")
    verifier(lv == "12-15", f"calque Raids : {lv}")
    page.evaluate(f"() => openRaidForm({k})")
    tf.ecrire_champ(page, "#rd_nmin", "10")
    tf.enregistrer(page)
    identique(page, original, "raid remis à 10-15")
    print("   formulaire de raid : 10-15 affiché, boss rappelé, 12-15 enregistré et lu par le calque Raids, retour à l'identique")


# ── 3. Étapes de quête ────────────────────────────────────────────────────────

def quetes(page, original, cap):
    """Éditeur d'étapes situées, calque Quêtes, quêtes plaçables avant / après."""
    r = page.evaluate("""() => {
      const L = carteBuildLayers();
      const sv = db.quests.map(q => q.etapes_struct); db.quests.forEach(q => { q.etapes_struct = []; });
      const L0 = carteBuildLayers(); db.quests.forEach((q, i) => { q.etapes_struct = sv[i]; });
      return { apres: L.quetesAncrees, parEtapes: L.quetesParEtapes, avant: L0.quetesAncrees, total: L.quetesTotal };
    }""")
    att = quetes_avant_attendues(original)
    verifier(r["avant"] == att, f"quêtes ancrées sans etapes_struct : {r['avant']} au lieu de {att}")
    verifier(r["apres"] > r["avant"] and r["parEtapes"] > 0, f"quêtes ancrées : {r['apres']}")
    print(f"   quêtes plaçables : {r['avant']}/{r['total']} avant, {r['apres']}/{r['total']} après ({r['parEtapes']} par leurs étapes situées)")

    qid = "quest_signe_tenebres"
    i = idx(original, "quests", qid)
    q = original["quests"][i]
    page.evaluate(f"() => openQuestForm({i})")
    n = page.locator("#qtEtapesList .qt-etape").count()
    verifier(n == len(q["etapes_struct"]), f"étapes rendues : {n} au lieu de {len(q['etapes_struct'])}")
    # Valeurs des listes reprises (étape 2 : PNJ Pile-ou-Face, voir données).
    for k, e in enumerate(q["etapes_struct"]):
        for champ, cls in (("ville_id", "qt-et-ville"), ("lieu_id", "qt-et-lieu"), ("pnj_id", "qt-et-pnj")):
            v = page.evaluate(f"() => document.querySelectorAll('#qtEtapesList .qt-etape')[{k}].querySelector('.{cls}').value")
            verifier(v == e.get(champ, ""), f"étape {k + 1} : {champ} {v!r} au lieu de {e.get(champ, '')!r}")
    # Ajout d'une étape, ville choisie, montée de deux rangs, case inconnue refusée puis corrigée.
    page.click("#qtEtapeAddBtn")
    last = page.locator("#qtEtapesList .qt-etape").last
    last.locator(".qt-et-texte").fill("Rejoindre Brenhaval par la côte (test v1.0.100).")
    last.locator(".qt-et-ville").select_option("town_brinehaven")
    last.locator(".qt-et-up").click()
    page.locator("#qtEtapesList .qt-etape").nth(n - 1).locator(".qt-et-up").click()
    verifier("Brenhaval" in page.locator("#qtEtapesList .qt-etape").nth(n - 2).inner_text(), "étape non remontée")
    lieu = page.locator("#qtEtapesList .qt-etape").nth(0)
    lieu.locator(".qt-et-lieu").select_option("lieu_mine_de_la_traverse")
    lieu.locator(".qt-et-pnj").select_option("char_ghorghor_bey")
    lieu.locator(".qt-et-cell").fill("N999_W999")
    lieu.locator(".qt-et-cell").dispatch_event("change")
    refus = tf.enregistrer(page)
    verifier(refus and "case inconnue" in refus, f"case inconnue acceptée ({refus})")
    # Le formulaire a été fermé par enregistrer() : on recommence avec une case valide.
    page.evaluate(f"() => openQuestForm({i})")
    page.click("#qtEtapeAddBtn")
    last = page.locator("#qtEtapesList .qt-etape").last
    last.locator(".qt-et-texte").fill("Rejoindre Brenhaval par la côte (test v1.0.100).")
    last.locator(".qt-et-ville").select_option("town_brinehaven")
    last.locator(".qt-et-up").click()
    first = page.locator("#qtEtapesList .qt-etape").nth(0)
    first.locator(".qt-et-cell").fill("N010_W011")
    first.locator(".qt-et-cell").dispatch_event("change")
    first.locator(".qt-et-pnj").select_option("char_ghorghor_bey")
    page.locator("#qtEtapesList .qt-etape").nth(2).locator(".qt-et-del").click()
    if cap:
        page.evaluate("() => document.getElementById('qtEtapesList').previousElementSibling.scrollIntoView({block: 'start'})")
        page.locator(".modal").first.screenshot(path=str(cap.with_suffix(".png")))
    verifier(not tf.enregistrer(page), "enregistrement des étapes refusé")
    seulement(page, original, [f"quests[{i}].etapes_struct"], "étapes situées")
    es = page.evaluate(f"() => db.quests[{i}].etapes_struct")
    att = [dict(e) for e in q["etapes_struct"]]
    att[0].update({"cell": "N010_W011", "pnj_id": "char_ghorghor_bey"})
    att.insert(n - 1, {"texte": "Rejoindre Brenhaval par la côte (test v1.0.100).", "ville_id": "town_brinehaven"})
    del att[2]
    norm = [{"texte": e["texte"], **{k: e[k] for k in ("cell", "ville_id", "lieu_id", "pnj_id") if k in e}} for e in att]
    verifier(es == norm, f"étapes enregistrées : {json.dumps(es, ensure_ascii=False)[:300]}")
    verifier(page.evaluate(f"() => JSON.stringify(db.quests[{i}].steps)") == json.dumps(q["steps"], ensure_ascii=False, separators=(",", ":")),
             "texte des étapes (steps) modifié")
    # Calque : chemin par les étapes, numéros = rangs des étapes situées.
    path = page.evaluate(f"""() => {{ const qp = carteBuildLayers().quetePaths['{qid}'];
        return qp ? {{ via: qp.via, cells: qp.path.map(s => s.rec.cell), n: qp.path.map(s => s.n) }} : null; }}""")
    verifier(path and path["via"] == "etapes" and path["cells"][0] == "N010_W011", f"calque Quêtes : {path}")
    # Tracé SVG au survol de la pastille de la case de la première étape.
    page.evaluate(f"() => {{ showSection('carte'); window._carteState.filters.quetes = true; window._carteState.zoom = 4; drawCarte(); carteQuestOverlay(['{qid}']); }}")
    nums = page.evaluate("() => Array.from(document.querySelectorAll('#carteOverlay text')).map(t => +t.textContent)")
    verifier(nums == path["n"], f"numéros du tracé {nums} au lieu de {path['n']}")
    page.evaluate("() => carteQuestOverlay(null)")
    # « Reprendre le texte » : une étape par ligne, références gardées par rang.
    page.evaluate(f"() => openQuestForm({i})")
    page.click("#qtEtapesSyncBtn")
    verifier(page.locator("#qtEtapesList .qt-etape").count() == len(q["steps"]), "reprise du texte : nombre d'étapes")
    page.evaluate("() => closeModal()")
    print(f"   éditeur d'étapes : {n} lignes reprises, ajout, ville, case, PNJ, réordonnancement, suppression, case inconnue refusée ; "
          f"calque par etapes_struct (étapes {path['n']}) ; steps inchangé")
    tf.recharger(page, tf.DONNEES)
    identique(page, original, "rechargement après le test des étapes")


# ── 4. Mines ──────────────────────────────────────────────────────────────────

def mines(page, original):
    """Type mine_poi dans le formulaire de lieu et le sous-filtre du calque des lieux."""
    i = idx(original, "lieux", "lieu_mine_de_wardholt")
    page.evaluate(f"() => openLieuForm({i})")
    opts = page.evaluate("() => Array.from(document.querySelectorAll('#modalBody select option')).map(o => o.value)")
    verifier("mine_poi" in opts, "mine_poi absent du formulaire de lieu")
    page.evaluate("() => closeModal()")
    n = page.evaluate("() => (carteBuildLayers().lieuTypes || {})['mine_poi'] || 0")
    verifier(n == len([l for l in original["lieux"] if l["type"] == "mine_poi" and l.get("cell")]), f"sous-filtre mine : {n}")
    page.evaluate("() => { showSection('carte'); }")
    lab = page.evaluate("() => Array.from(document.querySelectorAll('.calque-sub-lieu')).filter(c => c.getAttribute('data-val') === 'mine_poi').map(c => c.parentElement.textContent.trim())[0] || ''")
    verifier(lab.startswith("Mine"), f"libellé du sous-filtre : {lab!r}")
    print(f"   type mine_poi : formulaire de lieu, sous-filtre « {lab.strip()} »")


# ── 5. Formulaires ────────────────────────────────────────────────────────────

def formulaires(page, original):
    """Couverture des champs (faction, race, PNJ), modifications ciblées, enregistrement sans modification."""
    # Faction : chaque clé feuille d'un sous-objet a un champ fsub_<clé>_<sous-clé>.
    subs = page.evaluate("() => FACTION_SUBFORM.map(s => s.key)")
    manquants = set()
    for i, f in enumerate(original["factions"]):
        page.evaluate(f"() => openFactionForm({i})")
        champs = set(page.evaluate("() => Array.from(document.querySelectorAll('#modalBody .fsub-field')).map(e => e.id)"))
        for k in subs:
            v = f.get(k)
            if isinstance(v, dict):
                for sk, sv in v.items():
                    if isinstance(sv, dict):
                        manquants |= {f"{k}.{sk}.{x}" for x in sv if f"fsub_{k}_{sk}_{x}" not in champs}
                    elif f"fsub_{k}_{sk}" not in champs:
                        manquants.add(f"{k}.{sk}")
        if "game_design" in f:
            txt = page.input_value("#f_game_design")
            verifier(json.loads(txt) == f["game_design"], f"game_design de {f['id']} mal affiché")
        page.evaluate("() => closeModal()")
    couverts = set(page.evaluate("() => ['id','name','type','alignment','region_id','region_ids','playable','notes','faction_relations','ville_relations','game_design'].concat(FACTION_SUBFORM.map(s => s.key))"))
    hors = {k for f in original["factions"] for k in f} - couverts
    verifier(not manquants and not hors, f"faction : champs sans formulaire {sorted(manquants | hors)}")
    # Modifications ciblées sur un peuple lointain.
    i = idx(original, "factions", "faction_nordiques")
    page.evaluate(f"() => openFactionForm({i})")
    tf.ecrire_champ(page, "#fsub_fiche_peuple_culture", original["factions"][i]["fiche_peuple"]["culture"] + " (test)")
    tf.ecrire_champ(page, "#fsub_territory_comptoir_nom", "Comptoir test")
    gd = dict(original["factions"][i]["game_design"])
    gd["note"] = "test v1.0.100"
    tf.ecrire_champ(page, "#f_game_design", json.dumps(gd, ensure_ascii=False, indent=2))
    verifier(not tf.enregistrer(page), "faction refusée")
    d = seulement(page, original, [f"factions[{i}].fiche_peuple.culture", f"factions[{i}].territory.comptoir.nom",
                                   f"factions[{i}].game_design.note"], "faction")
    verifier(len(d) == 3, f"faction : {d}")
    page.evaluate(f"() => openFactionForm({i})")
    tf.ecrire_champ(page, "#f_game_design", "{ pas du JSON")
    refus = tf.enregistrer(page)
    verifier(refus and "JSON" in refus, "game_design invalide accepté")
    page.evaluate(f"() => openFactionForm({i})")
    tf.ecrire_champ(page, "#fsub_fiche_peuple_culture", original["factions"][i]["fiche_peuple"]["culture"])
    tf.ecrire_champ(page, "#fsub_territory_comptoir_nom", original["factions"][i]["territory"]["comptoir"]["nom"])
    tf.ecrire_champ(page, "#f_game_design", json.dumps(original["factions"][i]["game_design"], ensure_ascii=False))
    tf.enregistrer(page)
    identique(page, original, "faction remise")
    # v1.0.109 : territory.ville_id (Ghaurdes → Thurvhal) a son champ ; modifié puis remis,
    # seul ce chemin change.
    g = next((k for k, f in enumerate(original["factions"])
              if isinstance(f.get("territory"), dict) and f["territory"].get("ville_id")), None)
    if g is not None:
        v0 = original["factions"][g]["territory"]["ville_id"]
        autre = next(v["id"] for v in original["villes"] if v["id"] != v0)
        page.evaluate(f"() => openFactionForm({g})")
        verifier(page.input_value("#fsub_territory_ville_id") == v0, "territory.ville_id mal affiché")
        tf.ecrire_champ(page, "#fsub_territory_ville_id", autre)
        verifier(not tf.enregistrer(page), "faction (ville_id) refusée")
        seulement(page, original, [f"factions[{g}].territory.ville_id"], "territory.ville_id")
        page.evaluate(f"() => openFactionForm({g})")
        tf.ecrire_champ(page, "#fsub_territory_ville_id", v0)
        tf.enregistrer(page)
        identique(page, original, "territory.ville_id remis")
    # Faction sans sous-objets (null) : un champ rempli crée l'objet, vidé le remet à null.
    j = next(k for k, f in enumerate(original["factions"]) if f.get("politics") is None)
    page.evaluate(f"() => openFactionForm({j})")
    tf.ecrire_champ(page, "#fsub_politics_relations", "Relations de test")
    tf.enregistrer(page)
    verifier(page.evaluate(f"() => JSON.stringify(db.factions[{j}].politics)") == json.dumps({"relations": "Relations de test"}, ensure_ascii=False, separators=(",", ":")),
             "politics créé avec des clés vides")
    tf.recharger(page, tf.DONNEES)

    # Race : Hwakhari (race_huacani) : classe ajoutée puis retirée, note d'affiliation.
    r = idx(original, "races", "race_huacani")
    page.evaluate(f"() => openRaceForm({r})")
    page.select_option("#raceClassSel", original["classes"][0]["id"])
    page.click("button[onclick='addRaceClass()']")
    page.fill(".affil-notes", "\n".join(original["races"][r]["faction_affiliations"][0]["notes"] + ["note de test"]))
    page.dispatch_event(".affil-notes", "input")
    verifier(not tf.enregistrer(page), "race refusée")
    seulement(page, original, [f"races[{r}].class_access", f"races[{r}].faction_affiliations[0].notes"], "race")
    page.evaluate(f"() => openRaceForm({r})")
    page.click("#raceClassList .note-del-btn")
    page.fill(".affil-notes", "\n".join(original["races"][r]["faction_affiliations"][0]["notes"]))
    page.dispatch_event(".affil-notes", "input")
    tf.enregistrer(page)
    # Ophidiens : champs couverts (aucun champ de race hors formulaire).
    hors = {k for x in original["races"] for k in x} - {"id", "name", "playable", "npc", "companion", "genders", "origins",
                                                       "faction_affiliations", "abilities_traits", "class_access", "notes"}
    verifier(not hors, f"race : champs sans formulaire {hors}")

    # PNJ : description (char_lucifer_prince_tenebres, hiérarchie des Enfers).
    p = idx(original, "persons", "char_lucifer_prince_tenebres")
    page.evaluate(f"() => openPersonForm({p})")
    verifier(page.input_value("#pers_descr") == original["persons"][p]["description"], "description du PNJ non affichée")
    tf.ecrire_champ(page, "#pers_descr", original["persons"][p]["description"] + " (test)")
    verifier(not tf.enregistrer(page), "PNJ refusé")
    seulement(page, original, [f"persons[{p}].description"], "description du PNJ")
    page.evaluate(f"() => openPersonForm({p})")
    tf.ecrire_champ(page, "#pers_descr", original["persons"][p]["description"])
    tf.enregistrer(page)
    hors = {k for x in original["persons"] for k in x} - set(page.evaluate("""() => { openPersonForm(0);
        const ids = ['id','name','importance','alive','tomes','faction_id','race_id','class_id','role','visual','personality',
                     'description','arc','abilities','special_items','special_rule','quote','source','ville_ids','zone_ids','dialogue_ids','notes'];
        closeModal(); return ids; }"""))
    verifier(not hors, f"PNJ : champs sans formulaire {hors}")
    tf.recharger(page, tf.DONNEES)
    identique(page, original, "après les formulaires")

    # Toutes les fiches enregistrées sans modification.
    total = 0
    for coll, ouvrir in (("regions", "openRegionForm(i)"), ("raids", "openRaidForm(i)"), ("quests", "openQuestForm(i)"),
                         ("factions", "openFactionForm(i)"), ("races", "openRaceForm(i)"), ("persons", "openPersonForm(i)")):
        n = len(original[coll])
        refus, sans = page.evaluate("([o, n]) => window.__saveAll(o, Array.from({length: n}, (_, i) => i))",
                                    [ouvrir.replace("(i)", "(i)"), n])
        verifier(not refus and not sans, f"{coll} : refus {refus[:3]} ; sans base {sans[:3]}")
        total += n
    identique(page, original, "enregistrement sans modification")
    print(f"   formulaires : faction (sous-objets, comptoir, game_design, JSON invalide refusé, objet null créé), "
          f"race (class_access, notes d'affiliation), PNJ (description) ; {total} fiches enregistrées sans modification : export inchangé")


# ── 6. Suppression ────────────────────────────────────────────────────────────

def suppression(page, original):
    """Boîte de suppression : nombre de références, Annuler sans effet ; deleteRule confirmé."""
    lignes = []
    for coll, rid, fn in (("regions", "terres_des_gigantomorphes", "deleteRegion"), ("villes", "ville_moork", "deleteVille"),
                          ("races", "race_huacani", "deleteRace"), ("quests", "quest_signe_tenebres", "deleteQuest"),
                          ("server_commands", None, "deleteServerCommand")):
        i = idx(original, coll, rid) if rid else 0
        rid = original[coll][i]["id"]
        att = references(original, coll, rid)
        page.evaluate(f"() => {fn}({i})")
        page.wait_for_selector("#confirmYes")
        tot = int(page.get_attribute("#confirmRefs", "data-total"))
        txt = page.inner_text("#confirmRefs")
        verifier(tot == att, f"{coll} {rid} : {tot} références affichées, {att} attendues")
        verifier((str(att) in txt) if att else ("Aucune référence" in txt), f"{coll} {rid} : texte {txt[:80]!r}")
        page.click("#confirmNo")
        verifier(not page.query_selector("#confirmYes"), "boîte non fermée")
        lignes.append(f"{rid} {att}")
    identique(page, original, "suppressions annulées")
    # Suppression confirmée : la fiche disparaît (puis rechargement).
    i = idx(original, "races", "race_huacani")
    page.evaluate(f"() => deleteRace({i})")
    page.click("#confirmYes")
    verifier(page.evaluate("() => !db.races.some(r => r.id === 'race_huacani')"), "race non supprimée")
    # deleteRule : confirmation demandée (avant v1.0.100 : immédiate).
    cat = page.evaluate("() => Object.keys(db.cross_rules || {}).find(k => Array.isArray(db.cross_rules[k]) && db.cross_rules[k].length)")
    if cat:
        n0 = page.evaluate(f"() => db.cross_rules['{cat}'].length")
        page.evaluate(f"() => deleteRule('{cat}', 0)")
        page.wait_for_selector("#confirmYes")
        page.click("#confirmNo")
        verifier(page.evaluate(f"() => db.cross_rules['{cat}'].length") == n0, "deleteRule sans confirmation")
    tf.recharger(page, tf.DONNEES)
    identique(page, original, "rechargement après suppression")
    print(f"   suppression : références affichées ({', '.join(lignes)}), Annuler sans effet, deleteRule confirmé")


def main() -> int:
    """Lance le scénario (voir le docstring) ; code de sortie 0 si tout passe."""
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--editeur", type=Path, default=rt.EDITEUR)
    ap.add_argument("--donnees", type=Path, default=rt.DONNEES)
    ap.add_argument("--captures", type=Path, default=None, help="chemin sans extension des captures PNG")
    args = ap.parse_args()
    from playwright.sync_api import sync_playwright

    tf.DONNEES = args.donnees
    texte_original = args.donnees.read_text(encoding="utf-8")
    original = json.loads(texte_original)
    ok = True
    try:
        print("   données : " + donnees(original))
    except Echec as e:
        print(f"[ÉCHEC] données : {e}")
        ok = False
    with sync_playwright() as p:
        nav = p.chromium.launch()
        page, erreurs = rt.ouvrir_editeur(nav, args.editeur, taille=(1500, 1000))
        rt.charger(page, args.donnees)
        page.evaluate(tf.JS_OUTILS)
        for nom, fn in (("niveaux (1-2)", lambda: niveaux(page, original, args.captures)),
                        ("étapes de quête (3)", lambda: quetes(page, original, args.captures)),
                        ("mines (4)", lambda: mines(page, original)),
                        ("formulaires (5)", lambda: formulaires(page, original)),
                        ("suppression (6)", lambda: suppression(page, original))):
            try:
                fn()
                print(f"[OK] {nom}")
            except Echec as e:
                ok = False
                print(f"[ÉCHEC] {nom} : {e}")
                tf.recharger(page, tf.DONNEES)
        texte = page.evaluate("() => pyJsonStringify(buildExportSchema())")
        same = texte == texte_original
        print(f"[{'OK' if same else 'ÉCHEC'}] export final identique octet pour octet au fichier")
        ok &= same
        if erreurs:
            ok = False
            print(f"[ÉCHEC] {len(erreurs)} erreur(s) console :")
            for e in erreurs[:10]:
                print("   ", e)
        else:
            print("[OK] aucune erreur console")
        if args.captures:
            print(f"   captures : {args.captures.with_suffix('.png')}, {args.captures.with_name(args.captures.stem + '_region.png')}")
        nav.close()
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
