#!/usr/bin/env python3
"""Test exhaustif de la sauvegarde de l'éditeur de lore (v1.0.98), avec Playwright.

Charge ``lune-noire-data.json`` dans ``lune-noire-editor.html`` (Chromium sans
affichage) et vérifie chaque chemin d'écriture :

1. **format** : l'export sans modification est identique octet pour octet au fichier
   (``json.dumps(indent=2, ensure_ascii=False)``, UTF-8, sans retour à la ligne final) ;
2. **enregistrer sans modifier** : pour chaque formulaire, TOUTES les fiches de la
   collection (cellules : un échantillon) sont ouvertes et enregistrées telles
   quelles ; ``db`` et l'export doivent rester identiques à l'original ; la base du
   formulaire (capture à l'ouverture) doit exister pour chaque fiche ;
3. **un champ modifié** : sur 3 fiches par formulaire (la plus riche, la plus pauvre
   en champs, la première), un champ simple est modifié : seul ce chemin change dans
   l'export ; remis à sa valeur, l'export redevient identique ;
4. **création** : une fiche par formulaire ; seule la longueur de la collection
   change ; structure comparée aux fiches existantes (champs manquants, champs vides
   ajoutés) ; puis suppression par l'éditeur : export identique à l'original ;
5. **suppression** : une fiche par collection (la plus citée) : seule elle
   disparaît ; les références devenues orphelines sont signalées (l'éditeur ne les
   gère pas) ;
6. **séquence mixte** : Tracer (création d'un sentier), Déplacer (Brenhaval), édition
   de la ville déplacée par son formulaire, cellule modifiée, deux Ctrl+Z : l'export ne
   diffère de l'original que par le nom de Brenhaval et la case modifiée ; ce fichier
   passe ``validate_city_services.py``, ``generate_city_services.py --check`` et
   ``sync_positions.py --check`` ;
7. **import de villes** (mise à jour partielle) : seuls les champs importés changent ;
8. aucune erreur console.

Lancement (depuis la racine du dépôt, Playwright installé comme pour
``test_editor_roundtrip.py``) ::

    python3 tools/lore/test_editor_forms.py                 # ~4 min
    python3 tools/lore/test_editor_forms.py --rapide        # 3 fiches par formulaire au lieu de toutes
    python3 tools/lore/test_editor_forms.py --editeur legacy/world-builder-html/lune-noire-editor-v20.html

Code de sortie 0 si tout passe, 1 sinon. Le fichier de données n'est jamais modifié.
"""

from __future__ import annotations

import argparse
import collections
import json
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import test_editor_roundtrip as rt  # noqa: E402
import test_editor_routes as tr  # noqa: E402

# Formulaires à fiche : (collection, ouverture de la fiche i, ouverture en création,
# (champ id, champ nom) du formulaire de création, suppression de la fiche i).
FICHES = [
    ("factions", "openFactionForm(i)", "openFactionForm()", ("f_id", "f_name"), "deleteFaction(i)"),
    ("classes", "openClassForm(i)", "openClassForm()", ("item_id", "item_name"), "deleteClass(i)"),
    ("companions", "openCompanionForm(i)", "openCompanionForm()", ("item_id", "item_name"), "deleteCompanion(i)"),
    ("races", "openRaceForm(i)", "openRaceForm()", ("r_id", "r_name"), "deleteRace(i)"),
    ("lieux", "openLieuForm(i)", "openLieuForm()", ("l_id", "l_name"), "deleteLieu(i)"),
    ("villes", "openVilleForm(i)", "openVilleForm()", ("v_id", "v_name"), "deleteVille(i)"),
    ("exploits", "openExploitForm(i)", "openExploitForm()", ("ex_id", "ex_name"), "deleteExploit(i)"),
    ("quests", "openQuestForm(i)", "openQuestForm()", ("qt_id", "qt_name"), "deleteQuest(i)"),
    ("mounts", "openMountForm(i)", "openMountForm()", ("mt_id", "mt_name"), "deleteMount(i)"),
    ("raids", "openRaidForm(i)", "openRaidForm()", ("rd_id", "rd_name"), "deleteRaid(i)"),
    ("regions", "openRegionForm(i)", "openRegionForm()", ("rg_id", "rg_name"), "deleteRegion(i)"),
    ("weapons", "openWeaponForm(i)", "openWeaponForm()", ("wp_id", "wp_name"), "deleteWeapon(i)"),
    ("server_commands", "openServerCommandForm(i)", "openServerCommandForm()", ("cmd_id", "cmd_name"), "deleteServerCommand(i)"),
    ("loots", "openLootForm(i)", "openLootForm()", ("lt_id", "lt_name"), "deleteLoot(i)"),
    ("dialogues", "openDialogueForm(i)", "openDialogueForm()", ("dl_id", "dl_name"), "deleteDialogue(i)"),
    ("creatures", "openCreatureForm(i)", "openCreatureForm()", ("cr_id", "cr_name"), "deleteCreature(i)"),
    ("timeline", "openTimelineForm(i)", "openTimelineForm()", ("tl_id", "tl_name"), "deleteTimelineEvent(i)"),
    ("persons", "openPersonForm(i)", "openPersonForm()", ("pers_id", "pers_name"), "deletePerson(i)"),
    ("zones", "openZoneForm(i)", "openZoneForm()", ("z_id", "z_name"), "deleteZone(i)"),
    ("competences", "openCompetenceForm(i)", "openCompetenceForm()", ("comp_id", "comp_name"), "deleteCompetence(i)"),
    ("regles_transverses", "openRegleForm(i)", "openRegleForm()", ("regle_id", "regle_name"), "deleteRegle(i)"),
]

