# Username Availability Check (Sous-projet C) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Vérifier la disponibilité du nom d'utilisateur en temps réel pendant la saisie : 800 ms après la dernière frappe (≥ 3 caractères), le client envoie une requête au master et affiche un indicateur coloré sur le champ.

**Architecture:** Nouveau type de message `USERNAME_AVAILABLE_REQUEST/RESPONSE` (opcodes 35/36). Le handler serveur délègue à `AccountStore::ExistsLogin` déjà en place. Côté client, un timer debounce déclenche une requête async ; un numéro de séquence élimine les réponses obsolètes. L'indicateur visuel (barre colorée) réutilise le mécanisme `fieldError`/`passwordMatchState` de `RenderField` introduit en Plan B.

**Tech Stack:** C++20, réseau TCP (protocol_v1), Vulkan UI (indicateur visuel), CMake/MSVC.

---

## Fichiers modifiés / créés

| Fichier | Changement |
|---|---|
| `engine/network/ProtocolV1Constants.h` | Opcodes 35/36 `kOpcodeUsernameAvailableRequest/Response` |
| `engine/network/AuthRegisterPayloads.h` | Structs + fonctions `UsernameAvailableRequest/Response` |
| `engine/network/AuthRegisterPayloads.cpp` | Parse/Build payload |
| `engine/server/AuthRegisterHandler.h` | Déclaration `HandleUsernameAvailable` |
| `engine/server/AuthRegisterHandler.cpp` | Implémentation du handler |
| `engine/client/AuthUi.h` | État debounce/séquence ; `UsernameCheckState` enum |
| `engine/client/AuthUi.cpp` | Timer debounce, envoi requête, réception réponse |
| `engine/render/AuthUiRenderer.cpp` | Rendu barre colorée (vert/rouge/gris) sur champ login |

---

### Task 1 : Opcodes et payloads réseau

**Files:**
- Modify: `engine/network/ProtocolV1Constants.h`
- Modify: `engine/network/AuthRegisterPayloads.h`
- Modify: `engine/network/AuthRegisterPayloads.cpp`

- [ ] **Step 1 : Ajouter les opcodes dans `ProtocolV1Constants.h`**

Après la ligne `constexpr uint16_t kOpcodeCharacterCreateResponse = 34u;` (fin du fichier, avant `}`) :

```cpp
	/// Username availability check (Plan C).
	/// Client→Master: check if login is already taken (unauthenticated).
	constexpr uint16_t kOpcodeUsernameAvailableRequest  = 35u;
	constexpr uint16_t kOpcodeUsernameAvailableResponse = 36u;
```

- [ ] **Step 2 : Ajouter les structs et déclarations dans `AuthRegisterPayloads.h`**

À la fin du namespace `engine::network`, après les déclarations `VerifyEmail` :

```cpp
	// -------------------------------------------------------------------------
	// Plan C — Username availability check
	// -------------------------------------------------------------------------

	/// Client→Master: check if a login is available (no session required).
	/// Payload: login string (length-prefixed, same encoding as register).
	struct UsernameAvailableRequestPayload
	{
		std::string login;
		uint32_t    seq = 0; ///< Client sequence number; echoed back in response for stale-detection.
	};
	std::optional<UsernameAvailableRequestPayload> ParseUsernameAvailableRequestPayload(const uint8_t* payload, size_t payloadSize);
	std::vector<uint8_t> BuildUsernameAvailableRequestPayload(std::string_view login, uint32_t seq);

	/// Master→Client: result of availability check.
	struct UsernameAvailableResponsePayload
	{
		uint8_t  available = 0; ///< 1 = available, 0 = taken or invalid.
		uint32_t seq = 0;       ///< Echoed seq from request.
	};
	std::optional<UsernameAvailableResponsePayload> ParseUsernameAvailableResponsePayload(const uint8_t* payload, size_t payloadSize);
	std::vector<uint8_t> BuildUsernameAvailableResponsePacket(uint8_t available, uint32_t seq, uint32_t requestId, uint64_t sessionIdHeader);
```

- [ ] **Step 3 : Implémenter Parse/Build dans `AuthRegisterPayloads.cpp`**

Ajouter à la fin du fichier (avant la fermeture du namespace) :

