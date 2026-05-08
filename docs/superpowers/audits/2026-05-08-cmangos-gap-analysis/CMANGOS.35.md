# CMANGOS.35 — Multithreading (Messager / swap-and-execute)

> **Ticket source** : [tickets/CMANGOS/CMANGOS.35_Multithreading_messager_swap_execute.md](../../../../tickets/CMANGOS/CMANGOS.35_Multithreading_messager_swap_execute.md)
> **Priorité** : P3 — ajout à valeur (concurrence)
> **Cible** : cross (master + shard)

## 1. Statut implémentation

❌ **Absent** — pas de pattern `Messager` swap-and-execute. M00.3
(Job System thread pool) couvre **un autre pattern** (jobs queue).

## 2. Preuves dans le code

**Existant (orthogonal) :**
- M00.3 — Job System thread pool queues + job groups (jobs offload,
  pas swap-and-execute)

**Manquant :**
- ❌ `engine/core/multithreading/Messager` templated
- ❌ Pattern swap-and-execute pour cross-thread state mutation

## 3. Recouvrement milestones existantes

❌ **Non couvert** — Job System couvre offload, pas swap-and-execute.
Patterns différents.

## 4. Écart par rapport à la spec CMANGOS

100% absent. Pattern utile pour :
- master → shard : "exécute ceci dans ton tick"
- IO thread → game thread : dispatch paquet
- shard A → shard B : migration joueur cross-shard

Sans `Messager`, alternatives ad hoc (mutex partagé, atomic flags,
queues custom) → dette + bugs subtils.

## 5. Effort estimé

**S-M** (1-2 PR) — pattern templated simple : queue + swap + drain.
Pas wire-breaking, pas migration DB.

## 6. Valeur joueur/serveur

**Moyenne** — invisible joueur, gain architectural. Devient critique
quand le code multithread se complexifie (multi-shard, migration
cross-shard).

## 7. Dépendances bloquantes

Aucune — peut être livré tôt.

## 8. Risque / piège ⚠️

- ⚠️ **Lock-free vs mutex** — implémentation triviale = mutex protégeant
  std::vector. Lock-free = plus complexe. Démarrer mutex.
- ⚠️ **Lambda capture by value** — éviter les captures par référence
  qui dangling après cross-thread.
- ⚠️ **Drain timing** — si trop fréquent, contention. Si pas assez,
  latence. À profiler par cas d'usage.
- ⚠️ **Owner thread affinity** — chaque Messager appartient à un
  thread. Bug subtil si le owner change.

## 9. Recommandation finale

🔧 **Adapter et faire**, **avant** CMANGOS.19 Maps + CMANGOS.07 AI
(qui auront besoin de cross-thread mutations) :

1. **Étape 1** : `Messager` templated basic (queue + drain) + tests.
2. **Étape 2** : intégration premier cas d'usage (probablement
   migration entité entre Maps quand .19 livré).

Effort minimal, ROI architectural. Bonne candidate pour livraison
opportuniste.

---

*Audit du 2026-05-08. Mises à jour : —*