DONNEES = rt.DONNEES  # fichier chargé (remplacé par --donnees)

JS_OUTILS = """() => {
  window.__toasts = [];
  if (!window.__toastOrig) window.__toastOrig = window.toast;
  const t = window.__toastOrig;
  window.toast = function(m) { window.__toasts.push(String(m)); return t(m); };
  // Export découpé par clé de premier niveau, au format du fichier (pyJsonStringify).
  window.__parts = () => { const e = buildExportSchema(), o = {}; const ks = _objKeys(e);
    ks.forEach(k => { o[k] = pyJsonStringify(e[k]); }); o.__keys = ks.join(','); return o; };
  window.__base = window.__parts();
  // Clés de premier niveau de l'export qui diffèrent de l'export de référence.
  window.__diffParts = () => { const p = window.__parts(), out = {};
    Object.keys(p).forEach(k => { if (p[k] !== window.__base[k]) out[k] = p[k]; });
    Object.keys(window.__base).forEach(k => { if (!(k in p)) out[k] = null; }); return out; };
  // Ouvre puis enregistre sans rien toucher ; renvoie [refus, sans capture].
  window.__saveAll = (ouvrir, indices) => { const refus = [], sansBase = [];
    for (const i of indices) {
      window.__toasts = []; eval(ouvrir);
      if (!window._formBase) sansBase.push(i);
      const b = document.querySelector('#modalFooter button[onclick^="save"]');
      if (!b) { refus.push(i + ' : pas de bouton Enregistrer'); closeModal(); continue; }
      b.click();
      if (!document.getElementById('modalOverlay').classList.contains('hidden')) { refus.push(i + ' : ' + window.__toasts.join(' | ')); closeModal(); }
    }
    return [refus, sansBase]; };
}"""


class Rapport:
    """Accumule les lignes du tableau final et l'état global."""

    # Rapport vide, état global « tout passe ».
    def __init__(self):
        self.ok = True
        self.lignes: list[tuple[str, str, str]] = []

    def note(self, formulaire, fiches, resultat, echec=False):
        """Ajoute une ligne ; ``echec`` marque le test en échec."""
        self.lignes.append((formulaire, fiches, resultat))
        if echec:
            self.ok = False
        print(f"[{'ÉCHEC' if echec else 'OK'}] {formulaire} — {fiches} — {resultat}", flush=True)


def diffs_export(page, original):
    """Différences (chemins diff_profond) entre l'export courant et l'original."""
    parts = page.evaluate("() => window.__diffParts()")
    out = []
    for k, txt in parts.items():
        if k == "__keys":
            out.append(f"ordre/clés de premier niveau : {txt[:200]}")
            continue
        if txt is None:
            out.append(f"{k} : supprimé")
            continue
        out += rt.diff_profond(original.get(k), json.loads(txt), k, limite=200)
    return out


