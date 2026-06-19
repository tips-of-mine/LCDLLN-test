# Spec — Sélection de langue au 1er lancement + suggestion géo-IP

**Date :** 2026-06-18
**Sous-système :** #1 du lot de demandes (langue / géoloc)
**Statut :** Design validé, en attente de plan d'implémentation
**Déploiement :** ✅ client uniquement, **pas** de redéploiement serveur

---

## 1. Objectif

Au **premier lancement** (aucune locale persistée), l'écran
`Phase::LanguageSelectionFirstRun` ne doit plus proposer **toutes** les locales
disponibles, mais une **liste filtrée et pertinente** déduite de :

- la **langue du système** (déjà détectée par le socle i18n existant) ;
- la **langue du pays** déterminé par géolocalisation de l'**IP publique** ;
- l'**anglais**, toujours présent comme filet de sécurité.

### Exemples de référence (DoD comportementale)

| Poste | Système | Pays IP | Langues proposées |
|-------|---------|---------|-------------------|
| France | Français | France | **{fr, en}** |
| Espagne | Allemand | Espagne | **{de, es, en}** |

---

## 2. Contexte existant (à respecter, ne pas réécrire)

- Socle i18n livré (ticket STAB.14) : `src/client/localization/LocalizationService.{h,cpp}`
  — `GetAvailableLocales()`, `SetLocale()`, `Translate()`, détection langue
  système, persistance dans `user_settings.json`.
- Écran 1er lancement livré (ticket AUTH-UI.7) :
  - modèle : `src/client/auth/screens/AuthScreenLanguageSelect.cpp`
    (`BuildModel_LanguageSelect`, `Update_LanguageSelect`,
    `ApplyLocaleSelection`, `ImGuiApplyFirstRunLanguageContinue`)
  - rendu : `src/client/render/auth/screens/AuthImGuiLanguageSelect.cpp`
- État actuel des catalogues : seuls `game/data/localization/en/en.json` et
  `fr/fr.json` existent. Les dossiers `de/`, `es/`, `it/`, `pl/`, `pt/` ne
  contiennent que des **images** (drapeaux/bannières), **pas** de catalogue JSON.
  → `GetAvailableLocales()` ne renvoie aujourd'hui que `fr`/`en`.
- Le client **n'a pas** de client HTTP générique.
- L'écran langue s'affiche **avant** l'auth (donc avant toute connexion master).

L'univers de langues est **borné aux 7 dossiers existants** :
`en, fr, es, de, it, pl, pt`.

---

## 3. Décisions de conception (verrouillées avec l'utilisateur)

