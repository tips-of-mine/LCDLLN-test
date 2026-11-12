#!/usr/bin/env python3
"""Test d'aller-retour de l'éditeur de lore (v1.0.96).

Charge ``lune-noire-data.json`` dans l'éditeur HTML (``lune-noire-editor.html``)
avec Playwright (Chromium sans affichage), exporte et compare l'export au fichier
d'origine en JSON profond :

1. chargement : aucun type changé ni champ retiré dans ``db`` (des collections
   absentes peuvent être ajoutées vides, ce qui n'est pas une différence) ;
2. export sans aucune modification : différence vide attendue ;
3. nom d'une ville modifié par le formulaire, puis export : seule la valeur
   ``villes[i].name`` change.

L'export de l'éditeur n'ajoute aucun champ d'horodatage (``meta`` est restitué tel
quel) : aucune clé n'est ignorée par la comparaison. Seule tolérance : un nombre
flottant entier (``1.0`` en Python) et l'entier correspondant (``1``) sont égaux,
JavaScript ne distinguant pas les deux (l'export écrit ``1``).

Lancement (depuis la racine du dépôt) ::

    pip install playwright && python3 -m playwright install chromium   # une fois
    python3 tools/lore/test_editor_roundtrip.py
    python3 tools/lore/test_editor_roundtrip.py --editeur legacy/world-builder-html/lune-noire-editor-v20.html

Code de sortie 0 si tout passe, 1 sinon. Le fichier de données n'est jamais
modifié (l'export est écrit dans un dossier temporaire).

Le module est réutilisé par ``test_editor_routes.py`` (v1.0.97) : fonctions
``ouvrir_editeur``, ``charger``, ``exporter`` et ``diff_profond``.
"""

from __future__ import annotations

import argparse
import json
import sys
import tempfile
from pathlib import Path

RACINE = Path(__file__).resolve().parents[2]
EDITEUR = RACINE / "legacy/world-builder-html/lune-noire-editor.html"
DONNEES = RACINE / "legacy/world-builder-html/lune-noire-data.json"


def diff_profond(a, b, chemin="", limite=50):
    """Compare deux valeurs JSON et renvoie la liste des différences.

    \\param chemin préfixe du chemin JSON (``villes[3].name``) pour les messages.
    \\param limite nombre maximal de différences rapportées (arrêt au-delà).
    Les types sont comparés strictement (chaîne contre liste, booléen contre
    nombre), sauf entier et flottant de même valeur.
    """
    out: list[str] = []

    def rec(x, y, p):
        if len(out) >= limite:
            return
        nombre = (int, float)
        if isinstance(x, bool) or isinstance(y, bool):
            if type(x) is not type(y) or x != y:
                out.append(f"{p or '/'} : {json.dumps(x, ensure_ascii=False)[:120]} → {json.dumps(y, ensure_ascii=False)[:120]}")
            return
        if isinstance(x, nombre) and isinstance(y, nombre):
            if x != y:
                out.append(f"{p} : {x} → {y}")
            return
        if type(x) is not type(y):
            out.append(f"{p or '/'} : type {type(x).__name__} → {type(y).__name__} "
                       f"({json.dumps(x, ensure_ascii=False)[:80]} → {json.dumps(y, ensure_ascii=False)[:80]})")
            return
        if isinstance(x, dict):
            for k in x:
                if k not in y:
                    out.append(f"{p}.{k} : supprimé")
                else:
                    rec(x[k], y[k], f"{p}.{k}" if p else k)
            for k in y:
                if k not in x:
                    out.append(f"{p}.{k} : ajouté ({json.dumps(y[k], ensure_ascii=False)[:80]})")
            return
        if isinstance(x, list):
            if len(x) != len(y):
                out.append(f"{p} : longueur {len(x)} → {len(y)}")
            for i, (u, v) in enumerate(zip(x, y)):
                rec(u, v, f"{p}[{i}]")
            return
        if x != y:
            out.append(f"{p} : {json.dumps(x, ensure_ascii=False)[:120]} → {json.dumps(y, ensure_ascii=False)[:120]}")

    rec(a, b, chemin)
    return out


def ouvrir_editeur(navigateur, editeur: Path, taille=(1500, 950)):
    """Ouvre l'éditeur dans une nouvelle page et collecte les erreurs console.

    \\param editeur chemin du fichier HTML de l'éditeur.
    \\param taille dimensions de la fenêtre (pixels), utiles pour les captures.
    Google Fonts est bloqué (aucun réseau requis) ; les erreurs dues à ce blocage
    sont ignorées. Renvoie ``(page, erreurs)`` : ``erreurs`` est une liste remplie
    au fil de l'eau (messages console de niveau erreur et exceptions JavaScript).
    """
    page = navigateur.new_page(viewport={"width": taille[0], "height": taille[1]}, accept_downloads=True)
    page.route("**/fonts.googleapis.com/**", lambda r: r.abort())
    page.route("**/fonts.gstatic.com/**", lambda r: r.abort())
    erreurs: list[str] = []

    def console(msg):
        if msg.type == "error" and "fonts.g" not in msg.text and "ERR_FAILED" not in msg.text:
            erreurs.append(msg.text)

    page.on("console", console)
    page.on("pageerror", lambda e: erreurs.append(f"exception : {e}"))
    page.goto(editeur.resolve().as_uri())
    return page, erreurs


