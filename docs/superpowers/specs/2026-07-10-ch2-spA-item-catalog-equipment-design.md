# Chantier 2 — SP-A : Catalogue d'objets + Équipement — Design

**Date :** 2026-07-10
**Statut :** spec à valider

## Objectif

Donner une identité à chaque objet (type, slot d'équipement, bonus de stats,
apparence) et permettre de l'**équiper depuis l'inventaire**, avec état
d'équipement autoritaire serveur, persistance DB, et recalcul des stats. Objectif
en jeu : équiper un objet ramassé → l'effet est visible **immédiatement** dans
l'inventaire ET sur la fiche (stats). L'apparence 3D sur l'avatar-monde et le
drag paperdoll complet sont des SP ultérieurs (C, D).

## Périmètre (décidé)

SP-A fusionne « catalogue » + « équiper ». Inclus :
- Catalogue d'objets partagé (données) : type, slot, bonus stats, apparence.
- 10 slots d'équipement : 7 visuels (Tête, Torse, Jambes, Pieds, Mains, Arme,
  2ᵈᵉ main) + 3 non visuels (Amulette, Anneau 1, Anneau 2).
- Bonus possible sur n'importe laquelle des **11 stats dérivées** existantes.
- État d'équipement serveur + table DB + persistance.
- Opcodes réseau equip/unequip + snapshot d'équipement (**wire-breaking**).
- Recalcul stats avec contribution d'équipement.
- UI minimale : le panneau gauche « Équipement » de la fenêtre Personnage (F1)
  affiche les slots + l'objet équipé ; équiper/déséquiper via clic (bouton /
  clic droit) sur un objet d'inventaire ou un slot.

Hors périmètre (SP ultérieurs) :
- Apparence 3D de l'équipement sur l'avatar-monde et sur les avatars distants (SP-D + broadcast).
- Drag & drop complet inventaire↔paperdoll (SP-C).
- Instances d'objets uniques (durabilité, enchantements) : **non** — modèle par `itemId` (cf. §4).

## Global Constraints

- C++ (client Windows/Vulkan, serveur Linux master + shardd). Commentaires FR, clarté > brièveté.
- Nouveau code en **PascalCase** (classes/fichiers/dossiers) ; `m_camelCase`/`kPascalCase` conservés.
- Serveur : `character_stats` recalcul **déterministe, sans accès disque** pour les
  tables de stats (embarqué). Le **catalogue d'objets**, lui, peut être lu depuis
  `game/data/` sur le shard (comme les loot tables) — l'important anti-triche est
  que le serveur calcule slot/bonus depuis **sa propre** copie, jamais des valeurs
  fournies par le client.
- `server_app`/`shardd` ne linkent pas `engine_core` : tout `.cpp` partagé ajouté
  doit être listé dans les cibles serveur de `src/CMakeLists.txt` (et racine).
- Bump `kProtocolVersion` (14→15) car nouveaux opcodes + snapshot ⇒ déploiement
  **lock-step master + shardd + client**.

## 1. Catalogue d'objets (partagé)

### 1.1 Modèle de données — `src/shared/items/ItemDefinition.h`

