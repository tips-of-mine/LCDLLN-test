#!/usr/bin/env python3
# Remplit les services des villes et leurs PNJ de service dans le fichier de lore
# (legacy/world-builder-html/lune-noire-data-v20.json et sa copie
# lune-noire-data.json).
#
# Regles (source de verite : game_design.cities_services.grille dans le fichier) :
#   - chaque ville a une TAILLE (hameau, village, ville, grande_ville, capitale,
#     forteresse, sans_service) qui fixe, pour chaque service, un nombre [min, max] ;
#   - ecurie : 1 par sortie terrestre (routes de type "road"), minimum 1 ;
#   - port   : seulement les villes listees dans grille.ports ;
#   - arene  : seulement les villes listees dans grille.arenes ;
#   - cimetiere : 1 dans chaque ville habitee ;
#   - grille.ajustements : surcharges ponctuelles par ville.
#   - chaque exemplaire de service = 1 PNJ, avec un nom propre unique dans tout
#     le monde et jamais repris des BD (personnages, compagnons, villes).
#
# Deterministe (graine par ville et par service) et idempotent : un PNJ deja
# present (meme id) garde son nom, son genre et sa race ; seuls les manquants
# sont crees, les surnumeraires retires.
#
# Lancement : python tools/lore/generate_city_services.py [--check]
#   --check : n'ecrit rien, sort en erreur si le fichier n'est pas a jour.
import argparse
import json
import os
import random
import re
import sys
import unicodedata

REPO = os.path.normpath(os.path.join(os.path.dirname(__file__), "..", ".."))
LORE_DIR = os.path.join(REPO, "legacy", "world-builder-html")
LORE_FILES = ["lune-noire-data-v20.json", "lune-noire-data.json"]
SEED = "lcdlln-services-v1"

SERVICES = [
    "auberge", "potions", "armes", "armures", "forgeron", "marche", "ecurie",
    "temple", "banque", "guilde", "infirmerie", "bibliotheque", "prison",
    "port", "arene", "ecole_magie", "cimetiere",
]

METIERS = {
    "auberge": "aubergiste", "potions": "herboriste", "armes": "marchand_armes",
    "armures": "armurier", "forgeron": "forgeron", "marche": "marchand",
    "ecurie": "palefrenier", "temple": "pretre", "banque": "banquier",
    "guilde": "maitre_de_guilde", "infirmerie": "guerisseur",
    "bibliotheque": "bibliothecaire", "prison": "geolier",
    "port": "capitaine_de_port", "arene": "maitre_d_arene",
    "ecole_magie": "maitre_de_magie", "cimetiere": "fossoyeur",
}