def taille(x):
    """Nombre de valeurs feuilles d'une valeur JSON (richesse d'une fiche)."""
    if isinstance(x, dict):
        return sum(taille(v) for v in x.values()) + len(x)
    if isinstance(x, list):
        return sum(taille(v) for v in x) + 1
    return 1


def trois_fiches(coll):
    """Indices de la fiche la plus riche, de la plus pauvre et de la première."""
    if not coll:
        return []
    idx = sorted(range(len(coll)), key=lambda i: taille(coll[i]))
    return list(dict.fromkeys([idx[-1], idx[0], 0]))


def recharger(page, donnees):
    """Recharge le fichier (état d'origine) et réinstalle les outils de test."""
    page.evaluate("""() => { closeModal();
        document.querySelectorAll('#confirmYes').forEach(b => { let e = b;
          while (e.parentElement && e.parentElement !== document.body) e = e.parentElement; e.remove(); });
        if (window._carteState) window._carteState.undoStack = [];
        window.__ancienDb = db; }""")
    page.set_input_files("#fileInput", str(donnees))
    page.wait_for_function("() => window.db && db !== window.__ancienDb && _exportBase", timeout=120_000)
    page.evaluate(JS_OUTILS)


def ecrire_champ(page, selecteur, valeur):
    """Écrit ``valeur`` dans un champ texte et déclenche input / change."""
    page.evaluate("""([s, v]) => { const e = document.querySelector(s); e.value = v;
        e.dispatchEvent(new Event('input', {bubbles: true})); e.dispatchEvent(new Event('change', {bubbles: true})); }""",
                  [selecteur, valeur])


def enregistrer(page):
    """Clique « Enregistrer » du formulaire ouvert ; renvoie le dernier message si refusé."""
    page.evaluate("() => { window.__toasts = []; document.querySelector('#modalFooter button[onclick^=\"save\"]').click(); }")
    if page.evaluate("() => !document.getElementById('modalOverlay').classList.contains('hidden')"):
        msg = page.evaluate("() => window.__toasts.join(' | ')")
        page.evaluate("() => closeModal()")
        return msg or "refus"
    return None


# ── 2. Enregistrer sans modifier ──────────────────────────────────────────────

def test_sans_modification(page, original, rap, rapide):
    """Toutes les fiches de chaque formulaire enregistrées telles quelles."""
    db0 = page.evaluate("() => JSON.stringify(db)")
    for coll, ouvrir, *_ in FICHES:
        n = len(original[coll])
        indices = trois_fiches(original[coll]) if rapide else list(range(n))
        refus, sans_base = page.evaluate("([o, ix]) => window.__saveAll(o, ix)", [ouvrir, indices])
        d = diffs_export(page, original)
        db_change = page.evaluate("s => JSON.stringify(db) !== s", db0)
        res = "export identique" if not d else f"{len(d)} différence(s) : {d[:3]}"
        if refus:
            res += f" ; {len(refus)} refus (nom vide dans les données, ex. {refus[0]})"
        if sans_base:
            res += f" ; {len(sans_base)} fiche(s) sans capture"
        if db_change:
            res += " ; db modifié"
        rap.note(f"{coll} : enregistrer sans modifier", f"{len(indices)}/{n}", res, bool(d or sans_base or db_change))
        if d or db_change:
            db0 = page.evaluate("() => JSON.stringify(db)")

    # Formulaires sans fiche indexée.
    autres = [
        ("skill_trees", "openSkillTreeEditor(db.skill_trees[i].class_id)", len(original["skill_trees"])),
        ("place_types", "openPlaceTypesEditor()", 1),
        ("lore", "openLoreEditor()", 1),
    ]
    for cat, regles in original["cross_rules"].items():
        if cat in ("mounts_and_mobility", "war_and_access", "magic_interactions") and regles:
            autres.append((f"cross_rules.{cat}", f"openRuleForm('{cat}', i)", len(regles)))
    cellules = page.evaluate("""() => { const v = new Set((db.villes||[]).map(x => x.cell));
        return db.cells.map((c, i) => [i, c]).filter(([i, c]) => i % 25 === 0 || v.has(c.id) || (c.points_of_interest||[]).length
          || c.province || (c.region_id && !(db.regions||[]).some(r => r.id === c.region_id))).map(([i]) => i); }""")
    if rapide:
        cellules = cellules[:20]
    for nom, ouvrir, n in autres:
        refus, sans_base = page.evaluate("([o, ix]) => window.__saveAll(o, ix)", [ouvrir, list(range(n))])
        d = diffs_export(page, original)
        rap.note(f"{nom} : enregistrer sans modifier", f"{n}/{n}",
                 "export identique" if not d else f"{len(d)} différence(s) : {d[:3]}",
                 bool(d or refus or sans_base))
    refus, sans_base = page.evaluate("([o, ix]) => window.__saveAll(o, ix)", ["openCellEditor(db.cells[i].id)", cellules])
    d = diffs_export(page, original)
    rap.note("cells (éditeur de cellule) : enregistrer sans modifier", f"{len(cellules)}/{len(original['cells'])}",
             "export identique" if not d else f"{len(d)} différence(s) : {d[:3]}", bool(d or refus or sans_base))
    # Métadonnées (section, pas de modale).
    page.evaluate("() => { showSection('meta'); window.__metaBase = !!window._formBase; saveMeta(); saveMeta(); }")
    d = diffs_export(page, original)
    rap.note("meta : enregistrer sans modifier", "1/1", "export identique (canon gardé)" if not d else f"{d[:3]}",
             bool(d) or not page.evaluate("() => window.__metaBase"))


