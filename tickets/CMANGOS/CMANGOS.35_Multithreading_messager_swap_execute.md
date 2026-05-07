# CMANGOS.35_Multithreading_messager_swap_execute

## Objectif

Implémenter le pattern **`Messager`** inspiré de
`src/shared/Multithreading` cmangos. Pattern : au lieu de protéger un
état partagé par mutex prolongé, le thread propriétaire vide
périodiquement une queue de fonctions qui modifient son état localement
("swap-and-execute"). Élimine la contention.

Cas d'usage LCDLLN :
- master → shard : « exécute ceci dans ton tick »
- IO thread → game thread : « voici un paquet à dispatcher »
- shard A → shard B : « migre ce joueur »

C'est un **P3 cross**.

## Dépendances

- M00.1 (build base)

## Livrables

### Couche partagée (`engine/core/multithreading/`)

- `Messager.{h,cpp}` (templated) :
  ```cpp
  template <class Owner>
  class Messager {
  public:
    using Message = std::function<void(Owner&)>;
    void Send(Message msg);          // appelable depuis n'importe quel thread
    void Execute(Owner& owner);      // doit être appelé par owner thread uniquement
  private:
    std::mutex m_mutex;
    std::vector<Message> m_pending;
    std::vector<Message> m_local;    // swap target (réutilisé pour éviter alloc)
  };
  ```

### Tests

- `MessagerTests.cpp` :
  - Multi-thread push 1000 messages → owner thread `Execute` les exécute tous.
  - Verify : owner state mutated correctement.

## Structure & chemins (verrouillé)

- Code moteur : uniquement sous `/engine`
- ❌ Interdit : créer un dossier racine non autorisé

## Spécification technique

### Swap-and-execute

```cpp
template <class Owner>
void Messager<Owner>::Send(Message msg) {
  std::lock_guard lk(m_mutex);
  m_pending.push_back(std::move(msg));
}

template <class Owner>
void Messager<Owner>::Execute(Owner& owner) {
  m_local.clear();
  {
    std::lock_guard lk(m_mutex);
    std::swap(m_pending, m_local);     // O(1), held lock briefly
  }
  for (auto& msg : m_local) {
    msg(owner);                        // exécuté hors lock
  }
}
```

Le lock est tenu uniquement pendant le swap (très court). L'exécution
des fonctions se fait hors lock — pas de blocage des producteurs.

### Usage type

```cpp
// shard A
g_shardB.Messager().Send([playerData](Shard& dest) {
  dest.AddIncomingPlayer(playerData);
});

// shard B (owner thread):
void Shard::Tick() {
  m_messager.Execute(*this);    // applique les Send() reçus
  // ... logique normale du tick
}
```

## Étapes d'implémentation

1. Créer `engine/core/multithreading/Messager.{h,cpp}`.
2. Tests basiques + multi-thread.
3. Câbler côté shard : chaque Map a un Messager pour réception cross-thread.
4. Câbler côté master : Messager pour réception depuis IO thread.
5. Doc : section « Messager pattern » dans `CODEBASE_MAP.md`.

## Definition of Done (DoD)

- [ ] Build Linux OK
- [ ] Tests multi-thread passent (1000 producers × 100 messages)
- [ ] Smoke test : un Map reçoit un message cross-thread et l'applique au tick
- [ ] Aucun dossier racine non autorisé
- [ ] Rapport final

## Notes / pièges à éviter

- **Owner-only Execute** : `Execute` n'est appelable que par le thread propriétaire. Si appelé par un autre, race sur `m_local`. Documenter clairement.
- **Reentrancy** : si une Message contient un `Send` vers la même Messager, le nouveau message attendra le prochain `Execute` (correct — pas de pile).
- **`std::function` cost** : une allocation heap par Send (sauf SBO). Pour les hot paths, considérer un `inplace_function<sizeof(void*)*4>` ou un pool.
- **Drop on shutdown** : à la fermeture, drainer la queue (`Execute` une dernière fois) avant de détruire l'owner.

## Références

- `CMANGOS_ANALYSIS.md` § Multithreading (P3 cross)
- cmangos `src/shared/Multithreading/Messager.h`