# Grille validee par Hubert le 2026-10-25 (valeurs [min, max]).
# "ecurie", "port" et "arene" sont regles par grille.ecurie / ports / arenes.
DEFAULT_GRILLE = {
    "_comment": "Services par taille de ville (valeurs [min, max] ; 1 exemplaire = 1 PNJ). "
                "Ecurie : 1 par sortie terrestre (routes 'road'), minimum 1. Port et arene : "
                "seulement les villes listees. Cimetiere : 1 par ville habitee. "
                "Genere et verifie par tools/lore/*_city_services.py.",
    "version": 1,
    "tailles": {
        "hameau": {"pnj": [10, 19], "services": {
            "auberge": [1, 1], "potions": [0, 1], "armes": [0, 1], "armures": [0, 1],
            "forgeron": [0, 1], "marche": [0, 0], "temple": [0, 0], "banque": [0, 0],
            "guilde": [0, 0], "infirmerie": [0, 0], "bibliotheque": [0, 0],
            "prison": [0, 0], "ecole_magie": [0, 0], "cimetiere": [1, 1]}},
        "village": {"pnj": [20, 39], "services": {
            "auberge": [1, 1], "potions": [0, 1], "armes": [0, 1], "armures": [0, 1],
            "forgeron": [0, 1], "marche": [0, 1], "temple": [0, 1], "banque": [0, 1],
            "guilde": [0, 1], "infirmerie": [0, 1], "bibliotheque": [0, 0],
            "prison": [0, 0], "ecole_magie": [0, 0], "cimetiere": [1, 1]}},
        "ville": {"pnj": [40, 59], "services": {
            "auberge": [1, 2], "potions": [1, 2], "armes": [1, 2], "armures": [1, 2],
            "forgeron": [1, 2], "marche": [1, 2], "temple": [1, 1], "banque": [1, 1],
            "guilde": [1, 1], "infirmerie": [1, 2], "bibliotheque": [0, 1],
            "prison": [0, 1], "ecole_magie": [0, 1], "cimetiere": [1, 1]}},
        "grande_ville": {"pnj": [60, 79], "services": {
            "auberge": [2, 2], "potions": [1, 2], "armes": [1, 2], "armures": [1, 2],
            "forgeron": [1, 2], "marche": [1, 2], "temple": [2, 2], "banque": [2, 2],
            "guilde": [2, 2], "infirmerie": [1, 2], "bibliotheque": [1, 2],
            "prison": [1, 1], "ecole_magie": [2, 2], "cimetiere": [1, 1]}},
        "capitale": {"pnj": [80, 100], "services": {
            "auberge": [2, 3], "potions": [2, 3], "armes": [2, 3], "armures": [2, 3],
            "forgeron": [2, 3], "marche": [2, 3], "temple": [2, 2], "banque": [2, 2],
            "guilde": [2, 2], "infirmerie": [2, 3], "bibliotheque": [2, 3],
            "prison": [1, 1], "ecole_magie": [2, 2], "cimetiere": [1, 1]}},
        "forteresse": {"pnj": [30, 50], "services": {
            "auberge": [1, 1], "potions": [0, 0], "armes": [0, 0], "armures": [0, 0],
            "forgeron": [1, 1], "marche": [1, 1], "temple": [0, 0], "banque": [0, 0],
            "guilde": [1, 1], "infirmerie": [1, 1], "bibliotheque": [0, 0],
            "prison": [1, 1], "ecole_magie": [0, 0], "cimetiere": [1, 1]}},
        "sans_service": {"pnj": [0, 0], "services": {}},
    },
    "types_vers_taille": {
        "farm_hamlet": "hameau", "fishing_hamlet": "hameau", "hamlet": "hameau",
        "hameau": "hameau", "village": "village", "town": "village",
        "market_town": "ville", "port_town": "ville", "city": "ville",
        "major_city": "grande_ville", "capital_city": "capitale",
        "fortress_city": "forteresse",
    },
    "ports": ["ville_altenberg", "town_ile_ashwick", "town_0123", "ville_lhynn",
              "hamlet_0172", "ville_tbarak", "town_0137", "hishtarland"],
    "arenes": ["ville_feyhin_lokcthat"],
    "ecurie": {"route_types": ["road"], "minimum": 1},
    "ajustements": {
        "ville_oracle_camp": {"temple": [1, 1]},
        "sauken_glude_of_magistrya": {"ecole_magie": [2, 2]},
        "city_dragon_palace": {"temple": [1, 1]},
    },
}

# Taille et type decides avec Hubert le 2026-10-25 pour les villes sans type,
# les villes speciales et les reclassements (Altenberg, Northind).
TAILLE_VILLES = {
    "ville_ghrunke_dash": ("grande_ville", "major_city"),
    "hishtarland": ("grande_ville", "major_city"),
    "ville_cite_jade_t7": ("grande_ville", "major_city"),
    "ville_altenberg": ("grande_ville", "major_city"),
    "ville_northind": ("grande_ville", "major_city"),
    "dys": ("ville", "city"),
    "horker": ("ville", "city"),
    "ville_tbarak": ("ville", "city"),
    "ville_hazol": ("ville", "city"),
    "kendrhir_vale": ("ville", None),
    "ville_feyhin_lokcthat": ("ville", None),
    "sauken_glude_of_magistrya": ("ville", None),
    "city_dragon_palace": ("ville", None),
    "ville_whispers_hill": ("village", "village"),
    "leviathan_jungle": ("village", "village"),
    "ville_tsaroth": ("village", "village"),
    "ville_orkh": ("village", "village"),
    "ville_oracle_camp": ("village", None),
    "ville_klindor": ("forteresse", "fortress_city"),
    "ville_hoengardt_t11": ("forteresse", "fortress_city"),
    "ville_forteresse_naine_heureux_t11": ("forteresse", "fortress_city"),
    "ville_garundel_fief": ("forteresse", "fortress_city"),
    "city_krak_de_jhenner": ("forteresse", None),
    "ville_village_fantome": ("sans_service", "other"),
    "ville_necropolis_jade_t7": ("sans_service", "ruins_settlement"),
    "ville_chateau_ruine_t00": ("sans_service", "ruins_settlement"),
    "ville_der_hem_shelsem": ("sans_service", "undead_kingdom"),
}

