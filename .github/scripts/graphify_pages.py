#!/usr/bin/env python3
"""Pages HTML de visualisation du graphe Graphify (publiées dans LCDLLN-graphity).

Usage : graphify_pages.py <racine_graphe> <dossier_pages>
  <racine_graphe>  dossier contenant graphify-out/graph.json (déjà regroupé
                   par `graphify cluster-only`).
  <dossier_pages>  dossier de sortie (créé) : index.html, tree.html,
                   graph.html, modules/<nom>.html.

Pages produites (sans LLM ni clé API) :
  - graph.html   vue globale : au-delà de ~5000 nœuds Graphify agrège par
                 communauté ; communautés nommées d'après leur nœud central
                 (GRAPHIFY_BACKEND=none).
  - tree.html    arborescence dossiers -> fichiers -> symboles, avec liens.
  - modules/*.html  une vue détaillée par module (nœuds cliquables, couleurs
                 par communauté), chaque vue sous LIMIT nœuds : un module trop
                 gros est découpé par sous-dossiers.
  - index.html   sommaire avec liens et nombres de nœuds.

Effet de bord : `graphify label` réécrit aussi GRAPH_REPORT.md et
.graphify_labels.json dans <racine_graphe>/graphify-out (noms de communautés).
"""
import collections
import html
import json
import os
import shutil
import subprocess
import sys
import tempfile

# Nombre maximal de nœuds par vue module (graph.html détaillé de Graphify
# jusqu'à 5000 ; marge pour la croissance du code).
LIMIT = 4500

# Modules racines, dans l'ordre de l'index. Le premier préfixe qui correspond
# l'emporte (src/client/render avant src/client).
MODULES = [
    ("render", "src/client/render", "Client — rendu Vulkan"),
    ("client", "src/client", "Client (hors rendu)"),
    ("shared", "src/shared", "Code partagé client / serveurs"),
    ("shardd", "src/shardd", "Serveur de jeu (shard)"),
    ("masterd", "src/masterd", "Serveur maître"),
    ("world_editor", "src/world_editor", "Éditeur monde"),
    ("tools", "tools", "Outils (world_gen, zone_builder…)"),
    ("web-portal", "web-portal", "Portail web"),
]

ENV = dict(os.environ, GRAPHIFY_BACKEND="none")


def run(*args):
    """Lance une commande graphify ; arrête le script en cas d'échec."""
    print("+", " ".join(args), flush=True)
    subprocess.run(args, check=True, env=ENV)


def module_of(path):
    """Clé du module racine d'un fichier source, ou None (hors modules)."""
    for key, prefix, _ in MODULES:
        if path == prefix or path.startswith(prefix + "/"):
            return key, prefix
    return None


def split_module(key, prefix, files_count):
    """Découpe un module trop gros en lots de sous-dossiers (≤ LIMIT nœuds).

    \\param files_count Counter {sous-dossier: nœuds} du module.
    \\return liste de (nom_vue, [sous-dossiers]).
    """
    bins = []  # [nœuds, [sous-dossiers]]
    for sub, n in files_count.most_common():
        for b in bins:
            if b[0] + n <= LIMIT:
                b[0] += n
                b[1].append(sub)
                break
        else:
            bins.append([n, [sub]])
    return [(f"{key}-{i + 1}", sorted(b[1])) for i, b in enumerate(bins)]


def build_views(graph):
    """Calcule les vues modules : [(nom, titre, préfixe, sous-dossiers|None, nb)]."""
    per_module = collections.Counter()
    per_sub = collections.defaultdict(collections.Counter)
    for n in graph["nodes"]:
        m = module_of(n.get("source_file") or "")
        if not m:
            continue
        key, prefix = m
        per_module[key] += 1
        rest = (n["source_file"])[len(prefix) + 1:]
        per_sub[key][rest.split("/")[0] if "/" in rest else "(racine)"] += 1
    views = []
    for key, prefix, title in MODULES:
        count = per_module.get(key, 0)
        if count == 0:
            continue
        if count <= LIMIT:
            views.append((key, title, prefix, None, count))
            continue
        for name, subs in split_module(key, prefix, per_sub[key]):
            n = sum(per_sub[key][s] for s in subs)
            views.append((name, f"{title} — {', '.join(subs)}", prefix, subs, n))
    return views