# ── 3. Un champ modifié ───────────────────────────────────────────────────────

def un_champ(page, original, rap, nom, chemin_attendu, modifier, restaurer, n_fiches):
    """Modifie un champ, vérifie que seul ``chemin_attendu`` change, puis le remet."""
    modifier()
    d = diffs_export(page, original)
    ok1 = len(d) == 1 and d[0].split(" : ")[0] == chemin_attendu
    restaurer()
    d2 = diffs_export(page, original)
    res = (f"seul {chemin_attendu} change ; remis : identique" if ok1 and not d2
           else f"modif : {d[:3]} ; remise : {d2[:3]}")
    rap.note(f"{nom} : un champ modifié", n_fiches, res, not (ok1 and not d2))


def test_un_champ(page, original, rap):
    """Nom (ou champ simple) modifié sur 3 fiches de chaque formulaire."""
    for coll, ouvrir, _, (_, champ_nom), _ in FICHES:
        res_ok, details = True, []
        for i in trois_fiches(original[coll]):
            val = original[coll][i]["name"]
            js = ouvrir.replace("(i)", f"({i})")
            page.evaluate(f"() => {js}")
            ecrire_champ(page, f"#{champ_nom}", val + " (test)")
            refus = enregistrer(page)
            d = diffs_export(page, original)
            attendu = f"{coll}[{i}].name"
            if refus or len(d) != 1 or d[0].split(" : ")[0] != attendu:
                res_ok = False
                details.append(f"fiche {i} : {refus or d[:4]}")
                recharger(page, DONNEES)
                continue
            page.evaluate(f"() => {js}")
            ecrire_champ(page, f"#{champ_nom}", val)
            refus = enregistrer(page)
            d = diffs_export(page, original)
            if val == "":
                # Nom vide dans les données : l'éditeur refuse d'enregistrer sans nom.
                if d:
                    recharger(page, DONNEES)
                continue
            if refus or d:
                res_ok = False
                details.append(f"fiche {i} remise : {refus or d[:4]}")
                recharger(page, DONNEES)
        ix = trois_fiches(original[coll])
        rap.note(f"{coll} : nom modifié", f"{len(ix)} fiche(s) {ix}",
                 "seul name change ; remis : identique" if res_ok else "; ".join(details)[:400], not res_ok)

    # Arbre de compétences : nom de la première branche.
    i = next(k for k, t in enumerate(original["skill_trees"]) if t.get("branches"))
    b0 = original["skill_trees"][i]["branches"][0]["name"]

    # Écrit v comme nom de la première branche de l'arbre i, par sa modale, puis enregistre.
    def arbre(v):
        page.evaluate(f"() => openSkillTreeEditor(db.skill_trees[{i}].class_id)")
        ecrire_champ(page, "#st_editor_body input[onchange*='branches[0].name']", v)
        enregistrer(page)
    un_champ(page, original, rap, "skill_trees", f"skill_trees[{i}].branches[0].name",
             lambda: arbre(b0 + " (test)"), lambda: arbre(b0), "1 arbre")

    # Types de lieux : libellé du premier type.
    l0 = original["place_types"][0]["label"]

    # Écrit v comme libellé du premier type de lieu, par l'éditeur des types, puis enregistre.
    def types(v):
        page.evaluate("() => openPlaceTypesEditor()")
        ecrire_champ(page, "#ptList > div:first-child input.form-input", v)
        enregistrer(page)
    un_champ(page, original, rap, "place_types", "place_types[0].label", lambda: types(l0 + " (test)"),
             lambda: types(l0), "1 type")

    # Règle transverse (cross_rules) : texte de la règle.
    cat = next(c for c in ("mounts_and_mobility", "war_and_access", "magic_interactions") if original["cross_rules"].get(c))
    r0 = original["cross_rules"][cat][0]["rule"]

    # Écrit v comme texte de la première règle de la catégorie cat, par son formulaire, puis enregistre.
    def regle(v):
        page.evaluate(f"() => openRuleForm('{cat}', 0)")
        ecrire_champ(page, "#rule_text", v)
        enregistrer(page)
    un_champ(page, original, rap, f"cross_rules.{cat}", f"cross_rules.{cat}[0].rule", lambda: regle(r0 + " (test)"),
             lambda: regle(r0), "1 règle")

    # Lore : ère.
    era = original["lore"]["era"]

    # Écrit v comme ère du lore, par l'éditeur du lore, puis enregistre.
    def lore(v):
        page.evaluate("() => openLoreEditor()")
        ecrire_champ(page, "#lore_era", v)
        enregistrer(page)
    un_champ(page, original, rap, "lore", "lore.era", lambda: lore(era + " (test)"), lambda: lore(era), "1")

    # Métadonnées : source.
    src = original["meta"]["source"]

    # Écrit v comme source des métadonnées, puis enregistre (saveMeta).
    def meta(v):
        page.evaluate("() => showSection('meta')")
        ecrire_champ(page, "#meta_source", v)
        page.evaluate("() => saveMeta()")
    un_champ(page, original, rap, "meta", "meta.source", lambda: meta(src + " (test)"), lambda: meta(src), "1")

    # Cellule : case à cocher « Rivière » d'une cellule avec ville.
    ci = page.evaluate("() => { const v = new Set(db.villes.map(x => x.cell)); return db.cells.findIndex(c => v.has(c.id)); }")

    # Inverse la case « Rivière » de la cellule ci par son éditeur, puis enregistre.
    def riviere():
        page.evaluate(f"() => {{ openCellEditor(db.cells[{ci}].id); const c = document.getElementById('cell_river'); c.checked = !c.checked; }}")
        enregistrer(page)
    un_champ(page, original, rap, "cells", f"cells[{ci}].has_river", riviere, riviere, "1 cellule")

    # Exploit : région choisie (champ du formulaire absent des fiches du lore).
    rg = original["regions"][0]["id"]

    # Choisit la région v (vide = aucune) dans le formulaire du premier exploit, puis enregistre.
    def region(v):
        page.evaluate("() => openExploitForm(0)")
        page.select_option("#ex_region", v)
        enregistrer(page)
    un_champ(page, original, rap, "exploits (région)", "exploits[0].region_id", lambda: region(rg), lambda: region(""), "1 exploit")

    # Tomes : couleur (champ absent de la normalisation de l'export avant v1.0.98).
    c0 = original["tomes_config"][0].get("color")
    un_champ(page, original, rap, "tomes_config (couleur)", "tomes_config[0].color",
             lambda: page.evaluate("() => updateTomeColor(0, '#123456')"),
             lambda: page.evaluate(f"() => updateTomeColor(0, {json.dumps(c0)})") if c0 is not None
             else page.evaluate("() => { delete db.tomes_config[0].color; }"), "1 tome")

    # Tomes (édition en ligne du libellé).
    t0 = original["tomes_config"][0]["label"]
    un_champ(page, original, rap, "tomes_config", "tomes_config[0].label",
             lambda: page.evaluate(f"() => updateTomeLabel(0, {json.dumps(t0 + ' (test)')})"),
             lambda: page.evaluate(f"() => updateTomeLabel(0, {json.dumps(t0)})"), "1 tome")