RENOMMAGES = {"horker": "Harrowdale"}

# Race des PNJ : connue pour les villes de depart (world_master.json), deduite
# de la faction pour la Lune Noire, humains par defaut. Liste ponderee.
RACE_VILLES = {
    "ville_whispers_hill": [("race_human", 1), ("race_haut_elfe", 1)],
    "leviathan_jungle": [("race_dzorak", 1)],
    "ville_hoengardt_t11": [("race_dzorak", 1)],
    "ville_orkh": [("race_orkh", 1)],
    "ville_ghrunke_dash": [("race_dwarf", 1)],
    "ville_forteresse_naine_heureux_t11": [("race_dwarf", 1)],
    "ville_tsaroth": [("race_demon", 2), ("race_dark_elf", 1)],
}
RACE_FACTIONS = {
    "lune_noire": [("race_human", 4), ("race_dark_elf", 3), ("race_dzorak", 2), ("race_demon", 1)],
}

# Syllabes par race : (debuts, fins masculines, fins feminines, noms de famille).
NOMS = {
    "race_human": (
        ["Al", "Ber", "Cor", "Dar", "Ed", "Fal", "Gar", "Hal", "Is", "Jor", "Kel", "Lor",
         "Mar", "Nor", "Os", "Per", "Ral", "Ser", "Tor", "Ul", "Var", "Wel", "Yv", "Ren",
         "Bran", "Ced", "Dun", "Eld", "Gil", "Hug", "Lan", "Mael", "Odo", "Rol", "Tib"],
        ["ric", "mond", "bert", "ard", "in", "old", "mar", "vin", "ius", "ald", "ran",
         "dor", "ot", "us", "wald", "an", "ais"],
        ["a", "ine", "elle", "isse", "ia", "ette", "ise", "ora", "ance", "ilde", "aude",
         "ane", "eline"],
        (["Val", "Mor", "Brun", "Haut", "Gris", "Roch", "Font", "Bel", "Clair", "Noir",
          "Sombre", "Fer", "Pierre", "Chen", "Saul", "Bois", "Corb", "Marl", "Ross", "Bleu"],
         ["morne", "val", "lac", "mont", "brume", "garde", "fort", "combe", "rive",
          "castel", "mare", "pré", "lande", "vigne", "ombre", "rade", "chêne"])),
    "race_haut_elfe": (
        ["Ela", "Ilan", "Sae", "Fae", "Lua", "Thal", "Cae", "Eri", "Lir", "Ama", "Nae",
         "Ithi", "Aer", "Cele", "Mire"],
        ["ndil", "rion", "thas", "nor", "dir", "las", "vel", "ion", "lad"],
        ["wyn", "riel", "lith", "ndra", "viel", "nya", "lie", "sse", "rae"],
        (["Silv", "Aur", "Lun", "Ven", "Cel", "Fen", "Lor", "Ael", "Sel"],
         ["anor", "ethil", "aris", "iel", "ion", "andel", "orin", "essa", "aleth"])),
    "race_dark_elf": (
        ["Ael", "Vae", "Syl", "Ny", "Xal", "Ith", "Zhe", "Myr", "Lae", "Ilv", "Rhy",
         "Shae", "Vel", "Qua", "Iz"],
        ["driel", "thas", "rion", "zar", "nyth", "vorn", "dris", "sek", "lith"],
        ["ith", "rae", "zra", "nys", "ssa", "vyre", "line", "zith", "ndra"],
        (["Nox", "Umbr", "Vel", "Sha", "Zar", "Mor", "Xyl", "Dra", "Vae"],
         ["iel", "eth", "ara", "yth", "ion", "oss", "aryn", "elle", "iss"])),
    "race_dwarf": (
        ["Bal", "Dur", "Grim", "Kor", "Brom", "Dwal", "Gol", "Hur", "Mor", "Rag", "Tor",
         "Bof", "Nar", "Ost", "Thur", "Gund", "Brak"],
        ["in", "ar", "ek", "dur", "grim", "rik", "bur", "li", "ok", "nar", "din", "gan"],
        ["a", "hild", "dis", "ra", "nja", "ga", "rid", "unn", "bera"],
        (["Poing", "Marteau", "Cœur", "Main", "Bouclier", "Hache", "Front", "Pic",
          "Enclume", "Torche", "Bras"],
         ["-de-Pierre", "-d'Acier", "-de-Roc", "-de-Cuivre", "-de-Granit", "-d'Or",
          "-de-Basalte", "-de-Bronze", "-d'Étain", "-de-Houille"])),
    "race_dzorak": (
        ["Grok", "Ug", "Thr", "Zug", "Mog", "Kra", "Gor", "Shak", "Nar", "Drub", "Urg",
         "Ruk", "Bor", "Gash", "Vrog", "Hruk"],
        ["ash", "gor", "nak", "thar", "ub", "rok", "zag", "mash", "gul", "dak", "ruk"],
        ["zka", "gha", "ra", "sha", "nka", "rga", "ula", "zha"],
        (["Croc", "Os", "Crâne", "Poing", "Sang", "Ongle", "Tibia", "Groin"],
         ["-Brisé", "-Noir", "-Rouge", "-Fendu", "-Sanglant", "-de-Fer", "-Tordu",
          "-Brûlant"])),
    "race_orkh": (
        ["Grou", "Pou", "Ronf", "Bru", "Mou", "Tru", "Gro", "Bou", "Fro", "Grim", "Lou"],
        ["ik", "ouf", "ac", "ot", "ul", "ard", "on"],
        ["ille", "otte", "ine", "ouse", "ette"],
        (["Sabot", "Glandier", "Jarret", "Lard", "Fouisseur", "Museau", "Bourbier"],
         ["-Vif", "-Rond", "-Gras", "-Doux", "-Fort", "-Lent", "-Fin"])),
    "race_demon": (
        ["Az", "Bel", "Mal", "Xer", "Zag", "Vor", "Ash", "Bael", "Ner", "Qar", "Uz",
         "Kesh", "Vex", "Sar"],
        ["goth", "zeth", "rax", "phas", "thor", "akh", "uun", "oth", "zul", "mon"],
        ["viel", "ith", "ara", "essa", "ra", "ys", "zea", "lith"],
        (["Cendre", "Braise", "Ombre", "Flamme", "Plaie", "Cicatrice", "Fournaise"],
         ["-Noire", "-Vive", "-Éternelle", "-Rouge", "-Sans-Fin", "-Hurlante", "-Froide"])),
}


