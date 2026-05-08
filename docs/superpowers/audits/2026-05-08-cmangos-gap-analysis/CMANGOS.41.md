# CMANGOS.41 — Util (ByteBuffer / PCQueue / Trackable)

> **Ticket source** : [tickets/CMANGOS/CMANGOS.41_Util_bytebuffer_pcqueue_trackable.md](../../../../tickets/CMANGOS/CMANGOS.41_Util_bytebuffer_pcqueue_trackable.md)
> **Priorité** : P3 — ajout à valeur (utilitaires C++)
> **Cible** : cross (master + shard)

## 1. Statut implémentation

🟡 **Partiel** — équivalent `ByteReader`/`ByteWriter` côté LCDLLN dans
`engine/network/`. `ProducerConsumerQueue` et `UniqueTrackablePtr`
absents.

## 2. Preuves dans le code

**Existant :**
- [engine/network/ByteReader.h](../../../../engine/network/ByteReader.h) + [engine/network/ByteWriter.h](../../../../engine/network/ByteWriter.h) —
  équivalent fonctionnel `ByteBuffer` (sérialisation typed)
- M00.3 — Job System (couvre une partie de PCQueue)

**Manquant :**
- ❌ `ByteBuffer` cmangos avec gestion **bit-level** (bits flag packing)
- ❌ `ProducerConsumerQueue<T>` générique avec `WaitAndPop` condition
  variable
- ❌ `UniqueTrackablePtr` smart pointer non-atomic compteur faible

## 3. Recouvrement milestones existantes

✅ **Couvert (partiellement)** — `ByteReader/Writer` couvre la
sérialisation. PCQueue couvert partiellement par Job System. Trackable
non couvert.

## 4. Écart par rapport à la spec CMANGOS

L'écart est **modéré** :

1. **ByteBuffer bit-level** — utile pour packing efficace (flags 1-bit).
   `ByteReader/Writer` actuels gèrent byte-aligned uniquement (à
   confirmer).
2. **PCQueue générique** — fondation classique multithreading. Job
   System différent (pool workers + tâches), PCQueue est plus simple et
   réutilisable.
3. **UniqueTrackablePtr** — pattern niche. Sans coût atomique vs
   `shared_ptr`. Utile pour tracker des entités monde sans cycle.

## 5. Effort estimé

**S-M** (1-2 PR) — utilitaires templated indépendants. Pas wire-breaking,
pas DB.

## 6. Valeur joueur/serveur

**Faible-Moyenne** — invisible joueur. Gain ergonomique +
performance ponctuelle.

## 7. Dépendances bloquantes

Aucune.

## 8. Risque / piège ⚠️

- ⚠️ **Doublon `ByteReader/Writer` vs `ByteBuffer`** — éviter divergence.
  Si on adopte ByteBuffer cmangos, migrer le code existant ou conserver
  l'existant + ajouter bit-level support.
- ⚠️ **`UniqueTrackablePtr` thread-safety** — semantique précise (qui
  invalide les weak refs ? quand ?). Bug ici = use-after-free.
- ⚠️ **PCQueue performance** — implémentation naïve OK, mais bench si
  hot path.

## 9. Recommandation finale

⏸ **Reporter** ou faire opportunément :

1. **Étape 1 (opportune)** : étendre `ByteReader/Writer` avec bit-level
   si besoin émerge (packing flags compact).
2. **Étape 2 (opportune)** : implémenter `ProducerConsumerQueue<T>` à la
   première situation où Job System est trop lourd.
3. **Étape 3 (skip)** : `UniqueTrackablePtr` à n'introduire que si
   profilage révèle contention `shared_ptr`.

Pas urgent. Effort S-M, ROI faible-moyen.

---

*Audit du 2026-05-08. Mises à jour : —*