```cpp
	// -------------------------------------------------------------------------
	// Plan C — Username availability check
	// -------------------------------------------------------------------------

	std::optional<UsernameAvailableRequestPayload> ParseUsernameAvailableRequestPayload(const uint8_t* payload, size_t payloadSize)
	{
		ByteReader r(payload, payloadSize);
		UsernameAvailableRequestPayload out;
		if (!r.ReadString(out.login))   return std::nullopt;
		if (!r.ReadUint32(out.seq))     return std::nullopt;
		return out;
	}

	std::vector<uint8_t> BuildUsernameAvailableRequestPayload(std::string_view login, uint32_t seq)
	{
		ByteWriter w;
		w.WriteString(login);
		w.WriteUint32(seq);
		return w.Take();
	}

	std::optional<UsernameAvailableResponsePayload> ParseUsernameAvailableResponsePayload(const uint8_t* payload, size_t payloadSize)
	{
		ByteReader r(payload, payloadSize);
		UsernameAvailableResponsePayload out;
		if (!r.ReadUint8(out.available)) return std::nullopt;
		if (!r.ReadUint32(out.seq))      return std::nullopt;
		return out;
	}

	std::vector<uint8_t> BuildUsernameAvailableResponsePacket(uint8_t available, uint32_t seq, uint32_t requestId, uint64_t sessionIdHeader)
	{
		ByteWriter w;
		w.WriteUint8(available);
		w.WriteUint32(seq);
		return PacketBuilder::Build(kOpcodeUsernameAvailableResponse, w.Take(), requestId, sessionIdHeader);
	}
```

- [ ] **Step 4 : Vérifier que le code compile**

```bash
cmake --build . --target engine_app --config Release 2>&1 | tail -20
```

Résultat attendu : zéro erreur.

- [ ] **Step 5 : Commit**

```bash
git add engine/network/ProtocolV1Constants.h engine/network/AuthRegisterPayloads.h engine/network/AuthRegisterPayloads.cpp
git commit -m "feat(net): opcodes + payloads USERNAME_AVAILABLE_REQUEST/RESPONSE (Plan C)"
```

---

### Task 2 : Handler serveur

**Files:**
- Modify: `engine/server/AuthRegisterHandler.h`
- Modify: `engine/server/AuthRegisterHandler.cpp`

- [ ] **Step 1 : Déclarer `HandleUsernameAvailable` dans `AuthRegisterHandler.h`**

Dans la section `private:`, après la déclaration `HandleAuth` :

```cpp
		void HandleUsernameAvailable(uint32_t connId, uint32_t requestId, uint64_t sessionIdHeader,
			const uint8_t* payload, size_t payloadSize);
```

- [ ] **Step 2 : Dispatcher le nouvel opcode dans `HandlePacket`**

Dans `AuthRegisterHandler.cpp`, dans la fonction `HandlePacket`, après le bloc `kOpcodeAuthRequest` :

```cpp
		if (opcode == engine::network::kOpcodeUsernameAvailableRequest)
		{
			HandleUsernameAvailable(connId, requestId, sessionIdHeader, payload, payloadSize);
			return;
		}
```

- [ ] **Step 3 : Implémenter `HandleUsernameAvailable`**

Ajouter la fonction après `HandleAuth` dans `AuthRegisterHandler.cpp` :

```cpp
	void AuthRegisterHandler::HandleUsernameAvailable(uint32_t connId, uint32_t requestId, uint64_t sessionIdHeader,
		const uint8_t* payload, size_t payloadSize)
	{
		using namespace engine::network;
		auto parsed = ParseUsernameAvailableRequestPayload(payload, payloadSize);
		if (!parsed)
		{
			LOG_DEBUG(Auth, "[AuthRegisterHandler] UsernameAvailable: invalid payload (connId={})", connId);
			return; // pas de réponse erreur : le client ignorera le timeout silencieusement
		}
		if (!m_accountStore)
		{
			LOG_WARN(Auth, "[AuthRegisterHandler] UsernameAvailable: no account store");
			// Répondre "taken" pour éviter de bloquer le client indéfiniment.
			auto pkt = BuildUsernameAvailableResponsePacket(0, parsed->seq, requestId, sessionIdHeader);
			if (!pkt.empty()) m_server->Send(connId, pkt);
			return;
		}
		const bool taken = m_accountStore->ExistsLogin(parsed->login);
		const uint8_t available = taken ? 0u : 1u;
		auto pkt = BuildUsernameAvailableResponsePacket(available, parsed->seq, requestId, sessionIdHeader);
		if (!pkt.empty())
			m_server->Send(connId, pkt);
		LOG_DEBUG(Auth, "[AuthRegisterHandler] UsernameAvailable: login={} available={}", parsed->login, (int)available);
	}
```

- [ ] **Step 4 : Compiler**

```bash
cmake --build . --target engine_app --config Release 2>&1 | tail -20
```