# ── 4. Création, 5. suppression ───────────────────────────────────────────────

def structure(nouvelle, existantes):
    """Champs manquants (présents dans toutes les fiches existantes) et champs vides ajoutés
    (absents de toutes les fiches existantes)."""
    n = len(existantes)
    cpt = collections.Counter(k for e in existantes for k in e)
    manquants = sorted(k for k, c in cpt.items() if c == n and k not in nouvelle)
    vides = sorted(k for k, v in nouvelle.items() if k not in cpt and v in ("", None, [], {}))
    return manquants, vides


def test_creation(page, original, rap):
    """Une fiche créée par formulaire, puis supprimée par l'éditeur."""
    for coll, _, creer, (cid, cnom), supprimer in FICHES:
        page.evaluate(f"() => {creer}")
        ecrire_champ(page, f"#{cid}", f"test_v1098_{coll}")
        ecrire_champ(page, f"#{cnom}", f"Test v1.0.98 {coll}")
        refus = enregistrer(page)
        d = diffs_export(page, original)
        n = len(original[coll])
        attendu = [f"{coll} : longueur {n} → {n + 1}"]
        nouvelle = page.evaluate(f"() => {{ const a = buildExportSchema()['{coll}']; return a[a.length - 1]; }}")
        manquants, vides = structure(nouvelle, original[coll])
        ok = not refus and d == attendu and nouvelle.get("id") == f"test_v1098_{coll}"
        # Suppression par l'éditeur (boîte de confirmation HTML).
        page.evaluate(f"() => {{ const i = db['{coll}'].length - 1; {supprimer} }}")
        if page.query_selector("#confirmYes"):
            page.click("#confirmYes")
        d2 = diffs_export(page, original)
        res = "fiche ajoutée en fin de collection" if ok else f"refus={refus} diff={d[:3]}"
        res += "; supprimée : identique" if not d2 else f"; après suppression : {d2[:3]}"
        if manquants:
            res += f"; champs absents (présents partout ailleurs) : {', '.join(manquants)}"
        if vides:
            res += f"; champs vides nouveaux : {', '.join(vides)}"
        rap.note(f"{coll} : création", "1 nouvelle", res, not ok or bool(d2))
        if not ok or d2:
            recharger(page, DONNEES)


