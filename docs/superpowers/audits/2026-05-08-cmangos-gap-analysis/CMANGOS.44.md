# CMANGOS.44 — AuctionHouseBot (internal client low-pop)

> **Ticket source** : [tickets/CMANGOS/CMANGOS.44_AuctionHouseBot_internal_client_low_pop.md](../../../../tickets/CMANGOS/CMANGOS.44_AuctionHouseBot_internal_client_low_pop.md)
> **Priorité** : P4 — cas spécifique (conditionnel)
> **Cible** : master

## 1. Statut implémentation

❌ **Absent**.

## 2. Preuves dans le code

**Manquant :**
- ❌ `engine/server/auction/bot/AuctionHouseBot`
- ❌ Tick périodique post + bid simulés
- ❌ Config `auction.bot.enabled`, `auction.bot.target_listings`

## 3. Recouvrement milestones existantes

❌ **Non couvert**.

## 4. Écart par rapport à la spec CMANGOS

100% absent. Bot interne pour simuler économie active sur serveur
low-pop.

## 5. Effort estimé

**S-M** (1-2 PR) — bot simple posant/buyant items selon distribution
configurée.

## 6. Valeur joueur/serveur

**Conditionnelle** — utile **uniquement** si :
- Le serveur tombe en low-pop (peu de joueurs alimentent le HV)
- Le HV doit rester actif pour engagement
- Sinon, ignorer.

## 7. Dépendances bloquantes

- **CMANGOS.09 AuctionHouse** (V2 ou V1 M35.4)
- **CMANGOS.13 Database**

## 8. Risque / piège ⚠️

- ⚠️ **Détection joueurs** — si bot trop évident, perte de confiance
  joueur. Patterns aléatoires.
- ⚠️ **Inflation/déflation** — bot mal calibré peut casser l'économie.
  Bornes prix strictes.
- ⚠️ **Conflit avec économie réelle** — si bot achète tout, joueurs
  frustrés.

## 9. Recommandation finale

🚫 **Ne pas faire** par défaut. **À activer uniquement** si :
1. Serveur live + low-pop avéré (< X joueurs concurrents simultanés)
2. Décision business explicite "garder HV actif coûte que coûte"

À garder en référence pour activation future conditionnelle.

---

*Audit du 2026-05-08. Mises à jour : —*
