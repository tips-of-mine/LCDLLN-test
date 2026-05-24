# Backlog gameplay (LCDLLN) — à réaliser plus tard

> Récapitulatif des tâches identifiées au fil du développement (avatar / locomotion /
> combat / nage / interaction / contenu). Mis à jour le 2026-05-23.
> Les sections livrées sont dans `CODEBASE_MAP.md` §28→§40.

## A. Polish marginale (code, faible valeur, à valider en jeu)

- **Crouch — réduction de capsule** : aujourd'hui l'anim accroupie joue mais la capsule
  de collision reste `r=0.3 h=1.8` (pas de passage sous obstacle bas). À ajouter :
  réduire `m_capsule.height` quand `crouch`, + test « puis-je me relever ? » (sweep à
  hauteur debout) avant de quitter l'accroupi. **Risque** : fall-through / rester coincé
  accroupi / jitter du centre — non testable en headless, à faire avec validation en jeu.
- **Emote `/sit` avec Enter/Idle/Exit** : actuellement les emotes jouent un simple loop.
  Pour `/sit` (et clips `Sitting_Enter`/`Idle_Loop`/`Exit`), enchaîner s'asseoir → tenir →
  se relever (au déplacement). Réutiliser le mécanisme de replay de clip en cours d'état
  (`m_avatarPendingClipRole`, cf. §33), avec une phase « loop » intermédiaire et une
  sortie déclenchée par le mouvement (pas par un timer).
- **Combo épée** : enchaînement sur clics rapides (au lieu d'« ignoré pendant l'attaque »).
  Nécessite un 2ᵉ clip de frappe franc (seul `Sword_Attack_RM`, à root-motion, dispo).
- **Touche `TAB` → carte** : quand un système de carte/minimap existera, **réserver `TAB`**
  pour afficher/masquer la carte (toggle). Aujourd'hui aucune carte n'existe ; c'est un
  rappel pour câbler la touche le moment venu (cf. aussi « Rendu de props / meshes
  statiques » et un éventuel système de carte monde en section D).

## B. Refinements mouvement / anim

- **Roll — i-frames** : la roulade a son impulsion (§30) mais pas d'invincibilité ;
  n'a de sens qu'avec un système de dégâts (cf. section serveur).
- **Attaque remappable — UI** : touche alternative livrée par config (§46) ; reste la ligne UI dans Options (Windows-only).

## C. Contenu / assets (hors code — création par level-design / artiste)

- **Meshes d'armes** : `models/equipment/weapons/{magic,one_handed,ranged,two_handed}`
  ne contiennent que des `.gitkeep`. Sans mesh d'arme → pas d'« arme visible » possible.
  Bloque le **système d'équipement** (slots + attache aux os).
- **Vraie étendue d'eau** par zone : un `instances/water.bin` réaliste (basin creusé)
  au lieu de l'eau-test procédurale (cf. §38, §40). **L'éditeur monde produit déjà
  des `water.bin`** (`WaterDocument`/`SaveWaterBin`) — un générateur CLI séparé serait
  redondant (chaîne de link Log/FileSystem). Reste à AUTHORER une vraie étendue (level-design).
- **Entités interactibles visibles** : meshes/props pour les PNJ et objets (le rendu
  3D statique n'existe pas encore côté client — voir §F). Aujourd'hui : marqueurs
  ImGui + hint chat seulement.
- **Arbres de dialogue** : le format dialogue livré est mono-/multi-ligne simple ;
  un vrai système de dialogue (branches, choix, conditions) reste à faire.

## D. Systèmes plus gros (code, à cadrer)

- **Système d'équipement** : slots, attache d'un mesh d'arme à un os (main) du squelette,
  équiper/déséquiper. Dépend des meshes d'armes (section C).
- **Système d'interaction complet** : raycast visée (au lieu de proximité simple),
  prompt UI dédié, loot, déclencheurs d'objets typés, dialogue PNJ branché.
- **Rendu de props / meshes statiques** : passe de rendu pour afficher des objets non-skinnés
  (interactibles, décor) à une position monde — n'existe pas (seul l'avatar skinné est rendu).

## E. Dépend du SERVEUR (redéploiement master/shard)

- **Combat réel** : dégâts, cible/ciblage, application des effets ; réactions
  `Hit_Chest`/`Hit_Head`, mort `Death01`. Nécessite des events serveur.
- **Sync multijoueur** : que les autres joueurs voient nos états (nage, emotes, attaques,
  roulade…) — réplication réseau des états d'animation/action.

## F. Notes techniques

- **Persistance Options** : volume / sensibilité du panneau Options in-game restent
  *session-only* (non sérialisés). Seuls les binds sont persistés (`keybinds.json`, §34/§38… cf. §34).
- **Validation runtime** : tout le gameplay/anim/physique ci-dessus n'est pas testable en
  CI (compilation seulement). Chaque item « feel » nécessite un test en jeu (build Windows).

## G. Character-customization (chantier d'origine)

- **#2 Modèle féminin** : cosmétique client ✅ (§43) + **sélecteur UI de genre in-game** ✅ (§50 : toggle Homme/Femme à la création, aperçu 3D live, 2 genres chargés au boot, persisté client via `character_appearance.json`). **Reste** : **persistance serveur** (DB migration + payload = redéploiement).
- **#5 Textures** : ✅ habit + peau livrés. Set Ranger BaseColor/Normal/ORM (§44) **et** rendu **multi-matériaux** peau/habit (§47 : `MI_Regular_Male` → T_Regular_Male sur les mains, `MI_Ranger` → T_Ranger ailleurs). **Reste** : packer l'ORM peau (Roughness dispo en inbox `source_textures/`) ; textures Female/Peasant/autres races (encore en inbox) ; généraliser par race/genre/tenue.
- **#3 Animations** : ✅ fait et dépassé (§27→§42).