def references(original, coll, rid):
    """Chemins (hors de la fiche elle-même) dont la valeur chaîne vaut ``rid``."""
    out = []

    # Parcours récursif : ajoute à out le chemin p de chaque valeur chaîne égale à rid.
    def rec(x, p):
        if isinstance(x, dict):
            for k, v in x.items():
                rec(v, f"{p}.{k}")
        elif isinstance(x, list):
            for i, v in enumerate(x):
                rec(v, f"{p}[{i}]")
        elif x == rid:
            out.append(p)
    for k, v in original.items():
        if k == coll:
            for i, f in enumerate(v):
                if f.get("id") != rid:
                    rec(f, f"{k}[{i}]")
        else:
            rec(v, k)
    return out


def test_suppression(page, original, rap):
    """Suppression d'une fiche par collection (la plus citée ailleurs)."""
    attendu = json.loads(json.dumps(original))
    for coll, _, _, _, supprimer in FICHES:
        refs = {i: references(original, coll, f["id"]) for i, f in enumerate(original[coll]) if f.get("id")}
        i = max(refs, key=lambda k: len(refs[k]))
        rid = original[coll][i]["id"]
        j = page.evaluate(f"id => db['{coll}'].findIndex(x => x.id === id)", rid)
        page.evaluate(f"() => {{ const i = {j}; {supprimer} }}")
        page.click("#confirmYes")
        attendu[coll] = [f for f in attendu[coll] if f.get("id") != rid]
        parts = page.evaluate(f"() => pyJsonStringify(buildExportSchema()['{coll}'])")
        d = rt.diff_profond(attendu[coll], json.loads(parts), coll)
        ex = ", ".join(refs[i][:3])
        rap.note(f"{coll} : suppression", f"1 ({rid})",
                 ("seule la fiche disparaît" if not d else f"{d[:3]}")
                 + (f" ; {len(refs[i])} référence(s) orpheline(s) non gérées (ex. {ex})" if refs[i] else ""), bool(d))
    # Toutes les autres collections inchangées.
    d = [x for x in diffs_export(page, original) if not x.split("[")[0].split(" ")[0] in {c for c, *_ in FICHES}]
    rap.note("suppressions : autres collections", "-", "inchangées" if not d else f"{d[:3]}", bool(d))


