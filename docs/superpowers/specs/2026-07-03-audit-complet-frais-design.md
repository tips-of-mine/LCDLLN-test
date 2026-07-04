# Audit complet frais du code — design (2026-07-03)

## Objectif

Balayer l'ensemble du code du projet (hors `legacy/`) à la recherche
d'**anomalies** (bugs, UB, races, fuites), de **problèmes de sécurité** et
d'**optimisations** candidates. Audit *frais* : il ne s'appuie pas sur les
audits de juin (`docs/audit/2026-06-*`) et ne cherche pas à en dédupliquer
les findings.

## Livrable

Un rapport unique : `docs/audit/2026-07-03-audit-complet-frais.md`, en
français, **sans aucune correction de code dans cette session**. Les
corrections éventuelles seront décidées ensuite par l'utilisateur et
livrées en PRs séparées cadrées.

Classement des findings par sévérité :

| Niveau | Signification |
|--------|---------------|
| **P0** | Sécurité critique (exploitable à distance, fuite de secrets, corruption mémoire déclenchable par un pair) |
| **P1** | Bug sérieux ou sécurité modérée (crash, perte de données, contournement de gating nécessitant des conditions) |
| **P2** | Bug mineur ou optimisation probable (candidat à mesurer) |
| **P3** | Qualité / suggestion |

Chaque finding comporte : localisation `fichier:ligne` cliquable, catégorie
(sécurité / bug / perf), description, recommandation.

Déploiement : ✅ docs uniquement, pas de redéploiement serveur.

## Périmètre

**Inclus :**

- `src/client`, `src/masterd`, `src/shardd`, `src/shared`, `src/world_editor`
- `web-portal/` (app, lib, components, email-templates)
- `sql/migrations/`
- Tools C++ : `tools/hlod_builder`, `tools/zone_builder`,
  `tools/impostor_builder`, `tools/load_tester`
- Passe légère : `deploy/docker`, `config.json` (secrets, ports exposés,
  clés sensibles), `game/data/config/slash_commands.json` (RBAC)

**Exclus :**

- `legacy/` (consigne permanente utilisateur)
- `game/data` hors slash_commands.json (données, pas du code)
- `tickets/`, `docs/`
- Assets binaires (`.fbx` du pipeline, textures, etc.)

## Dimensions d'analyse

1. **Sécurité** : parsing wire sans borne dans les handlers masterd/shardd,
   auth/sessions/RBAC (SessionManager, ConnectionSessionMap,
   AuthRegisterHandler, AccountStore), injection SQL dans les stores,
   web-portal (XSS, CSRF, secrets en dur, validation d'entrée), parsing de
   fichiers chargés depuis disque (buildings.bin, props.bin, config).
2. **Anomalies / bugs** : UB (débordements, use-after-free, variables non
   initialisées), data races (threads réseau vs rendu — précédent réel :
   crash géoloc PR #915/#918), fuites de ressources (objets Vulkan,
   handles, sockets), erreurs logiques.
3. **Optimisations** : allocations/copies par frame côté rendu, algorithmes
   O(n²) (collisions, réplication), requêtes DB dans des boucles,
   gaspillage GPU évident. **Analyse statique uniquement** (pas de
   toolchain de build locale) : les findings perf sont des candidats à
   mesurer, pas des certitudes.

## Méthode : workflow multi-agents

Opt-in explicite de l'utilisateur pour l'orchestration multi-agents
(choix « Workflow multi-agents » au cadrage).

- **Phase 1 — Audit** : un agent par cellule sous-système × dimension
  pertinente (~15 cellules). Le client (687 fichiers) est subdivisé en
  render / app / net+UI. Chaque agent retourne des findings structurés
  (fichier:ligne, sévérité, catégorie, description) via schéma JSON.
- **Phase 2 — Dédup** : barrière ; fusion des doublons inter-agents en
  code (clé fichier+ligne±ε+catégorie).
- **Phase 3 — Vérification adversariale** : chaque finding P0/P1 est soumis
  à un agent sceptique chargé de le **réfuter** en relisant le code réel ;
  les findings réfutés sont éliminés ou rétrogradés. Les P2/P3 passent une
  vérification simple (existence du code cité, plausibilité).
- **Phase 4 — Synthèse** : rédaction du rapport final classé P0→P3.

## Critères de succès

- Toutes les cellules du périmètre couvertes (aucune abandonnée en silence ;
  si une cellule échoue, le rapport le mentionne).
- Zéro finding P0/P1 non contre-vérifié dans le rapport.
- Chaque finding localisé précisément et actionnable.
- Rapport committé et poussé sur la branche de travail.