```cpp
namespace engine::items
{
    // Slot d'équipement gameplay. 0 = objet non équipable (consommable, quête…).
    enum class EquipmentSlot : std::uint8_t {
        None = 0,
        Head, Chest, Legs, Feet, Hands, MainHand, OffHand, // 7 visuels
        Amulet, Ring1, Ring2,                              // 3 non visuels
        Count
    };

    // Catégorie d'objet (pour tri/UI ; pas de logique lourde en SP-A).
    enum class ItemType : std::uint8_t {
        Misc = 0, Weapon, Armor, Accessory, Consumable, Quest
    };

    // Les 11 stats dérivées existantes (miroir de shardd DerivedStats).
    // Un bonus d'équipement additionne ces champs à la stat calculée.
    struct StatBonus {
        std::int32_t hp = 0;
        std::int32_t resource = 0;
        std::int32_t damage = 0;
        std::int32_t accuracy = 0;
        float        range = 0.f;
        float        critRate = 0.f;
        float        critMult = 0.f;
        float        speedWalk = 0.f;
        float        speedRun = 0.f;
        float        speedSprint = 0.f;
        std::int32_t stamina = 0;
        std::int32_t perception = 0;
        std::int32_t stealth = 0;
    };

    struct ItemDefinition {
        std::uint32_t id = 0;
        std::string   name;
        std::string   description;
        std::string   iconPath;
        ItemType      type = ItemType::Misc;
        EquipmentSlot slot = EquipmentSlot::None; // None => non équipable
        StatBonus     bonus;                      // additif quand équipé
        // Apparence modulaire (SP-D) : slot visuel + réf. asset. Facultatif ici.
        std::string   visualMesh;                 // vide en SP-A (placeholder)
    };
}
```

*Note : `StatBonus` liste 13 champs (les 11 « dérivées » + resource/stealth) pour
couvrir tout `DerivedStats` ; on garde l'ensemble complet pour éviter un 2ᵉ
passage plus tard. Le mapping `EquipmentSlot` → `EquipVisualSlot` (rendu) est une
fonction utilitaire `ToVisualSlot(EquipmentSlot)` renvoyant `Count` pour les slots
non visuels (Amulet/Ring).*

### 1.2 Chargeur — `src/shared/items/ItemCatalog.{h,cpp}`

```cpp
class ItemCatalog {
public:
    // Charge depuis un contenu JSON (texte). Renvoie false si JSON invalide.
    bool LoadFromJson(const std::string& json);
    const ItemDefinition* Find(std::uint32_t itemId) const; // nullptr si absent
    std::size_t Count() const;
private:
    std::unordered_map<std::uint32_t, ItemDefinition> m_items;
};
```

- Parseur JSON : réutiliser le même utilitaire JSON que `CharacterStatsTables`
  (à identifier lors du plan ; sinon parseur minimal cohérent avec le repo).
- **Client** : charge `game/data/items/items.json` via `FileSystem`.
- **Shard** : charge le même fichier depuis `game/data/items/items.json` au boot.
- Tolérant : un item mal formé est ignoré avec un WARN (comme `inventory_items.txt`).

### 1.3 Fichier de données — `game/data/items/items.json`

Reprend les 4 objets existants de `inventory_items.txt` + exemples équipables :

```json
{
  "items": [
    { "id": 2001, "name": "Épée rouillée", "type": "weapon", "slot": "main_hand",
      "icon": "ui/icons/item_2001.png", "description": "Butin de mêlée de base.",
      "bonus": { "damage": 3 } },
    { "id": 2002, "name": "Potion mineure", "type": "consumable", "slot": "none",
      "icon": "ui/icons/item_2002.png", "description": "Consommable empilable." },
    { "id": 3001, "name": "Relique de quête", "type": "quest", "slot": "none",
      "icon": "ui/icons/item_3001.png", "description": "Récompense de quête." },
    { "id": 4001, "name": "Sceau d'événement", "type": "accessory", "slot": "amulet",
      "icon": "ui/icons/item_4001.png", "description": "Amulette d'événement.",
      "bonus": { "hp": 10, "perception": 2 } },
    { "id": 5001, "name": "Casque de cuir", "type": "armor", "slot": "head",
      "icon": "ui/icons/item_5001.png", "description": "Protège la tête.",
      "bonus": { "hp": 8 } },
    { "id": 5002, "name": "Plastron de cuir", "type": "armor", "slot": "chest",
      "icon": "ui/icons/item_5002.png", "description": "Protège le torse.",
      "bonus": { "hp": 15 } }
  ]
}
```

`inventory_items.txt` reste en place (compat) mais le client privilégie le
catalogue JSON quand un itemId y figure (fallback .txt sinon). *Décision plan :
migrer entièrement puis retirer .txt, ou garder le fallback — à trancher au plan ;
défaut = garder le fallback pour ne rien casser.*