# ── 6. Séquence mixte, 7. import ──────────────────────────────────────────────

def test_sequence_mixte(page, original, rap, dossier: Path):
    """Tracer + Déplacer + formulaires + annulations, puis export validé par les outils du lore."""
    page.evaluate(f"() => {{ showSection('carte'); window._carteState.zoom = {tr.ZOOM}; drawCarte(); }}")
    nb0 = len(original["routes"])
    # Tracer : sentier Thrival → Lhantarn.
    page.keyboard.press("r")
    page.select_option("#carteTraceType", "path")
    tr.clic(page, tr.point(page, fiche=("villes", "town_truceholm")))
    tr.clic(page, tr.point(page, "N020_W011"))
    tr.clic(page, tr.point(page, fiche=("villes", "town_lanternhold")))
    cree = page.evaluate("() => db.routes.length") == nb0 + 1
    # Déplacer : Brenhaval d'une case vers l'ouest.
    page.keyboard.press("Escape")
    page.keyboard.press("d")
    tr.glisser(page, tr.point(page, fiche=("villes", "town_brinehaven")), tr.point(page, "N017_W011", fy=0.3))
    iv = page.evaluate("() => db.villes.findIndex(x => x.id === 'town_brinehaven')")
    deplace = page.evaluate(f"() => db.villes[{iv}].cell") == "N017_W011"
    # Formulaire : la ville déplacée est renommée (base prise après le déplacement).
    nom = original["villes"][iv]["name"] + " (renommée)"
    page.evaluate(f"() => openVilleForm({iv})")
    ecrire_champ(page, "#v_name", nom)
    enregistrer(page)
    # Cellule : rivière basculée sur une case sans ville.
    ci = page.evaluate("() => { const v = new Set(db.villes.map(x => x.cell)); return db.cells.findIndex(c => !v.has(c.id) && !c.has_river); }")
    page.evaluate(f"() => {{ openCellEditor(db.cells[{ci}].id); document.getElementById('cell_river').checked = true; }}")
    enregistrer(page)
    # Deux Ctrl+Z (déplacement, puis création du sentier) depuis la carte.
    page.evaluate("() => showSection('carte')")
    page.keyboard.press("Control+z")
    page.keyboard.press("Control+z")
    d = diffs_export(page, original)
    attendu = sorted([f"villes[{iv}].name", f"cells[{ci}].has_river"])
    ok = cree and deplace and sorted(x.split(" : ")[0] for x in d) == attendu
    rap.note("séquence mixte (Tracer, Déplacer, formulaires, 2 Ctrl+Z)", "routes, Brenhaval, 1 cellule",
             "seuls le nom (gardé après Ctrl+Z) et la cellule changent" if ok
             else f"créée={cree} déplacée={deplace} diff={d[:5]}", not ok)
    # Fichier exporté (vrai téléchargement) : format et outils du lore.
    _, texte = rt.exporter(page, dossier, "mixte.json")
    brut = (dossier / "mixte.json").read_bytes()
    fmt = brut == json.dumps(json.loads(texte), indent=2, ensure_ascii=False).encode("utf-8")
    rap.note("format du fichier après la séquence", "-", "UTF-8, json.dumps(indent=2, ensure_ascii=False), sans \\n final"
             if fmt else "format différent de json.dumps", not fmt)
    rap_outils(brut, rap)