def in_view(path, prefix, subs):
    """True si le fichier appartient à la vue (module, éventuellement sous-dossiers)."""
    m = module_of(path)
    if not m or m[1] != prefix:
        return False
    if subs is None:
        return True
    rest = path[len(prefix) + 1:]
    return (rest.split("/")[0] if "/" in rest else "(racine)") in subs


def main():
    root, pages = sys.argv[1], sys.argv[2]
    out = os.path.join(root, "graphify-out")
    os.makedirs(os.path.join(pages, "modules"), exist_ok=True)

    # 1) Vue globale + noms de communautés (réécrit aussi GRAPH_REPORT.md).
    run("graphify", "label", root)
    shutil.copy(os.path.join(out, "graph.html"), os.path.join(pages, "graph.html"))

    # 2) Arborescence.
    run("graphify", "tree", "--graph", os.path.join(out, "graph.json"),
        "--output", os.path.join(pages, "tree.html"), "--label", "LCDLLN-test")

    # 3) Vues par module : sous-graphe (liens internes à la vue), regroupé et
    #    nommé à part pour que les couleurs reflètent la structure du module.
    with open(os.path.join(out, "graph.json"), encoding="utf-8") as f:
        graph = json.load(f)
    views = build_views(graph)
    tmp = tempfile.mkdtemp(prefix="graphify-views-")
    for name, title, prefix, subs, _ in views:
        keep = {n["id"] for n in graph["nodes"]
                if in_view(n.get("source_file") or "", prefix, subs)}
        sub = dict(graph)
        sub["nodes"] = [n for n in graph["nodes"] if n["id"] in keep]
        sub["links"] = [e for e in graph["links"]
                        if e["source"] in keep and e["target"] in keep]
        sub["hyperedges"] = []
        vroot = os.path.join(tmp, name)
        os.makedirs(os.path.join(vroot, "graphify-out"))
        with open(os.path.join(vroot, "graphify-out", "graph.json"), "w", encoding="utf-8") as f:
            json.dump(sub, f)
        run("graphify", "cluster-only", vroot, "--no-viz", "--no-label")
        run("graphify", "label", vroot)
        shutil.copy(os.path.join(vroot, "graphify-out", "graph.html"),
                    os.path.join(pages, "modules", f"{name}.html"))
    shutil.rmtree(tmp, ignore_errors=True)

    # 4) Sommaire.
    rows = "\n".join(
        f'<li><a href="modules/{html.escape(n)}.html">{html.escape(t)}</a>'
        f" <small>({c} nœuds)</small></li>"
        for n, t, _, _, c in views)
    total = len(graph["nodes"])
    with open(os.path.join(pages, "index.html"), "w", encoding="utf-8") as f:
        f.write(f"""<!DOCTYPE html>
<html lang="fr"><head><meta charset="utf-8"><title>LCDLLN — graphe Graphify</title>
<style>body{{font-family:system-ui,sans-serif;max-width:52rem;margin:2rem auto;padding:0 1rem;line-height:1.5}}
small{{color:#666}}</style></head><body>
<h1>LCDLLN-test — graphe Graphify</h1>
<p>{total} nœuds. Pages générées par la CI à chaque push sur main (sans LLM).</p>
<h2>Vues d'ensemble</h2>
<ul>
<li><a href="tree.html">Arborescence</a> <small>(dossiers → fichiers → symboles, avec liens)</small></li>
<li><a href="graph.html">Graphe global</a> <small>(agrégé par communauté)</small></li>
</ul>
<h2>Vues par module</h2>
<ul>
{rows}
</ul>
</body></html>
""")
    print(f"Pages : {len(views)} modules + tree + graph + index -> {pages}")


if __name__ == "__main__":
    main()
