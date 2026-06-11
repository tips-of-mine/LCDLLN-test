# Combat SP3 — Proposition de kits starter par profil (à valider)

> Statut : **PROPOSITION** soumise à validation utilisateur (décision SP3 :
> « kits starter par profil, validés avant implémentation »). Rien n'est
> implémenté tant que ce document n'est pas validé/amendé.

## 1. Principes de conception

- **8 profils** (cf. design système de personnages §6.3) : melee, tank,
  distance, voleur, healer, sacre, lanceur, pisteur. Chaque **classe** hérite du
  kit de son profil (déclinaison cosmétique par classe — noms/FX — possible plus
  tard, la mécanique est par profil).
- **3 sorts par profil** en V1 : un cœur de rotation (dégât/soin), un effet
  périodique (DoT/HoT), un utilitaire (buff/debuff/taunt). L'auto-attaque (SP2)
  reste la base du DPS.
- **Les dégâts/soins scalent sur la stat `damage`** du personnage (déjà calculée
  par niveau/classe/race/sexe) via un multiplicateur — aucun nouveau tuning par
  niveau à maintenir.
- **Coûts en % de la ressource max** (la ressource secondaire de la classe :
  ferveur, souffle, corruption…) — scalent automatiquement avec le niveau.
- **Types d'effets V1** (tous implémentables côté serveur sans nouveau système) :
  1. `DirectDamage(mult)` — dégâts immédiats = mult × damage (jets précision/crit SP2 réutilisés)
  2. `DamageOverTime(multPerTick, period, duration)` — aura DoT (Aura::Tick existant)
  3. `DirectHeal(mult)` — soin immédiat = mult × damage
  4. `HealOverTime(multPerTick, period, duration)` — aura HoT
  5. `BuffDamagePercent(+X %, duration)` — buff de dégâts sur soi (multiplicateur attaquant)
  6. `DebuffDamageTakenPercent(+X %, duration)` — la cible subit +X % de dégâts
  7. `TauntThreatMult(×N)` — menace instantanée (ThreatList existant)
  8. `SlowMobPercent(-X %, duration)` — ralentit le `moveSpeed` du mob (serveur-autoritaire)
- **Exclu V1 (volontairement)** : modification de vitesse du **joueur**
  (mouvement client-autoritaire — désync), effets positionnels (dans le dos),
  absorptions/boucliers, CC durs (stun/root), points de combo (les
  combo_builder/spender de test seront migrés quand le profil voleur passera
  aux combos — V2).

## 2. Les kits (3 sorts × 8 profils)

Format : **Nom** — effet ; cast ; coût ; cooldown ; portée.

### melee (guerrier, tourmenteur, dragonnier…)
| Sort | Effet | Cast | Coût | CD | Portée |
|---|---|---|---|---|---|
| Frappe brutale | DirectDamage ×1.4 | instant | 15 % | 6 s | mêlée |
| Entaille | DoT ×0.25/2 s pendant 8 s | instant | 20 % | 10 s | mêlée |
| Cri de guerre | BuffDamagePercent +15 % / 10 s | instant | 25 % | 30 s | soi |

### tank (gardien_ecailles, brise_roc)
| Sort | Effet | Cast | Coût | CD | Portée |
|---|---|---|---|---|---|
| Provocation | TauntThreatMult ×3 (menace immédiate) | instant | 10 % | 8 s | 10 m |
| Coup de bouclier | DirectDamage ×1.0 + SlowMob −30 % / 4 s | instant | 20 % | 12 s | mêlée |
| Second souffle | HoT 3 % PV max/2 s pendant 10 s | instant | 30 % | 30 s | soi |

### distance (archer, arbaletrier, archer_bois)
| Sort | Effet | Cast | Coût | CD | Portée |
|---|---|---|---|---|---|
| Tir visé | DirectDamage ×1.6 | 1.5 s | 20 % | 8 s | 30 m |
| Flèche barbelée | DoT ×0.3/2 s pendant 8 s | instant | 20 % | 12 s | 30 m |
| Tir rapide | DirectDamage ×0.8 | instant | 15 % | 3 s | 30 m |