## 2. État d'équipement (serveur autoritaire)

Par personnage : `equipment[EquipmentSlot] = itemId` (0 = vide). Vit dans
`ConnectedClient` côté `shardd` (à côté de `inventory` = `vector<ItemStack>`).

```cpp
// 10 slots utiles (indices 1..10 ; 0 = None non stocké).
std::array<std::uint32_t, /*Count-1*/ 10> equipment{}; // itemId par slot
```

### 2.1 Logique équiper / déséquiper (serveur)

- **Équiper `itemId`** :
  1. Résoudre `def = catalog.Find(itemId)`. Rejeter si absent ou `def.slot == None`.
  2. Vérifier que le joueur **possède** `itemId` (inventaire, quantity ≥ 1).
  3. Slot cible = `def.slot`. Si un objet y est déjà équipé, il est **déséquipé**
     (rendu à l'inventaire) d'abord.
  4. Décrémenter l'inventaire de 1 (retirer le stack si quantity tombe à 0).
  5. `equipment[slot] = itemId`.
  6. Recalculer stats (avec bonus), envoyer `PlayerStats` + `EquipmentUpdate` +
     `InventoryDelta` ; sauvegarder.
- **Déséquiper `slot`** :
  1. `itemId = equipment[slot]` ; no-op si vide.
  2. `equipment[slot] = 0` ; rendre `itemId` à l'inventaire (+1 / nouveau stack).
  3. Recalcul stats + `EquipmentUpdate` + `InventoryDelta` + save.

Anti-triche : slot et bonus proviennent **toujours** du catalogue serveur ; le
client n'envoie qu'un `itemId` (équiper) ou un indice de `slot` (déséquiper).

## 3. Recalcul des stats avec équipement

Point d'extension : `ComputeStats(tables, factionId, classId, sex, level)`
(`src/shardd/gameplay/character/CharacterStatsEngine.{h,cpp}`).

- Nouvelle surcharge / paramètre : `ComputeStats(..., const StatBonus& equipmentBonus)`
  où `equipmentBonus` = somme des `def.bonus` de tous les slots équipés.
- La contribution s'**additionne** aux stats dérivées après interpolation
  classe/race/sexe (ex. `derived.hp += equipmentBonus.hp`). Clamp à ≥ 0.
- Appelé à : boot (avec l'équipement chargé de la DB), equip, unequip, level-up.
- `SendPlayerStats` inchangé côté wire (11 stats) — seules les valeurs changent.

## 4. Modèle « pas d'instances »

Un objet équipé est identifié par son `itemId` seul (pas d'instanceId, pas de
durabilité). Conséquences assumées :
- Deux exemplaires du même `itemId` sont interchangeables.
- Équiper = déplacer 1 unité inventaire → slot ; déséquiper = l'inverse.
- Suffisant pour SP-A ; les instances uniques (enchant/durabilité) seront un SP
  dédié si le design du jeu l'exige.

## 5. Persistance (shardd)

- **Table DB** (SQLite persistance shard) — nouvelle migration
  `game/data/persistence/db/migrations/0002_character_equipment.sql` :
  ```sql
  CREATE TABLE IF NOT EXISTS character_equipment (
      character_key INTEGER NOT NULL,
      slot          INTEGER NOT NULL,     -- valeur EquipmentSlot (1..10)
      item_id       INTEGER NOT NULL,
      PRIMARY KEY (character_key, slot),
      FOREIGN KEY (character_key) REFERENCES characters(character_key)
  );
  ```
- **Sérialisation KV** (chemin réel utilisé par `CharacterPersistence`) : ajouter
  `equipment.count`, `equipment.N.slot`, `equipment.N.item_id` à côté du bloc
  `inventory.*` (`CharacterPersistence.cpp`). Charger au boot → remplir
  `ConnectedClient.equipment` avant le 1ᵉʳ recalcul stats.

## 6. Réseau (wire-breaking → bump protocole 14→15)

`src/shared/network/ServerProtocol.h` :

- **Nouveaux opcodes** :
  - `EquipRequest` (client→shard) : `{ clientId, action:uint8 (0=equip,1=unequip),
    itemId:uint32, slot:uint8 }`. Pour equip, `itemId` compte ; pour unequip,
    `slot` compte.
  - `EquipmentUpdate` (shard→client) : snapshot complet
    `{ clientId, count:uint8, [slot:uint8, itemId:uint32]×count }`.
- `Encode/DecodeEquipRequest`, `Encode/DecodeEquipmentUpdate` (miroir des helpers
  inventaire existants).
- Handler shard `HandleEquipRequest` (près de `PickupRequest`), envoi
  `EquipmentUpdate` après mutation. Master : relai/route si nécessaire (equip est
  du ressort shardd, gameplay).
- Bump `kProtocolVersion` 14 → 15.

## 7. Client

- **Chargeur catalogue** : `ItemCatalog` chargé au boot client ; `InventoryUi`
  résout nom/icône/infobulle via le catalogue (fallback `inventory_items.txt`).
  Infobulle enrichie : type, slot, bonus de stats (lignes « +8 PV », « +3 Dégâts »…).
- **Modèle client** : `UIModel.equipment` = `std::array<uint32_t,10>` alimenté par
  `EquipmentUpdate` (comme `inventory` par `InventoryDelta`). Flag de changement
  `UIModelChangeEquipment`.
- **Panneau Équipement** (fenêtre Personnage F1, panneau gauche « Équipement —
  Chantier 2 ») : afficher les 10 slots (icône de l'objet équipé ou slot vide +
  libellé). Clic sur un slot équipé → déséquiper (envoi `EquipRequest` unequip).
  Clic droit / bouton « Équiper » sur un objet d'inventaire équipable → envoi
  `EquipRequest` equip. (Le drag complet = SP-C.)
- **Envoi réseau** : ajouter l'émission `EquipRequest` via le client gameplay
  (là où `PickupRequest` est émis).

## 8. Tests

- `ItemCatalog` : parse JSON valide (6 items), Find hit/miss, item malformé ignoré.
- Logique equip/unequip (extraire en fonction pure testable serveur) : équiper
  déplace l'unité, remplace un slot occupé (rend l'ancien), déséquiper rend
  l'objet ; rejet si non possédé / non équipable / slot None.
- `StatBonus` : somme des bonus de plusieurs slots ; `ComputeStats` additionne
  correctement (ex. hp de base + bonus).
- `ToVisualSlot` : 7 slots → EquipVisualSlot correspondant, 3 accessoires → Count.

## 9. Découpage en PRs (server-first)

1. **PR1 (shared+serveur, ⚠️ wire-breaking)** : `ItemDefinition`/`ItemCatalog` +
   `items.json` + état équipement `ConnectedClient` + logique equip/unequip +
   bonus dans `ComputeStats` + migration DB + persistance KV + opcodes
   `EquipRequest`/`EquipmentUpdate` + bump protocole 14→15 + tests serveur.
   *(déployé seul, le client ancien ignore les nouveaux opcodes ; à mettre en
   lock-step avec PR2)*
2. **PR2 (client)** : `ItemCatalog` client + infobulle enrichie + `UIModel.equipment`
   + décodage `EquipmentUpdate` + panneau Équipement (F1) + émission `EquipRequest`.

Merge **lock-step** : master + shardd (PR1) et client (PR2) déployés ensemble
(protocole 15). Un client 14 ne peut pas parler à un serveur 15.

## Déploiement

⚠️ **REDÉPLOIEMENT SERVEUR REQUIS (master + shardd) en lock-step avec le client** :
nouveaux opcodes `EquipRequest`/`EquipmentUpdate`, bump `kProtocolVersion` 14→15,
migration DB `0002_character_equipment.sql`, nouveau handler shard. Un client neuf
ne peut pas parler à un serveur ancien (et inversement).