def rap_outils(brut: bytes, rap):
    """Lance les trois contrôles du lore sur une copie du dépôt où le fichier exporté
    remplace les deux JSON du lore (lune-noire-data.json et -v20.json)."""
    with tempfile.TemporaryDirectory() as t:
        t = Path(t)
        (t / "tools/lore").mkdir(parents=True)
        for f in ("validate_city_services.py", "generate_city_services.py", "sync_positions.py"):
            shutil.copy(rt.RACINE / "tools/lore" / f, t / "tools/lore" / f)
        lore = t / "legacy/world-builder-html"
        lore.mkdir(parents=True)
        for f in ("lune-noire-data.json", "lune-noire-data-v20.json"):
            (lore / f).write_bytes(brut)
        m = t / "game/data/world/royaumes"
        m.mkdir(parents=True)
        shutil.copy(rt.RACINE / "game/data/world/royaumes/world_master.json", m / "world_master.json")
        for cmd in (["validate_city_services.py"], ["generate_city_services.py", "--check"], ["sync_positions.py", "--check"]):
            p = subprocess.run([sys.executable, str(t / "tools/lore" / cmd[0]), *cmd[1:]], capture_output=True, text=True)
            der = (p.stdout.strip().splitlines() or [""])[-1]
            rap.note(f"outil {' '.join(cmd)}", "export de la séquence", der[:120], p.returncode != 0)


def test_import_villes(page, original, rap):
    """Import JSON d'une mise à jour partielle de ville : seuls les champs fournis changent."""
    iv = next(i for i, v in enumerate(original["villes"]) if v.get("port") and v.get("services"))
    v = original["villes"][iv]
    page.evaluate("() => openVilleImport()")
    page.fill("#importJsonArea", json.dumps({"id": v["id"], "name": v["name"] + " (import)"}, ensure_ascii=False))
    page.evaluate("() => { previewVilleImport(); confirmVilleImport(); }")
    d = diffs_export(page, original)
    ok = [x.split(" : ")[0] for x in d] == [f"villes[{iv}].name"]
    rap.note("import de villes (mise à jour partielle)", f"1 ({v['id']})",
             "seul le nom change (services, port, case gardés)" if ok else f"{d[:4]}", not ok)


def main() -> int:
    """Lance tous les tests et affiche le tableau ; code de sortie 0 si tout passe."""
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--editeur", type=Path, default=rt.EDITEUR)
    ap.add_argument("--donnees", type=Path, default=rt.DONNEES)
    ap.add_argument("--rapide", action="store_true", help="3 fiches par formulaire au lieu de toutes")
    args = ap.parse_args()
    global DONNEES
    DONNEES = args.donnees
    from playwright.sync_api import sync_playwright

    brut0 = args.donnees.read_bytes()
    original = json.loads(brut0.decode("utf-8"))
    rap = Rapport()
    with sync_playwright() as p, tempfile.TemporaryDirectory() as tmp:
        nav = p.chromium.launch()
        page, erreurs = rt.ouvrir_editeur(nav, args.editeur, taille=(1500, 1000))
        rt.charger(page, args.donnees)
        page.evaluate(JS_OUTILS)
        # 1. Format : export sans modification identique octet pour octet.
        rt.exporter(page, Path(tmp), "brut.json")
        same = (Path(tmp) / "brut.json").read_bytes() == brut0
        rap.note("format : export sans modification", "fichier entier",
                 "identique octet pour octet au fichier" if same else "octets différents", not same)
        test_sans_modification(page, original, rap, args.rapide)
        recharger(page, args.donnees)
        test_un_champ(page, original, rap)
        recharger(page, args.donnees)
        test_creation(page, original, rap)
        recharger(page, args.donnees)
        test_suppression(page, original, rap)
        recharger(page, args.donnees)
        test_sequence_mixte(page, original, rap, Path(tmp))
        recharger(page, args.donnees)
        test_import_villes(page, original, rap)
        if erreurs:
            rap.note("console", "-", f"{len(erreurs)} erreur(s) : {erreurs[:3]}", True)
        else:
            rap.note("console", "-", "aucune erreur")
        nav.close()
    print(f"\n{'TOUT PASSE' if rap.ok else 'ÉCHEC'} — {len(rap.lignes)} vérifications")
    return 0 if rap.ok else 1


if __name__ == "__main__":
    sys.exit(main())