def charger(page, donnees: Path):
    """Charge le fichier JSON dans l'éditeur par le bouton « Importer ».

    \\param donnees chemin du ``lune-noire-data.json`` à charger.
    Attend la fin du chargement (tableau de bord affiché, ``db.villes`` rempli).
    """
    page.set_input_files("#fileInput", str(donnees))
    page.wait_for_function("() => window.db && db.villes && db.villes.length > 0 && "
                           "document.getElementById('sectionContent').style.display === 'block'",
                           timeout=120_000)


def exporter(page, dossier: Path, nom="export.json"):
    """Clique « Exporter JSON » et renvoie ``(objet, texte)`` du fichier téléchargé.

    \\param dossier dossier temporaire où le téléchargement est écrit.
    """
    with page.expect_download(timeout=120_000) as dl:
        page.click("button:has-text('Exporter JSON')")
    cible = dossier / nom
    dl.value.save_as(str(cible))
    texte = cible.read_text(encoding="utf-8")
    return json.loads(texte), texte


def test_chargement(page, original, dossier):
    """Chargement : renvoie les types changés ou champs retirés dans ``db`` (vide attendu).

    Les clés ajoutées par l'éditeur (collections par défaut) ne sont pas des
    différences : seul l'export doit les omettre, ce que vérifie l'aller-retour.
    """
    db = json.loads(page.evaluate("() => JSON.stringify(db)"))
    return [d for d in diff_profond(original, db, limite=10_000) if ": ajouté" not in d][:50]


def test_aller_retour(page, original, dossier):
    """Export sans modification : renvoie la liste des différences (vide attendue)."""
    export, _ = exporter(page, dossier, "aller_retour.json")
    return diff_profond(original, export)


def test_nom_ville(page, original, dossier, ville_id="dys"):
    """Renomme une ville par son formulaire puis exporte.

    \\param ville_id identifiant de la ville renommée (Dys par défaut : ville sans
    ``anchor_tile``, ce qui vérifie aussi que le formulaire n'ajoute pas de champ).
    Renvoie la liste des différences autres que ``villes[i].name`` (vide attendue)
    et un message d'échec si le nouveau nom n'est pas dans l'export.
    """
    idx = page.evaluate("id => db.villes.findIndex(v => v.id === id)", ville_id)
    if idx < 0:
        return [f"ville {ville_id} introuvable"]
    page.evaluate("i => openVilleForm(i)", idx)
    nouveau = original["villes"][idx]["name"] + " (test)"
    page.fill("#v_name", nouveau)
    page.click("#modalFooter button:has-text('Enregistrer')")
    export, _ = exporter(page, dossier, "nom_ville.json")
    attendu = json.loads(json.dumps(original))
    attendu["villes"][idx]["name"] = nouveau
    diffs = diff_profond(attendu, export)
    if export["villes"][idx]["name"] != nouveau:
        diffs.insert(0, "nouveau nom absent de l'export")
    return diffs


def main() -> int:
    """Lance les trois tests et affiche le résultat ; code de sortie 0 si tout passe."""
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--editeur", type=Path, default=EDITEUR)
    ap.add_argument("--donnees", type=Path, default=DONNEES)
    args = ap.parse_args()
    from playwright.sync_api import sync_playwright

    original = json.loads(args.donnees.read_text(encoding="utf-8"))
    ok = True
    with sync_playwright() as p, tempfile.TemporaryDirectory() as tmp:
        nav = p.chromium.launch()
        page, erreurs = ouvrir_editeur(nav, args.editeur)
        charger(page, args.donnees)
        for nom, fn in (("chargement sans changement de type", test_chargement),
                        ("aller-retour sans modification", test_aller_retour),
                        ("nom d'une ville modifié", test_nom_ville)):
            diffs = fn(page, original, Path(tmp))
            print(f"[{'OK' if not diffs else 'ÉCHEC'}] {nom} : {len(diffs)} différence(s)")
            for d in diffs[:30]:
                print("   ", d)
            ok &= not diffs
        if erreurs:
            ok = False
            print(f"[ÉCHEC] {len(erreurs)} erreur(s) console :")
            for e in erreurs[:10]:
                print("   ", e)
        nav.close()
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