### voleur (assassin, voleur_tenebreux)
| Sort | Effet | Cast | Coût | CD | Portée |
|---|---|---|---|---|---|
| Attaque sournoise | DirectDamage ×1.7 | instant | 25 % | 10 s | mêlée |
| Lame empoisonnée | DoT ×0.35/2 s pendant 10 s | instant | 20 % | 12 s | mêlée |
| Dérobade | menace −50 % sur tous les mobs engagés | instant | 20 % | 25 s | soi |

### healer (hospitalier, pretre_grace)
| Sort | Effet | Cast | Coût | CD | Portée |
|---|---|---|---|---|---|
| Soin rapide | DirectHeal ×2.0 | 1.5 s | 25 % | — | 30 m (allié/soi) |
| Rémission | HoT ×0.5/2 s pendant 10 s | instant | 25 % | 8 s | 30 m (allié/soi) |
| Châtiment | DirectDamage ×1.0 | 1.5 s | 15 % | 4 s | 30 m |

### sacre (paladin)
| Sort | Effet | Cast | Coût | CD | Portée |
|---|---|---|---|---|---|
| Marteau sacré | DirectDamage ×1.3 | instant | 15 % | 6 s | mêlée |
| Imposition des mains | DirectHeal ×3.0 | instant | 40 % | 60 s | soi/allié 10 m |
| Aura de zèle | BuffDamagePercent +10 % / 12 s | instant | 20 % | 24 s | soi |

### lanceur (mage, archimage, demoniste, chaman, menthats…)
| Sort | Effet | Cast | Coût | CD | Portée |
|---|---|---|---|---|---|
| Trait de feu | DirectDamage ×1.6 | 1.5 s | 20 % | 4 s | 30 m |
| Brûlure | DoT ×0.35/2 s pendant 8 s | instant | 20 % | 10 s | 30 m |
| Nova | DirectDamage ×0.9 en zone (AreaAroundSelf 8 m) | instant | 30 % | 15 s | soi (AoE) |

### pisteur (pisteur orc/nain)
| Sort | Effet | Cast | Coût | CD | Portée |
|---|---|---|---|---|---|
| Tir de chasse | DirectDamage ×1.5 | 1.5 s | 20 % | 8 s | 30 m |
| Marque du chasseur | DebuffDamageTaken +15 % / 10 s | instant | 15 % | 12 s | 30 m |
| Morsure du piège | DoT ×0.3/2 s pendant 8 s + SlowMob −20 % / 4 s | instant | 25 % | 15 s | 25 m |

## 3. Ce que SP3 devra livrer techniquement (rappel spec §6, affiné)

- **Données** : `game/data/gameplay/spells/<profil>.json` (schéma calé sur
  `SpellTemplate` + extensions : `resourceCostPercent`, `effects[]` typés
  ci-dessus, `healMult`/`damageMult`). Le serveur charge dans `SpellMgr`
  (existant) ; le client charge le même fichier pour la barre d'action
  (nom/coût/cd/icône).
- **Serveur** : pipeline de cast par joueur (cast time, interruption au
  mouvement, coût en ressource — la ressource courante devient une valeur
  runtime serveur, aujourd'hui seule la max est calculée + régénération simple
  % / s à définir) ; application des effets ; auras répliquées (apply/remove)
  → `BuffBarPresenter` ; persistance `character_auras` (0061).
- **Wire** : kind CastRequest (sortId), CastResult/CastBar, AuraUpdate —
  bump v10→v11.
- **Client** : barre d'action (touches 1-4), barre de cast, BuffBar câblée.
- **Question ouverte n°4 (régén)** : proposition — régénération de ressource
  5 %/s hors combat, 2 %/s en combat (modifiable par data ensuite).

## 4. Questions à valider

1. Les **8 kits** ci-dessus (noms, effets, valeurs) — amendements bienvenus,
   les valeurs sont des points de départ d'équilibrage, modifiables en data.
2. **Coûts en % de la ressource max** : OK ?
3. **Exclusions V1** (pas de vitesse joueur, pas de stun/root, combos différés) : OK ?
4. **Régénération de ressource** : 5 %/s hors combat / 2 %/s en combat ?
5. **Touches** : sorts sur **1-4** (la barre d'action affiche le kit du profil) ?