# Prenoms reels ou deja connotes que le generateur peut produire par hasard :
# refuses pour garder des noms propres au monde du jeu.
PRENOMS_REFUSES = {
    "albert", "edmond", "osmond", "maria", "marine", "marin", "garance", "corine",
    "alvin", "tibia", "yvan", "nora", "ulric", "baldur", "perine", "dara", "eda",
    "alise", "aline", "martin", "marius", "lorine", "marise", "odette", "yvette",
    "cedric", "edwin", "gerald", "harold", "ronald", "donald", "valentin", "corentin",
}


def prenom_correct(prenom):
    """Refuse les prenoms reels et les sonorites maladroites (voyelle doublee, syllabe repetee)."""
    p = norm(prenom)
    if p in PRENOMS_REFUSES:
        return False
    if re.search(r"([aeiouyv])\1", p) or re.search(r"([a-z]{2})\1", p):
        return False
    return True


def norm(text):
    """Minuscule ASCII sans accents, pour comparer des noms."""
    return unicodedata.normalize("NFD", text.lower()).encode("ascii", "ignore").decode()


def mots(text):
    """Mots de 3 lettres ou plus d'un nom (pour la liste des noms interdits)."""
    return {m for m in re.split(r"[^a-z]+", norm(text)) if len(m) >= 3}


