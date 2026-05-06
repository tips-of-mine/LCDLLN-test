# CHAR-MODEL.12 — Sockets / attachements à des os nommés

## Dépendances
- CHAR-MODEL.2 (`Skeleton`, `ComputeModelMatrices`)

## Cadrage

Implémenter le système de **sockets** : un point d'attache nommé,
identifié par un nom de socket logique (`mainR`, `mainL`, `dos`,
`ceinture`, `casque`, `dosCheval`, …) qui résout vers un os du squelette
**plus** un offset local (Mat4). Permet d'attacher des objets (armes,
casques, monture) à un personnage en suivant l'animation.

---

## Pré-requis vérifiables

```bash
git status
ls engine/render/Skeleton.h
```

---

## Spécification technique

### Modèle

```cpp
// engine/render/AttachmentSocket.h
namespace engine::render
{
    /// Point d'attache logique sur un squelette.
    struct AttachmentSocket
    {
        std::string         name;           // ex. "mainR"
        uint16_t            jointIndex;     // résolu via Skeleton
        engine::math::Mat4  localOffset;    // Mat4 par rapport à l'os
    };

    /// Catalogue de sockets associé à un squelette particulier.
    /// Chargé depuis un fichier .sockets (JSON simple, lisible humain).
    class SocketCatalog
    {
    public:
        bool LoadFromFile(std::string_view path, const Skeleton& skeleton);

        /// Récupère un socket par nom logique. nullptr si absent.
        const AttachmentSocket* Find(std::string_view name) const;

        std::span<const AttachmentSocket> All() const;

    private:
        std::vector<AttachmentSocket>                        m_sockets;
        std::unordered_map<std::string, uint32_t /*index*/>  m_byName;
    };

    /// Calcule la matrice monde d'un socket attaché à un squelette dont
    /// les matrices model-space sont déjà calculées.
    /// modelMatrix = matrice monde de l'entité hôte.
    engine::math::Mat4 ComputeSocketWorldMatrix(
        const AttachmentSocket& socket,
        std::span<const engine::math::Mat4> jointModelMatrices,
        const engine::math::Mat4& modelMatrix);
}
```

### Format `.sockets` (JSON)

Lisible humain, éditable à la main :

```json
{
  "skeleton": "humanoid_v1",
  "sockets": [
    {
      "name": "mainR",
      "joint": "hand_r",
      "offset": {
        "translation": [0.02, 0.0, 0.0],
        "rotation":    [0, 0, 0, 1],
        "scale":       [1, 1, 1]
      }
    },
    {
      "name": "casque",
      "joint": "head",
      "offset": { "translation": [0,0.05,0], "rotation":[0,0,0,1], "scale":[1,1,1] }
    }
  ]
}
```

### Sockets canoniques humanoid_v1 (à pré-livrer en CHAR-MODEL.14)

`mainR`, `mainL`, `dos`, `ceinture`, `casque`. (Le socket `dosCheval`
sera défini sur le rig quadrupède en CHAR-MODEL.27/29.)

### Validation au load

- `skeleton` matchant le squelette passé.
- Chaque `joint` résolu en `jointIndex` valide (sinon erreur).
- Quaternion d'offset normalisé (sinon renormalise + warning).

---

## Liste des fichiers

**Créés :**
- `engine/render/AttachmentSocket.h` + `.cpp`
- `tests/render/AttachmentSocket_LoadAndResolve_test.cpp`
- `tests/render/AttachmentSocket_WorldMatrix_test.cpp`

**Modifiés :**
- `CMakeLists.txt`

---

## CMakeLists.txt

```cmake
target_sources(engine_core PRIVATE
    engine/render/AttachmentSocket.h
    engine/render/AttachmentSocket.cpp
)
```

---

## Critères d'acceptation

- [ ] Build Windows + Linux propre.
- [ ] Test `AttachmentSocket_LoadAndResolve_test` : charge un fichier
      `.sockets` avec 3 sockets, `Find("mainR")` retourne un pointeur
      valide, `Find("inexistant")` retourne `nullptr`.
- [ ] Test `AttachmentSocket_WorldMatrix_test` : pour un squelette à
      1 os à la position (10,0,0), un socket à `localOffset=identité`,
      `ComputeSocketWorldMatrix` retourne (10,0,0) en composante de
      translation.
- [ ] Le rejet d'un fichier `.sockets` qui référence un os inexistant
      est explicite (log + retourne false).

---

## Anti-objectifs

- **Ne pas** câbler les rendus d'objets attachés (responsabilité du
  call-site : reçoit la `Mat4` socket et l'utilise comme `modelMatrix`
  pour rendre l'objet).
- **Ne pas** définir les sockets canoniques ici (livrés avec chaque rig).
- **Ne pas** introduire de hiérarchie de sockets (les sockets ne
  peuvent pas s'attacher à d'autres sockets, uniquement à des os).