| Sujet | Décision |
|-------|----------|
| Mécanisme géoloc | **API HTTP tierce** (client-only), pas via le master serveur |
| Provider | `ip-api.com` — un seul GET `http://ip-api.com/json/` (sans IP : géolocalise l'appelant) → champ `countryCode`. ⚠️ HTTP non chiffré (gratuit). |
| Périmètre catalogues | **Plomberie + traduction complète** des 5 langues manquantes (es/de/it/pt/pl) |
| Consentement | **Silencieux best-effort** : appel en arrière-plan, IP jamais stockée ni persistée |
| Hors-ligne | Non sur-ingénieré : « pas d'internet → le jeu ne fonctionne pas de toute façon ». La géoloc reste best-effort. |
| Échec géoloc / pays introuvable / pays sans catalogue | Fallback → **{langue système, anglais}** |
| Ambiguïtés pays | CH→fr, CA→fr (éditable dans le fichier data) |

---

## 4. Architecture & composants

Responsabilités isolées (ni réseau dans le presenter, ni réseau dans
`LocalizationService`) :

| Composant | Emplacement | Rôle | Dépendances |
|-----------|-------------|------|-------------|
| `LanguageSuggestionService` *(nouveau)* | `src/client/localization/` | Calcule la liste **suggérée** (union filtrée). Détient l'état géoloc async + la table pays→langue chargée du fichier data. | `LocalizationService`, `HttpGet`, `country_language.json` |
| `HttpGet` *(nouveau helper)* | `src/shared/platform/` | GET HTTP minimal best-effort via **WinHTTP** (Windows) ; stub no-op Linux/Mac. Timeout court, 1 essai. | WinHTTP |
| `country_language.json` *(nouveau)* | `game/data/localization/` | Mapping ISO pays → 1 des 7 langues. Éditable sans recompiler. | — |
| `AuthUiPresenter` *(modifié)* | `src/client/auth/screens/AuthScreenLanguageSelect.cpp` | `BuildModel_LanguageSelect` consomme `GetSuggestedLocales()` au lieu d'itérer toutes les locales. Poll du résultat géoloc dans `Update_LanguageSelect`. | `LanguageSuggestionService` |

**Distinction clé :** `LocalizationService.GetAvailableLocales()` reste la liste
**complète** (7 langues une fois les catalogues créés) et alimente
**Options > Langue**. La **suggestion filtrée** n'est utilisée **que** par
l'écran premier lancement.

### Interface pressentie `LanguageSuggestionService`

```cpp
class LanguageSuggestionService {
public:
    // Démarre la détection (langue système immédiate + GET géoloc async).
    // Idempotent ; ne fait rien si une locale est déjà persistée.
    void BeginDetection(const LocalizationService& loc);

    // À appeler chaque frame depuis Update_LanguageSelect (main thread) :
    // intègre le résultat géoloc s'il vient d'arriver. Renvoie true si la
    // liste suggérée a changé (→ le presenter reconstruit le modèle).
    bool PollGeoUpdate();

    // Union filtrée, ordonnée : [langue système, langue IP (si ≠), en (si ≠)]
    // ∩ catalogues disponibles. Toujours non vide (en présent au minimum).
    std::vector<std::string> GetSuggestedLocales() const;
};
```

---

## 5. Logique d'union (cœur fonctionnel)

```
suggérées = dédupliquer([ langueSystème, langueIP, "en" ]) ∩ catalogues_disponibles
```

- `en` **toujours** présent.
- `langueSystème` : fournie par le socle existant.
- `langueIP` : `countryCode` → table → langue ; **absente** si géoloc échoue.
- `∩ catalogues_disponibles` : on ne propose **jamais** une langue sans vrai
  catalogue (pas de texte anglais déguisé).
- **Ordre d'affichage** : langue système d'abord (carte **sélectionnée par
  défaut** ; le message de bienvenue s'affiche dans cette langue), puis langue
  IP si différente, puis anglais si pas déjà présent.

---

## 6. Flux au premier lancement (géoloc non bloquante)

```
1. Pas de locale persistée → Phase::LanguageSelectionFirstRun
2. LanguageSuggestionService::BeginDetection()
     → langue système connue immédiatement
     → GET http://ip-api.com/json/ lancé sur thread worker détaché
3. Écran s'affiche IMMÉDIATEMENT avec {système, en}   (zéro latence)
4. Update_LanguageSelect appelle PollGeoUpdate() chaque frame :
   4a. Réponse < ~2s & langueIP ∈ catalogues & ≠ déjà présente
         → insère la carte (ex: Espagnol), reconstruit le modèle,
           sans casser la sélection courante
   4b. Hors-ligne / timeout / pays inconnu / pays sans catalogue
         → ne change rien : l'utilisateur garde {système, en}
5. Validation → ApplyLocaleSelection(firstRun=true) → persiste la locale dans
   user_settings.json (comportement inchangé)
```

La géoloc ne tourne **que** quand aucune locale n'est persistée (sinon l'écran
est sauté). Le `countryCode` / l'IP ne sont **jamais** persistés ni stockés.

---

## 7. Table pays → langue (`game/data/localization/country_language.json`)

`countryCode` ISO-3166 alpha-2 → 1 des 7 langues ; **défaut → `en`** :

| Langue | Codes pays (noyau, extensible) |
|--------|--------------------------------|
| `fr` | FR, BE, CH, LU, MC, CA, CI, SN, CM |
| `es` | ES, MX, AR, CO, CL, PE, VE |
| `de` | DE, AT, LI |
| `it` | IT, SM, VA |
| `pl` | PL |
| `pt` | PT, BR, AO, MZ |
| `en` | GB, US, IE, AU, NZ, IN **+ tout pays non listé** |

Le filtre `∩ catalogues` protège : un code qui mappe vers une langue sans
catalogue retombe sur le défaut `en`.

---

## 8. Livrable traductions (5 nouveaux catalogues)

- Créer `es/es.json`, `de/de.json`, `it/it.json`, `pl/pl.json`, `pt/pt.json`,
  **miroir des clés de `en.json`** (source de référence).
- Remplissage **best-effort/automatique**, marqué pour relecture native (ton
  lore à affiner — risque qualité connu et accepté).
- Ajouter pour chaque langue : `language.native_line.<tag>`,
  `language.first_run.welcome.<tag>`, et l'entrée correspondante dans
  `LocalizedLanguageName`.

---

## 9. Réseau, threads, erreurs

- `HttpGet` via **WinHTTP** : GET unique, timeout ~2 s, **1 essai**, sur **thread
  worker détaché** (jamais bloquer l'UI/ImGui).
- Parsing : réutiliser le parseur JSON déjà présent (config/localization),
  extraire `countryCode`.
- Résultat remonté au main thread via flag/atomique, **lu dans
  `PollGeoUpdate()`** depuis `Update_LanguageSelect`.
- Toute exception / échec / timeout = **silencieux** → on garde {système, en}.
- Stub no-op Linux/Mac (cohérent avec le `#if defined(_WIN32)` de l'auth UI).

---

## 10. Tests

- **Logique d'union** (table-driven, `LanguageSuggestionServiceTests`) :
  - exemple FR+FR → {fr, en} ;
  - exemple ES+DE → {de, es, en} ;
  - échec géoloc → {système, en} ;
  - dédup quand système == langue IP ;
  - langue IP sans catalogue → exclue (retombe sur {système, en}).
- **Mapping pays→langue** : FR→fr, ES→es, inconnu→en, code→langue sans
  catalogue → en.
- **Parité de clés** : chaque catalogue (es/de/it/pl/pt) a exactement le jeu de
  clés de `en.json` (échoue en CI sinon → garde-fou anti-oubli).
- `HttpGet` abstrait derrière une interface → injection d'un **faux fournisseur
  géo** (renvoie un pays ou un échec) ; **aucun** appel réseau réel en CI.

---

## 11. Déploiement

> **Déploiement** : ✅ **client uniquement, pas de redéploiement serveur.**
> Aucun changement de protocole, aucun handler/opcode. Le nouveau fichier
> `country_language.json` et les catalogues sont lus côté client.

---

## 12. Hors périmètre (volontairement exclu)

- Géoloc via le master serveur (écartée au profit de l'API tierce).
- Consentement opt-in / opt-out RGPD avancé (choix : silencieux best-effort).
- Gestion hors-ligne élaborée (le jeu requiert internet de toute façon).
- Relecture/qualité native des traductions (livrées best-effort, à affiner
  ultérieurement).
- Les 5 autres demandes du lot (validation inscription, cimetière par-zone,
  boutons Register, flottement collision, suppression panneau Scène) — chacune
  aura son propre cycle spec → plan.