def noms_interdits(data):
    """Mots tires des BD et du monde : personnages, compagnons, villes, lieux, PNJ existants."""
    interdits = set()
    for coll in ("persons", "companions", "villes", "lieux"):
        for item in data.get(coll, []):
            interdits |= mots(item.get("name", ""))
            interdits |= mots(item.get("alias", "") or "")
    world_master = os.path.join(REPO, "game", "data", "world", "royaumes", "world_master.json")
    if os.path.exists(world_master):
        with open(world_master, encoding="utf-8") as f:
            for it in json.load(f).get("interactables", []):
                interdits |= mots(it.get("label", ""))
    return interdits


def taille_de(ville, grille):
    """Taille de la ville : champ explicite du fichier, sinon decision connue, sinon d'apres son type."""
    if ville.get("taille"):
        return ville["taille"]
    if ville["id"] in TAILLE_VILLES:
        return TAILLE_VILLES[ville["id"]][0]
    return grille["types_vers_taille"].get(ville.get("type", ""))


def sorties_terrestres(data, grille):
    """Nombre de routes terrestres touchant chaque ville."""
    types = set(grille["ecurie"]["route_types"])
    compte = {}
    for r in data.get("routes", []):
        if r.get("type") not in types:
            continue
        for cle in ("from_ville_id", "to_ville_id"):
            vid = r.get(cle)
            if vid:
                compte[vid] = compte.get(vid, 0) + 1
    return compte


def nombre_par_service(ville, taille, grille, sorties):
    """Nombre d'exemplaires de chaque service pour une ville (tirage deterministe)."""
    if taille == "sans_service":
        return {}
    regles = dict(grille["tailles"][taille]["services"])
    regles.update(grille.get("ajustements", {}).get(ville["id"], {}))
    resultat = {}
    for service in SERVICES:
        if service == "ecurie":
            n = max(grille["ecurie"]["minimum"], sorties.get(ville["id"], 0))
        elif service == "port":
            n = 1 if ville["id"] in grille["ports"] else 0
        elif service == "arene":
            n = 1 if ville["id"] in grille["arenes"] else 0
        else:
            mini, maxi = regles.get(service, [0, 0])
            n = random.Random(f"{SEED}:{ville['id']}:{service}").randint(mini, maxi)
        if n:
            resultat[service] = n
    return resultat


def race_de(ville, rng):
    """Race tiree pour un PNJ de la ville."""
    choix = RACE_VILLES.get(ville["id"]) or RACE_FACTIONS.get(ville.get("faction_control", "")) \
        or [("race_human", 1)]
    races, poids = zip(*choix)
    return rng.choices(races, weights=poids)[0]


def nom_aleatoire(race, genre, rng):
    debuts, fins_m, fins_f, (fam_a, fam_b) = NOMS[race]
    prenom = rng.choice(debuts) + rng.choice(fins_m if genre == "male" else fins_f)
    while True:
        a, b = rng.choice(fam_a), rng.choice(fam_b)
        na, nb = norm(a), norm(b).lstrip("-")
        # Evite "Sombreombre", "Marllac" : partie repetee ou lettre doublee a la jointure.
        if nb not in na and na not in nb and na[-1] != nb[0]:
            return prenom, a + b


