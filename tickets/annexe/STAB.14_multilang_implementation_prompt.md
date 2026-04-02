# Prompt d’implémentation — Système multilingue dynamique (LCDLLN)

Tu es un agent de développement senior C++ travaillant **directement dans ce dépôt**. 
Ta mission est d’implémenter un système de localisation (i18n/l10n) **runtime** pour le jeu, sans redémarrage ni recompilation pour changer la langue.

## Contexte codebase (à respecter)
- L’UI d’auth actuelle est gérée par `engine/client/AuthUi.*` (textes actuellement codés en dur en anglais).
- La config runtime est chargée via `engine/core/Config` (`config.json`), avec déjà une clé `client.locale`.
- Le moteur et les logs passent par `engine/core/Log`.
- Il existe déjà un répertoire d’assets de localisation `game/data/localization/` (actuellement orienté ressources visuelles).

## Objectif fonctionnel
Mettre en place un système où :
1. **Premier lancement**
   - Détecte la langue système.
   - Affiche une première interface de confirmation de langue (dans la langue détectée) avec autres langues disponibles.
   - Permet de choisir une langue parmi les bibliothèques disponibles.
2. Après confirmation, l’utilisateur arrive sur l’interface auth/login+inscription existante.
3. À tout moment, l’utilisateur peut aller dans **Options > Langue**, sélectionner une langue, puis cliquer **Appliquer**.
4. Le changement de langue est pris en compte **immédiatement** après “Appliquer” (sans relancer).
5. Aux lancements suivants, le jeu réutilise la langue choisie par l’utilisateur.
6. Tous les textes UI sont remplaçables dynamiquement.
7. Les bibliothèques de langue sont faciles à éditer/étendre.
8. Les logs tracent les changements et chargements de langue.

## Contraintes d’implémentation
- Pas de hardcode de textes UI directement dans les presenters (sauf fallback technique minimal).
- Les traductions doivent être chargées depuis des fichiers externes (ex: JSON) à chaud au démarrage.
- Prévoir fallback robuste:
  - clé absente -> fallback langue par défaut (ex: `en`),
  - langue absente -> fallback global.
- Garder le design simple, testable et extensible.
- Conserver le style du projet (noms, logs, commentaires, robustesse).

## Architecture attendue
Implémente les éléments suivants :

### 1) Service de localisation central
Créer un composant dédié (ex: `engine/client/LocalizationService.h/.cpp`) qui :
- charge un catalogue de langues depuis `game/data/localization/<lang>/<lang>.json` (ou chemin équivalent cohérent),
- expose:
  - `GetCurrentLocale()`
  - `GetAvailableLocales()`
  - `SetLocale(localeTag)`
  - `Translate(key, params?)`
- gère fallback + erreurs de parsing,
- journalise via `LOG_INFO/LOG_WARN/LOG_ERROR`.

### 2) Détection langue système
- Ajouter une détection portable best-effort:
  - Windows: API système adaptée,
  - Linux/macOS: variables d’environnement (`LANG`, `LC_ALL`, etc.) en fallback.
- Normaliser en tags courts compatibles catalogue (`fr`, `en`, `es`, ...).

### 3) Persistance de la langue utilisateur
- Si `client.locale` est vide au premier lancement:
  - utiliser langue système détectée,
  - afficher interface de confirmation.
- Persister la langue choisie pour les lancements suivants.
- Si `Config` n’a pas de sauvegarde, implémenter un mécanisme minimal sûr (ex: fichier user settings dédié type `user_settings.json`) plutôt que casser l’architecture.

### 4) UI de sélection de langue au premier lancement
- Ajouter un état UI “LanguageSelectionFirstRun” avant l’auth.
- Afficher:
  - message de confirmation dans la langue détectée,
  - liste des langues disponibles,
  - action de validation.
- Quand validé: entrée dans l’écran login/register.

### 5) UI Options > Langue
- Ajouter (ou étendre) l’UI options pour inclure rubrique **Langue**.
- Ajouter bouton **Appliquer** qui déclenche `SetLocale(...)`.
- Rafraîchir immédiatement les textes visibles.

### 6) Migration des textes existants
- Remplacer les chaînes hardcodées dans `AuthUiPresenter` (titres fenêtre, labels, messages d’erreur, hints) par des clés de traduction.
- Même approche pour autres UIs exposées si touchées par ce ticket.

### 7) Fichiers de langue
Créer au minimum:
- `game/data/localization/en/en.json`
- `game/data/localization/fr/fr.json`

Structure simple recommandée:
```json
{
  "common.apply": "Apply",
  "common.language": "Language",
  "auth.login_title": "Login",
  "auth.register_title": "Register",
  "auth.error.enter_login_password": "Enter login and password."
}
```

### 8) Logging explicite
Ajouter des logs utiles:
- langue système détectée,
- langue initiale retenue,
- changement de langue demandé/appliqué,
- fallback utilisé,
- clé manquante (en debug/warn).

## Critères d’acceptation (DoD)
- [ ] Au 1er lancement, interface de choix de langue visible avant auth.
- [ ] Le texte de cette interface est bien dans la langue détectée (ou fallback).
- [ ] Le changement via Options > Langue + Appliquer met à jour immédiatement les textes affichés.
- [ ] La langue choisie est persistée et rechargée au lancement suivant.
- [ ] Les textes Auth UI ne sont plus hardcodés en dur.
- [ ] Les logs montrent les événements de localisation.
- [ ] Build OK + tests ajoutés/mis à jour.

## Tests à produire
1. **Unit tests**
   - chargement catalogue valide/invalide,
   - fallback de langue et fallback de clé,
   - normalisation locale système.
2. **Tests d’intégration/UI (si possible dans le projet)**
   - premier lancement -> écran langue -> auth,
   - changement runtime de langue -> texte actualisé sans redémarrage,
   - persistance entre 2 exécutions.
3. **Journalisation**
   - vérifier présence des logs clés pour audit/debug.

## Livrables attendus
- Code complet compilable.
- Nouveaux fichiers de traduction et docs courtes de maintenance (comment ajouter une langue/clé).
- Notes de conception succinctes dans le PR (choix de persistance, fallback, limites).

## Exigences de sortie
Dans ta réponse finale :
1. Résume les fichiers modifiés.
2. Donne les commandes de build/test lancées et leurs résultats.
3. Liste les limites connues/restantes.
