# CMANGOS.37 — Platform (crash dump Windows client)

> **Ticket source** : [tickets/CMANGOS/CMANGOS.37_Platform_crash_dump_windows_client.md](../../../../tickets/CMANGOS/CMANGOS.37_Platform_crash_dump_windows_client.md)
> **Priorité** : P3 — ajout à valeur (ops)
> **Cible** : client (Windows)

## 1. Statut implémentation

❌ **Absent** — pas de système de crash dump auto-généré côté client.

## 2. Preuves dans le code

**Manquant :**
- ❌ `engine/client/platform/WheatyCrashReport.{h,cpp}` (Win32-only)
- ❌ `SetUnhandledExceptionFilter` installé
- ❌ DbgHelp integration (types + valeurs locales)
- ❌ Format texte lisible auto-généré

## 3. Recouvrement milestones existantes

❌ **Non couvert** — pas de milestone crash report client.

## 4. Écart par rapport à la spec CMANGOS

100% absent. Sans crash dump, debug de crashs joueurs = impossible
(juste "le client a crashé" sans contexte).

## 5. Effort estimé

**S** (1 PR) — adaptation simple du WheatyExceptionReport cmangos
(open source, ~1500 lignes C++). DbgHelp livré avec Windows SDK,
pas de nouvelle dépendance externe (au sens AGENTS.md).

## 6. Valeur joueur/serveur

**Élevée (post-launch)** — invisible joueur sain, **critique pour
debug** dès qu'un crash survient.

## 7. Dépendances bloquantes

- DbgHelp (Win32 API, déjà disponible)
- Aucune dépendance bloquante.

## 8. Risque / piège ⚠️

- ⚠️ **Confidentialité** — crash dump peut contenir des PII (chemins
  user, mots de passe en mémoire). Filtre obligatoire avant upload.
- ⚠️ **Upload endpoint** — où envoyer les dumps ? Endpoint serveur ?
  Demander consentement utilisateur (RGPD).
- ⚠️ **Symboles** — sans PDB, dump moins exploitable. Stratégie
  symboles à acter (PDB serveur de symboles, ou dump auto-symboles
  embarqués via Wheaty).
- ⚠️ **Win32-only** — port futur Linux/Mac client demandera
  `signal()` + `backtrace()` (différent code).

## 9. Recommandation finale

🔧 **Adapter et faire** — **avant launch** :

1. **Étape 1** : adapter `WheatyExceptionReport` cmangos vers
   `WheatyCrashReport` LCDLLN. Win32-only.
2. **Étape 2** : `SetUnhandledExceptionFilter` au boot client.
3. **Étape 3** : filtre PII + format texte sortie.
4. **Étape 4** : (optionnel) upload endpoint serveur avec consentement
   utilisateur.

Effort minimal (S), ROI ops élevé. Peut être livré en parallèle des
autres P3.

---

*Audit du 2026-05-08. Mises à jour : —*