def creer_nom(race, genre, rng, pris, familles_ville, interdits):
    """Nom propre unique dans le monde, hors noms des BD, nom de famille unique dans la ville."""
    for _ in range(5000):
        prenom, famille = nom_aleatoire(race, genre, rng)
        if not prenom_correct(prenom):
            continue
        complet = f"{prenom} {famille}"
        if norm(complet) in pris or norm(famille) in familles_ville:
            continue
        if mots(prenom) & interdits or mots(famille.replace("-", " ")) & interdits:
            continue
        pris.add(norm(complet))
        familles_ville.add(norm(famille))
        return complet
    raise RuntimeError(f"impossible de trouver un nom libre ({race}, {genre})")


def generer(data):
    """Applique la grille au fichier de lore. Retourne la liste des changements."""
    design = data.setdefault("game_design", {}).setdefault("cities_services", {})
    grille = design.setdefault("grille", DEFAULT_GRILLE)
    interdits = noms_interdits(data)
    sorties = sorties_terrestres(data, grille)
    changements = []

    # Noms deja attribues (idempotence) : on les garde et on les reserve.
    pris = set()
    for v in data["villes"]:
        for pnj in v.get("service_npcs", []):
            pris.add(norm(pnj["name"]))

    for v in data["villes"]:
        if v["id"] in RENOMMAGES and not v.get("taille") and v["name"] != RENOMMAGES[v["id"]]:
            changements.append(f"{v['id']}: renommee {v['name']} -> {RENOMMAGES[v['id']]}")
            v["name"] = RENOMMAGES[v["id"]]
        # Premier passage seulement : ensuite le champ "taille" du fichier fait foi.
        decision = TAILLE_VILLES.get(v["id"]) if not v.get("taille") else None
        if decision and decision[1] and v.get("type") != decision[1]:
            changements.append(f"{v['id']}: type {v.get('type')!r} -> {decision[1]!r}")
            v["type"] = decision[1]
        taille = taille_de(v, grille)
        if taille is None:
            raise SystemExit(f"ERREUR : pas de taille pour la ville {v['id']} (type {v.get('type')!r})")
        v["taille"] = taille

        nombres = nombre_par_service(v, taille, grille, sorties)
        v["services"] = [s for s in SERVICES if s in nombres]

        existants = {p["id"]: p for p in v.get("service_npcs", [])}
        familles_ville = {norm(p["name"].split(" ", 1)[1]) for p in existants.values() if " " in p["name"]}
        pnjs = []
        for service in v["services"]:
            for i in range(1, nombres[service] + 1):
                pid = f"pnj_{v['id']}_{service}_{i:02d}"
                if pid in existants:
                    pnjs.append(existants[pid])
                    continue
                rng = random.Random(f"{SEED}:{pid}")
                genre = rng.choice(["male", "female"])
                race = race_de(v, rng)
                nom = creer_nom(race, genre, rng, pris, familles_ville, interdits)
                pnjs.append({"id": pid, "name": nom, "gender": genre, "race_id": race,
                             "service": service, "metier": METIERS[service]})
        if pnjs != v.get("service_npcs", []):
            changements.append(f"{v['id']}: {len(pnjs)} PNJ de service")
        v["service_npcs"] = pnjs
    return changements


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="verifie sans ecrire")
    args = parser.parse_args()

    principal = os.path.join(LORE_DIR, LORE_FILES[0])
    with open(principal, encoding="utf-8") as f:
        texte = f.read()
    data = json.loads(texte)
    changements = generer(data)
    sortie = json.dumps(data, ensure_ascii=False, indent=2)

    if args.check:
        if sortie != texte:
            print("ERREUR : services/PNJ non a jour, lancer tools/lore/generate_city_services.py")
            for c in changements[:20]:
                print("  -", c)
            return 1
        print("OK : services et PNJ a jour")
        return 0

    for nom in LORE_FILES:
        with open(os.path.join(LORE_DIR, nom), "w", encoding="utf-8", newline="\n") as f:
            f.write(sortie)
    print(f"{len(changements)} changement(s) ecrit(s) dans {', '.join(LORE_FILES)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
