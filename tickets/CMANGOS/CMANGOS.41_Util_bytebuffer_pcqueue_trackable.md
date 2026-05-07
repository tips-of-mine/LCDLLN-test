# CMANGOS.41_Util_bytebuffer_pcqueue_trackable

## Objectif

Mettre en place 3 utilitaires C++ inspirés de `src/shared/Util` cmangos :

1. **`ByteBuffer`** : sérialisation packet ergonomique avec
   `operator<<` / `operator>>` typés, position read/write séparées,
   gestion bit-level. Si nos sérialisations actuelles utilisent
   `memcpy` brut, c'est un upgrade ergonomique majeur.
2. **`ProducerConsumerQueue<T>`** : queue thread-safe générique avec
   `WaitAndPop` (condition variable). Réutilisable pour file de logs
   async, file de tasks DB, message bus interne.
3. **`UniqueTrackablePtr`** : smart pointer avec compteur de
   références faibles (sans coût atomique d'un `shared_ptr`). Utile
   pour tracker des entités du monde sans cycle.

C'est un **P3 cross**.

## Dépendances

- M00.1 (build base)

## Livrables

### Couche partagée (`engine/core/util/`)

- `ByteBuffer.{h,cpp}` :
  ```cpp
  class ByteBuffer {
  public:
    ByteBuffer();
    explicit ByteBuffer(size_t reserve);

    // Write
    template <typename T> ByteBuffer& operator<<(T value);
    ByteBuffer& operator<<(std::string_view s);
    void WriteBit(bool b);
    void FlushBits();

    // Read
    template <typename T> ByteBuffer& operator>>(T& value);
    bool ReadBit();
    std::string ReadString();

    // Positions
    size_t WritePos() const;
    size_t ReadPos() const;
    void SeekRead(size_t pos);
    std::span<const uint8_t> Data() const;
    bool HasError() const;            // overflow read
  };
  ```
- `ProducerConsumerQueue.{h,cpp}` (templated header-only) :
  ```cpp
  template <class T>
  class ProducerConsumerQueue {
  public:
    void Push(T item);
    bool TryPop(T& out);
    bool WaitAndPop(T& out, std::chrono::milliseconds timeout = ...);
    void Cancel();    // unblock all WaitAndPop
    size_t Size() const;
  private:
    std::mutex m_mtx;
    std::condition_variable m_cv;
    std::deque<T> m_q;
    bool m_cancelled = false;
  };
  ```
- `UniqueTrackablePtr.{h,cpp}` (templated) :
  ```cpp
  template <class T>
  class TrackerRef {
  public:
    T* Get() const;            // nullptr si target détruit
    explicit operator bool() const;
  };
  template <class T>
  class UniqueTrackablePtr {
  public:
    explicit UniqueTrackablePtr(T* obj);
    ~UniqueTrackablePtr();
    TrackerRef<T> Track() const;
    T* Get() const;
  };
  ```

### Tests

- `ByteBufferTests.cpp` — round-trip read/write de tous les types ;
  bit-level write/read aligné ; détection overflow.
- `ProducerConsumerQueueTests.cpp` — N producers + M consumers, pas de
  perte de message ; cancel libère les waiters.
- `UniqueTrackablePtrTests.cpp` — Tracker reste valide tant que owner
  vit, devient null après destruction.

## Structure & chemins (verrouillé)

- Code moteur : uniquement sous `/engine`
- ❌ Interdit : créer un dossier racine non autorisé

## Spécification technique

### 1. ByteBuffer endianness

Little-endian sur le wire (cohérence avec la couche réseau actuelle).
Wrapper sur `std::vector<uint8_t>`. Ne pas utiliser `memcpy` direct côté
appelant ; passer par `<<`.

### 2. Bit-level

Utile pour les flags compactés (cmangos l'utilise pour les masques de
mouvement). Les bits sont accumulés dans un buffer interne de 8 bits ;
`FlushBits` pad à octet entier.

### 3. UniqueTrackablePtr design

Inspiré de cmangos : un `Tracker` partagé entre l'owner et les
TrackerRef. À la destruction de l'owner, le Tracker est marqué "expired".
Coût : 1 ptr + 1 atomic bool, vs `shared_ptr<weak_ptr>` 2 atomics +
control block.

Cas d'usage : un `Spell` cible un `Unit`. Si l'`Unit` meurt avant que le
sort se résolve, le `TrackerRef<Unit>` retourne nullptr — on évite le
crash.

## Étapes d'implémentation

1. Créer `engine/core/util/`.
2. Implémenter `ByteBuffer` + tests.
3. Implémenter `ProducerConsumerQueue` + tests.
4. Implémenter `UniqueTrackablePtr` + tests.
5. Migrer 2-3 sites existants pour utiliser `ByteBuffer` au lieu de `memcpy` pur (si applicable).
6. Doc : section « Util » dans `CODEBASE_MAP.md`.

## Definition of Done (DoD)

- [ ] Build Linux OK (master + shard + client)
- [ ] Tous les tests passent
- [ ] Smoke test ProducerConsumerQueue : 4 producteurs × 100 messages, 2 consommateurs reçoivent les 400 sans perte
- [ ] UniqueTrackablePtr : owner détruit → tracker = nullptr
- [ ] Aucun dossier racine non autorisé
- [ ] Rapport final

## Notes / pièges à éviter

- **ByteBuffer overflow** : un read au-delà du buffer doit retourner false / set flag d'erreur, pas crash. Toujours vérifier `HasError()` après une séquence de reads sur du data externe.
- **Bit-level alignment** : ne jamais mélanger reads/writes octet et bit dans la même séquence sans `FlushBits` explicite. Confusion garantie sinon.
- **PCQueue contention** : si beaucoup de consommateurs, un seul mutex peut goulotter. Pour les hot paths (>10k push/s), considérer une lock-free queue (moodycamel::ConcurrentQueue) — mais commencer simple.
- **TrackablePtr cycles** : pas de cycles possibles par construction (le tracker est unidirectionnel owner → trackers). Pas de leak.
- **Performance Track()** : `Track()` est O(1) mais fait un atomic load. Ne pas tracker dans une boucle hot — caches le tracker.

## Références

- `CMANGOS_ANALYSIS.md` § Util (P3 cross)
- cmangos `src/shared/Util/ByteBuffer.h`,
  `ProducerConsumerQueue.h`, `UniqueTrackablePtr.h`