Résultat attendu : zéro erreur.

- [ ] **Step 5 : Commit**

```bash
git add engine/server/AuthRegisterHandler.h engine/server/AuthRegisterHandler.cpp
git commit -m "feat(server): HandleUsernameAvailable — verifie ExistsLogin et repond disponibilite (Plan C)"
```

---

### Task 3 : État client et enum `UsernameCheckState`

**Files:**
- Modify: `engine/client/AuthUi.h`

- [ ] **Step 1 : Ajouter l'enum `UsernameCheckState` dans `AuthUi.h`**

Avant la déclaration de la classe `AuthUiPresenter` (ou dans sa section publique d'enums), ajouter :

```cpp
	/// État de la vérification temps-réel du nom d'utilisateur.
	enum class UsernameCheckState : uint8_t
	{
		Idle       = 0, ///< Champ vide ou < 3 caractères ; aucun indicateur affiché.
		Pending    = 1, ///< Debounce en cours ou requête envoyée, réponse attendue.
		Available  = 2, ///< Serveur a confirmé disponibilité.
		Taken      = 3, ///< Serveur a indiqué login déjà pris.
	};
```

- [ ] **Step 2 : Ajouter les membres privés dans `AuthUiPresenter`**

Dans la section `private:` de `AuthUiPresenter`, ajouter après les membres existants :

```cpp
		// --- Plan C: username availability debounce ---
		UsernameCheckState m_usernameCheckState = UsernameCheckState::Idle;
		uint32_t m_usernameCheckSeq = 0;       ///< Numéro de séquence courant ; réponses avec seq différent sont ignorées.
		double   m_usernameDebounceTimer = 0.0; ///< Secondes restantes avant envoi de la requête. ≤0 = pas en attente.
		std::string m_usernameLastChecked;       ///< Valeur envoyée au serveur pour le seq courant.
```

- [ ] **Step 3 : Exposer l'état dans `RenderField` (champ login)**

Dans `RenderField` (ou dans la structure de modèle `AuthRenderModel`), il n'y a pas besoin d'un nouveau champ : `passwordMatchState` est déjà un `int32_t` pouvant servir d'indicateur générique. Cependant, pour plus de clarté, ajouter un champ dédié dans `RenderField` (après `passwordMatchState`) :

```cpp
		/// Indicateur de disponibilité username (champ login uniquement).
		/// 0 = neutre, 1 = disponible, -1 = pris, 2 = vérification en cours.
		int32_t usernameCheckState = 0;
```

- [ ] **Step 4 : Compiler**

```bash
cmake --build . --target engine_app --config Release 2>&1 | tail -20
```

- [ ] **Step 5 : Commit**

```bash
git add engine/client/AuthUi.h
git commit -m "feat(client): UsernameCheckState enum et membres debounce dans AuthUiPresenter (Plan C)"
```

---

### Task 4 : Logique debounce et envoi/réception dans `AuthUi.cpp`

**Files:**
- Modify: `engine/client/AuthUi.cpp`

- [ ] **Step 1 : Réinitialiser le debounce lors de la modification du champ login**

Dans la méthode qui traite les frappes clavier sur le champ login (probablement dans `HandleKeyInput` ou la section `Phase::Register` qui modifie `m_login`), après toute modification de `m_login` pendant l'inscription, ajouter :

```cpp
			// Réinitialiser le debounce username si on est en phase d'inscription.
			if (m_state.registerMode)
			{
				m_usernameCheckState = UsernameCheckState::Idle;
				m_usernameCheckSeq++;          // invalide les réponses en vol
				if (m_login.size() >= 3)
					m_usernameDebounceTimer = 0.8; // 800 ms
				else
					m_usernameDebounceTimer = 0.0;
			}
```

- [ ] **Step 2 : Tick du debounce dans `Update`**

Dans la méthode `Update(double dt)` (ou `Tick`, selon le nom dans AuthUi.cpp) du présenteur, ajouter après le traitement d'animation existant :

```cpp
		// Plan C: debounce username availability.
		if (m_state.registerMode && m_usernameDebounceTimer > 0.0)
		{
			m_usernameDebounceTimer -= dt;
			if (m_usernameDebounceTimer <= 0.0)
			{
				m_usernameDebounceTimer = 0.0;
				if (m_login.size() >= 3)
				{
					m_usernameCheckState = UsernameCheckState::Pending;
					m_usernameLastChecked = m_login;
					const uint32_t seq = m_usernameCheckSeq;
					// Envoi de la requête via le canal réseau existant.
					auto payload = engine::network::BuildUsernameAvailableRequestPayload(m_login, seq);
					m_netClient->SendPacket(engine::network::kOpcodeUsernameAvailableRequest, payload);
				}
			}
		}
```

- [ ] **Step 3 : Traiter la réponse `kOpcodeUsernameAvailableResponse` dans le dispatcher de paquets entrants**

Localiser la fonction/méthode qui dispatche les paquets reçus du master (elle doit contenir des cas `kOpcodeRegisterResponse`, `kOpcodeAuthResponse`, etc.). Ajouter :

```cpp
		case engine::network::kOpcodeUsernameAvailableResponse:
		{
			auto resp = engine::network::ParseUsernameAvailableResponsePayload(payload, payloadSize);
			if (!resp) break;
			if (resp->seq != m_usernameCheckSeq) break; // réponse obsolète
			m_usernameCheckState = resp->available
				? UsernameCheckState::Available
				: UsernameCheckState::Taken;
			break;
		}
```

- [ ] **Step 4 : Peupler `usernameCheckState` dans `BuildRenderModel` (phase Register)**

Dans la boucle qui construit les `RenderField` pour `Phase::Register`, sur le champ `login` (index 0) :

```cpp
			// Indicateur disponibilité username.
			loginField.usernameCheckState = [&]() -> int32_t {
				switch (m_usernameCheckState)
				{
					case UsernameCheckState::Available: return  1;
					case UsernameCheckState::Taken:     return -1;
					case UsernameCheckState::Pending:   return  2;
					default:                            return  0;
				}
			}();
```

- [ ] **Step 5 : Réinitialiser l'état à l'entrée/sortie de la phase Register**

Dans la méthode qui change de phase (transition vers `Phase::Register` et depuis), ajouter :

```cpp
		m_usernameCheckState = UsernameCheckState::Idle;
		m_usernameCheckSeq++;
		m_usernameDebounceTimer = 0.0;
		m_usernameLastChecked.clear();
```

- [ ] **Step 6 : Compiler**

```bash
cmake --build . --target engine_app --config Release 2>&1 | tail -20
```

Résultat attendu : zéro erreur.

- [ ] **Step 7 : Commit**

```bash
git add engine/client/AuthUi.cpp
git commit -m "feat(client): debounce 800ms + envoi/reception USERNAME_AVAILABLE pour champ login (Plan C)"
```

---

### Task 5 : Indicateur visuel dans `AuthUiRenderer.cpp`

**Files:**
- Modify: `engine/render/AuthUiRenderer.cpp`

- [ ] **Step 1 : Localiser le rendu du bord inférieur des champs dans `RecordModel`**

Dans `AuthUiRenderer.cpp`, trouver la boucle qui dessine les champs (`for (const auto& field : model.fields)`). Repérer l'endroit où la barre de bas de champ (`fieldError` / `passwordMatchState`) est déjà dessinée (ajoutée en Plan B).

- [ ] **Step 2 : Ajouter la barre colorée username**

Après la logique `passwordMatchState`, ajouter (pour le champ login uniquement, identifié par `field.usernameCheckState != 0`) :

```cpp
			// Plan C: indicateur disponibilité username (champ avec usernameCheckState != 0).
			if (field.usernameCheckState != 0)
			{
				constexpr int32_t kBarH = 2;
				const int32_t barY = fieldY + kAuthUiFieldBoxHeightPx - kBarH;
				uint32_t barColor = 0xFF888888u; // gris = pending
				if (field.usernameCheckState ==  1) barColor = 0xFF22CC44u; // vert = disponible
				if (field.usernameCheckState == -1) barColor = 0xFFCC2222u; // rouge = pris
				layers.PushRect(fieldX, barY, fieldW, kBarH, barColor);
			}
```

- [ ] **Step 3 : Compiler**

```bash
cmake --build . --target engine_app --config Release 2>&1 | tail -20
```

- [ ] **Step 4 : Vérification visuelle**

Lancer `lcdlln.exe`, aller sur la page d'inscription :
- Saisir 2 caractères → aucun indicateur.
- Saisir 3 caractères, attendre 800 ms → barre grise (requête en vol).
- Si le login existe → barre rouge apparaît.
- Si le login est libre → barre verte apparaît.
- Modifier le champ → barre disparaît puis réapparaît après le debounce.

- [ ] **Step 5 : Commit**

```bash
git add engine/render/AuthUiRenderer.cpp
git commit -m "feat(render): barre coloree disponibilite username sur champ login (Plan C)"
```
