# Grimoire / Carnet de techniques — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Ajouter un panneau « Grimoire » (référence des sorts du kit + réassignation des 10 slots de barre d'action par glisser-déposer), avec assignation persistée côté serveur.

**Architecture:** Server-first en 2 PR lock-step. Le serveur (shardd) persiste un `actionBarLayout` de 10 `spellId` par personnage (`PersistedCharacterState`, format INI existant) et l'échange via **deux nouveaux kinds wire rétro-additifs** (`SetActionBarLayout` client→shard, `ActionBarLayoutUpdate` shard→client, autoritaire, envoyé à l'enter-world ET en réponse à un Set). Le client étend la barre d'action SP3 de 4 à 10 slots remappables, résout l'ordre via le layout (fonction pure testable), et ajoute un presenter/renderer ImGui (patron SkillBook) pour la vue + le drag&drop.

**Tech Stack:** C++20, parseurs JSON/INI locaux, protocole UDP gameplay maison (`ServerProtocol`), ImGui (Vulkan), tests CTest via `lcdlln_add_simple_test` (CI build-linux). Pas de toolchain locale — compilation via CI/VS.

**Déviations vs spec (validées par l'extraction de code) :**
- **Pas de bump `kProtocolVersion`** (le spec §6.4 prévoyait un bump). `kProtocolVersion` vaut 13 ; la convention du fichier est que de **nouveaux kinds en queue d'enum sont rétro-additifs** (pas de bump, cf. `ForcePosition`/`LootNotify`). Conséquence déploiement **améliorée** : un vieux client ignore les nouveaux kinds (pas de lock-step global forcé), mais la feature exige le **redéploiement shardd** + un client neuf.
- **2 kinds au lieu de Request/Response+status** : la réponse est un `ActionBarLayoutUpdate` autoritaire (valide → nouveau layout ; invalide → layout inchangé renvoyé → le client réconcilie/réverte). Le client valide localement avant d'envoyer, donc un rejet serveur est de la défense en profondeur (pas de message d'erreur explicite — réconciliation silencieuse).

**Livraison : 2 PR séquentielles base main** (pas de stack — CI) :
- **PR-1 serveur+wire** (Tasks 1-6) — ⚠️ **redéploiement shardd requis**.
- **PR-2 client** (Tasks 7-15), après merge PR-1 — **déployée lock-step** avec PR-1.

---

# PR-1 — Serveur + wire

> Toutes les modifs PR-1 portent sur des fichiers **déjà** dans le build (aucun nouveau `.cpp` serveur) → **pas de changement CMake** en PR-1.

### Task 1: Wire — kinds `SetActionBarLayout` (88) + `ActionBarLayoutUpdate` (89)

**Files:**
- Modify: `src/shared/network/ServerProtocol.h`
- Modify: `src/shared/network/ServerProtocol.cpp`
- Test: `src/shared/network/ServerProtocolTests.cpp`

- [ ] **Step 1: Ajouter l'include `<array>`**

Dans `src/shared/network/ServerProtocol.h`, en tête (zone des `#include`), ajouter si absent :

```cpp
#include <array>
```

- [ ] **Step 2: Ajouter les 2 kinds à l'enum `MessageKind`**

Dans `src/shared/network/ServerProtocol.h`, le dernier membre est `LootNotify = 87` (sans virgule). Le remplacer par (ajout d'une virgule + 2 kinds) :

```cpp
		LootNotify = 87,
		/// Grimoire — client → shard : réassignation des 10 slots de la barre
		/// d'action (slot i → spellId, "" = vide). Validé contre le kit du profil.
		/// Ajout rétro-additif (pas de bump de kProtocolVersion).
		SetActionBarLayout = 88,
		/// Grimoire — shard → client : layout autoritaire des 10 slots, poussé à
		/// l'enter-world ET en réponse à un SetActionBarLayout (invalide = layout
		/// inchangé renvoyé). Ajout rétro-additif (pas de bump).
		ActionBarLayoutUpdate = 89
	};
```

- [ ] **Step 3: Ajouter les 2 structs de message**

Dans `src/shared/network/ServerProtocol.h`, après la struct `CastRequestMessage` (~ligne 419) :

```cpp
	/// Grimoire — slots de barre d'action (10 entrées, slot i → spellId, "" = vide).
	struct SetActionBarLayoutMessage
	{
		uint32_t clientId = 0;
		std::array<std::string, 10> slots{};
	};

	/// Grimoire — layout autoritaire poussé par le shard (enter-world / ACK).
	struct ActionBarLayoutUpdateMessage
	{
		uint32_t clientId = 0;
		std::array<std::string, 10> slots{};
	};
```

- [ ] **Step 4: Déclarer les Encode/Decode**

Dans `src/shared/network/ServerProtocol.h`, après les déclarations d'`EncodeCastRequest`/`DecodeCastRequest` (~ligne 644) :

```cpp
	std::vector<std::byte> EncodeSetActionBarLayout(const SetActionBarLayoutMessage& message);
	bool DecodeSetActionBarLayout(std::span<const std::byte> packet, SetActionBarLayoutMessage& outMessage);
	std::vector<std::byte> EncodeActionBarLayoutUpdate(const ActionBarLayoutUpdateMessage& message);
	bool DecodeActionBarLayoutUpdate(std::span<const std::byte> packet, ActionBarLayoutUpdateMessage& outMessage);
```

- [ ] **Step 5: Écrire les tests d'abord (roundtrip + tronqué)**

Dans `src/shared/network/ServerProtocolTests.cpp`, ajouter ces 2 fonctions (après `TestCastRequestRoundTrip`, ~ligne 394) :

```cpp
	void TestSetActionBarLayoutRoundTrip()
	{
		engine::server::SetActionBarLayoutMessage in{};
		in.clientId = 42u;
		in.slots[0] = "lanceur_trait_de_feu";
		in.slots[1] = "lanceur_nova";
		in.slots[9] = "lanceur_brulure";
		// slots 2..8 restent vides ("")

		const std::vector<std::byte> packet = engine::server::EncodeSetActionBarLayout(in);
		assert(!packet.empty());

		engine::server::SetActionBarLayoutMessage out{};
		assert(engine::server::DecodeSetActionBarLayout(packet, out));
		assert(out.clientId == 42u);
		assert(out.slots[0] == "lanceur_trait_de_feu");
		assert(out.slots[1] == "lanceur_nova");
		assert(out.slots[2].empty());
		assert(out.slots[9] == "lanceur_brulure");

		std::vector<std::byte> truncated = packet;
		truncated.resize(truncated.size() - 2u);
		assert(!engine::server::DecodeSetActionBarLayout(truncated, out));
		std::puts("[OK] TestSetActionBarLayoutRoundTrip");
	}

	void TestActionBarLayoutUpdateRoundTrip()
	{
		engine::server::ActionBarLayoutUpdateMessage in{};
		in.clientId = 7u;
		in.slots[0] = "melee_frappe_brutale";
		in.slots[3] = "melee_cri_de_guerre";

		const std::vector<std::byte> packet = engine::server::EncodeActionBarLayoutUpdate(in);
		assert(!packet.empty());

		engine::server::ActionBarLayoutUpdateMessage out{};
		assert(engine::server::DecodeActionBarLayoutUpdate(packet, out));
		assert(out.clientId == 7u);
		assert(out.slots[0] == "melee_frappe_brutale");
		assert(out.slots[3] == "melee_cri_de_guerre");
		assert(out.slots[1].empty());

		std::vector<std::byte> truncated = packet;
		truncated.resize(truncated.size() - 2u);
		assert(!engine::server::DecodeActionBarLayoutUpdate(truncated, out));
		std::puts("[OK] TestActionBarLayoutUpdateRoundTrip");
	}
```

Puis enregistrer les appels dans le runner (zone ~ligne 581-596, à côté de `TestCastRequestRoundTrip();`) :

```cpp
	TestSetActionBarLayoutRoundTrip();
	TestActionBarLayoutUpdateRoundTrip();
```

- [ ] **Step 6: Lancer les tests pour vérifier l'échec (compile fail)**

Run (CI ou VS) : cible `server_protocol_tests`.
Expected: **échec de compilation** — `EncodeSetActionBarLayout` non défini.

- [ ] **Step 7: Implémenter les Encode/Decode**

Dans `src/shared/network/ServerProtocol.cpp`, après `DecodeCastRequest` (~ligne 739) :

```cpp
	std::vector<std::byte> EncodeSetActionBarLayout(const SetActionBarLayoutMessage& message)
	{
		// Grimoire — payload : clientId (4) + 10 chaînes préfixées u16 (slots).
		size_t hint = 4;
		for (const std::string& slot : message.slots)
		{
			hint += 2 + slot.size();
		}
		std::vector<std::byte> packet = BeginPacket(MessageKind::SetActionBarLayout, hint);
		WriteU32(packet, message.clientId);
		for (const std::string& slot : message.slots)
		{
			WriteSizedString(packet, slot);
		}
		return packet;
	}

	bool DecodeSetActionBarLayout(std::span<const std::byte> packet, SetActionBarLayoutMessage& outMessage)
	{
		std::span<const std::byte> payload;
		if (!DecodeHeader(packet, MessageKind::SetActionBarLayout, payload) || payload.size() < 4)
		{
			return false;
		}
		outMessage.clientId = ReadU32(payload, 0);
		size_t offset = 4;
		for (std::string& slot : outMessage.slots)
		{
			// Borne dure défensive : un spellId fait < 64 octets (ids snake_case).
			if (!ReadSizedString(payload, offset, slot) || slot.size() > 64u)
			{
				return false;
			}
		}
		if (offset != payload.size())
		{
			return false;
		}
		return true;
	}

	std::vector<std::byte> EncodeActionBarLayoutUpdate(const ActionBarLayoutUpdateMessage& message)
	{
		size_t hint = 4;
		for (const std::string& slot : message.slots)
		{
			hint += 2 + slot.size();
		}
		std::vector<std::byte> packet = BeginPacket(MessageKind::ActionBarLayoutUpdate, hint);
		WriteU32(packet, message.clientId);
		for (const std::string& slot : message.slots)
		{
			WriteSizedString(packet, slot);
		}
		return packet;
	}

	bool DecodeActionBarLayoutUpdate(std::span<const std::byte> packet, ActionBarLayoutUpdateMessage& outMessage)
	{
		std::span<const std::byte> payload;
		if (!DecodeHeader(packet, MessageKind::ActionBarLayoutUpdate, payload) || payload.size() < 4)
		{
			return false;
		}
		outMessage.clientId = ReadU32(payload, 0);
		size_t offset = 4;
		for (std::string& slot : outMessage.slots)
		{
			if (!ReadSizedString(payload, offset, slot) || slot.size() > 64u)
			{
				return false;
			}
		}
		if (offset != payload.size())
		{
			return false;
		}
		return true;
	}
```

- [ ] **Step 8: Lancer les tests pour vérifier le succès**

Run : cible `server_protocol_tests`.
Expected: PASS — `[OK] TestSetActionBarLayoutRoundTrip` + `[OK] TestActionBarLayoutUpdateRoundTrip`.

- [ ] **Step 9: Commit**

```bash
git add src/shared/network/ServerProtocol.h src/shared/network/ServerProtocol.cpp src/shared/network/ServerProtocolTests.cpp
git commit -m "feat(wire): kinds SetActionBarLayout(88) + ActionBarLayoutUpdate(89) (Grimoire)"
```

---

### Task 2: Persistance — champ `actionBarLayout` dans `PersistedCharacterState`

**Files:**
- Modify: `src/shardd/gameplay/character/CharacterPersistence.h`
- Modify: `src/shardd/gameplay/character/CharacterPersistence.cpp`

> Pas de cible de test dédiée pour `CharacterPersistence` dans le repo ; la sérialisation suit **verbatim** le patron INI de `chatIgnoredDisplayNames`. Vérification via build + Task 6 (intégration).

- [ ] **Step 1: Include `<array>` + champ struct**

Dans `src/shardd/gameplay/character/CharacterPersistence.h`, ajouter `#include <array>` en tête si absent, puis ajouter le champ avant la fermeture de `struct PersistedCharacterState` (après `professions;`) :

```cpp
	/// Grimoire — 10 slots de barre d'action (slot i → spellId, "" = vide).
	std::array<std::string, 10> actionBarLayout{};
```

- [ ] **Step 2: Sérialisation (Save)**

Dans `src/shardd/gameplay/character/CharacterPersistence.cpp`, dans la fonction de save (après le bloc `chat.ignore.*`, ~ligne 261) :

```cpp
		// Grimoire — 10 slots de barre d'action (clés fixes, "" = slot vide ;
		// l'alignement positionnel est conservé, contrairement à chat.ignore).
		for (size_t slotIndex = 0; slotIndex < state.actionBarLayout.size(); ++slotIndex)
		{
			output << "actionbar.slot." << slotIndex << "=" << state.actionBarLayout[slotIndex] << "\n";
		}
```

- [ ] **Step 3: Désérialisation (Load)**

Dans la fonction de load (après le bloc `chat.ignore.*`, ~ligne 164) :

```cpp
		// Grimoire — 10 slots de barre d'action (absent = "" = vide).
		for (size_t slotIndex = 0; slotIndex < outState.actionBarLayout.size(); ++slotIndex)
		{
			outState.actionBarLayout[slotIndex] =
				persisted.GetString("actionbar.slot." + std::to_string(slotIndex), "");
		}
```

- [ ] **Step 4: Commit**

```bash
git add src/shardd/gameplay/character/CharacterPersistence.h src/shardd/gameplay/character/CharacterPersistence.cpp
git commit -m "feat(persist): actionBarLayout (10 slots) dans PersistedCharacterState"
```

---

### Task 3: `ConnectedClient` — champ + assemblage load/save

**Files:**
- Modify: `src/shared/server_bootstrap/ServerApp.h`
- Modify: `src/shared/server_bootstrap/ServerApp.cpp`

- [ ] **Step 1: Include `<array>` + champ sur `ConnectedClient`**

Dans `src/shared/server_bootstrap/ServerApp.h`, ajouter `#include <array>` en tête si absent. Puis, dans `struct ConnectedClient`, avant la fermeture `};` (après `professions;`, ~ligne 219) :

```cpp
	/// Grimoire — 10 slots de barre d'action (slot i → spellId, "" = vide).
	std::array<std::string, 10> actionBarLayout{};
```

- [ ] **Step 2: Load à l'enter-world**

Dans `src/shared/server_bootstrap/ServerApp.cpp`, dans le bloc d'admission, après la ligne 1302 (`acceptedClient.professions = std::move(persistedState.professions);`) :

```cpp
		/// Grimoire — restore action bar layout (10 slots).
		acceptedClient.actionBarLayout = std::move(persistedState.actionBarLayout);
```

- [ ] **Step 3: Save dans l'assemblage**

Dans `ServerApp::SaveConnectedClient`, après la ligne 2017 (`state.professions = client.professions;`) :

```cpp
		/// Grimoire — persist action bar layout.
		state.actionBarLayout = client.actionBarLayout;
```

- [ ] **Step 4: Commit**

```bash
git add src/shared/server_bootstrap/ServerApp.h src/shared/server_bootstrap/ServerApp.cpp
git commit -m "feat(server): ConnectedClient.actionBarLayout + load/save assembly"
```

---

### Task 4: Handler `HandleSetActionBarLayout` + dispatch

**Files:**
- Modify: `src/shared/server_bootstrap/ServerApp.h`
- Modify: `src/shared/server_bootstrap/ServerApp.cpp`

- [ ] **Step 1: Déclarer handler + émetteur**

Dans `src/shared/server_bootstrap/ServerApp.h`, à côté de la déclaration de `HandleCastRequest` :

```cpp
	void HandleSetActionBarLayout(const Endpoint& endpoint, const SetActionBarLayoutMessage& message);
	bool SendActionBarLayout(const ConnectedClient& client);
```

- [ ] **Step 2: Brancher le dispatch (cascade Decode/Handle)**

Dans `src/shared/server_bootstrap/ServerApp.cpp`, juste après le bloc `CastRequest` (~ligne 815) :

```cpp
		// Grimoire — réassignation des slots de barre d'action.
		SetActionBarLayoutMessage setLayout{};
		if (DecodeSetActionBarLayout(packetBytes, setLayout))
		{
			HandleSetActionBarLayout(datagram.endpoint, setLayout);
			return;
		}
```

- [ ] **Step 3: Implémenter `SendActionBarLayout` + `HandleSetActionBarLayout`**

Dans `src/shared/server_bootstrap/ServerApp.cpp`, à côté de `HandleCastRequest` :

```cpp
	bool ServerApp::SendActionBarLayout(const ConnectedClient& client)
	{
		ActionBarLayoutUpdateMessage msg{};
		msg.clientId = client.clientId;
		msg.slots = client.actionBarLayout;
		const std::vector<std::byte> packet = EncodeActionBarLayoutUpdate(msg);
		if (!m_transport.Send(client.endpoint, packet))
		{
			LOG_WARN(Net, "[ServerApp] SendActionBarLayout failed (client_id={})", client.clientId);
			return false;
		}
		return true;
	}

	void ServerApp::HandleSetActionBarLayout(const Endpoint& endpoint, const SetActionBarLayoutMessage& message)
	{
		ConnectedClient* client = FindClient(endpoint);
		if (client == nullptr)
		{
			LOG_WARN(Net, "[ServerApp] SetActionBarLayout ignored from unknown endpoint {}",
				UdpTransport::EndpointToString(endpoint));
			return;
		}
		if (client->clientId != message.clientId)
		{
			LOG_WARN(Net, "[ServerApp] SetActionBarLayout ignored: client_id mismatch (expected={}, got={})",
				client->clientId, message.clientId);
			return;
		}

		// Validation autoritaire : chaque slot non vide ∈ kit du profil + unicité.
		std::array<std::string, 10> validated{};
		for (size_t slotIndex = 0; slotIndex < message.slots.size(); ++slotIndex)
		{
			const std::string& spellId = message.slots[slotIndex];
			if (spellId.empty())
			{
				continue; // slot vide autorisé
			}
			if (client->profileId.empty()
				|| m_spellKits.FindSpell(client->profileId, spellId) == nullptr)
			{
				LOG_WARN(Net, "[ServerApp] SetActionBarLayout reject: spell '{}' not in kit '{}' (client_id={})",
					spellId, client->profileId, client->clientId);
				(void)SendActionBarLayout(*client); // renvoie l'état inchangé (réconciliation client)
				return;
			}
			// Unicité : un sort ne peut occuper deux slots.
			for (size_t prior = 0; prior < slotIndex; ++prior)
			{
				if (validated[prior] == spellId)
				{
					LOG_WARN(Net, "[ServerApp] SetActionBarLayout reject: duplicate spell '{}' (client_id={})",
						spellId, client->clientId);
					(void)SendActionBarLayout(*client);
					return;
				}
			}
			validated[slotIndex] = spellId;
		}

		client->actionBarLayout = validated;
		SaveConnectedClient(*client, "actionbar_change");
		(void)SendActionBarLayout(*client); // ACK autoritaire (layout validé)
		LOG_INFO(Net, "[ServerApp] SetActionBarLayout applied (client_id={})", client->clientId);
	}
```

- [ ] **Step 4: Pousser le layout à l'enter-world**

Dans `src/shared/server_bootstrap/ServerApp.cpp`, juste après l'appel d'enter-world `(void)SendPlayerStats(acceptedClient);` (~ligne 1500) :

```cpp
		(void)SendActionBarLayout(acceptedClient); // Grimoire — layout persisté (ou vide)
```

- [ ] **Step 5: Build serveur (compile check)**

Run : build de `shard_app` (CI build-linux ou VS).
Expected: compile OK ; aucun test nouveau (handler couvert par Task 6 intégration + le wire de Task 1).

- [ ] **Step 6: Commit**

```bash
git add src/shared/server_bootstrap/ServerApp.h src/shared/server_bootstrap/ServerApp.cpp
git commit -m "feat(server): HandleSetActionBarLayout (validation kit+unicite) + push enter-world"
```

---

### Task 5: CODEBASE_MAP + PR-1

**Files:**
- Modify: `CODEBASE_MAP.md`

- [ ] **Step 1: Documenter**

Ajouter une entrée « Grimoire (PR-1 serveur) » dans `CODEBASE_MAP.md` : kinds 88/89, champ persistant `actionBarLayout`, handler `HandleSetActionBarLayout`, push enter-world.

- [ ] **Step 2: Commit + push + PR**

```bash
git add CODEBASE_MAP.md
git commit -m "docs(codebase-map): Grimoire PR-1 (wire 88/89, persist, handler)"
git push -u origin <branche-pr1>
```

Ouvrir la PR base `main`. Description : inclure **⚠️ redéploiement shardd requis** (nouveau handler + champ persisté ; kinds rétro-additifs, pas de bump de version mais le shard doit traiter le kind 88).

---

# PR-2 — Client (après merge PR-1)

### Task 6: Résolveur de layout (fonction pure, testable)

**Files:**
- Create: `src/client/gameplay/ActionBarLayout.h`
- Create: `src/client/gameplay/ActionBarLayout.cpp`
- Test: `src/client/gameplay/ActionBarLayoutTests.cpp`
- Modify: `src/CMakeLists.txt`

- [ ] **Step 1: Écrire le test d'abord**

Create `src/client/gameplay/ActionBarLayoutTests.cpp` :

```cpp
#include "src/client/gameplay/ActionBarLayout.h"
#include "src/client/gameplay/SpellKitCatalog.h"

#include <array>
#include <cassert>
#include <cstdio>
#include <string>

namespace
{
	using engine::client::SpellDisplay;

	std::vector<SpellDisplay> MakeKit()
	{
		std::vector<SpellDisplay> kit;
		SpellDisplay a{}; a.spellId = "s1"; a.slot = 1; kit.push_back(a);
		SpellDisplay b{}; b.spellId = "s2"; b.slot = 2; kit.push_back(b);
		SpellDisplay c{}; c.spellId = "s3"; c.slot = 3; kit.push_back(c);
		return kit;
	}

	// Layout vide → défaut = sorts du kit dans l'ordre, le reste vide.
	void TestEmptyLayoutUsesKitOrder()
	{
		const std::vector<SpellDisplay> kit = MakeKit();
		std::array<std::string, 10> layout{}; // tout vide
		const std::array<std::string, 10> resolved =
			engine::client::ResolveActionBarLayout(layout, kit);
		assert(resolved[0] == "s1");
		assert(resolved[1] == "s2");
		assert(resolved[2] == "s3");
		assert(resolved[3].empty());
		std::puts("[OK] TestEmptyLayoutUsesKitOrder");
	}

	// Layout custom conservé.
	void TestCustomLayoutPreserved()
	{
		const std::vector<SpellDisplay> kit = MakeKit();
		std::array<std::string, 10> layout{};
		layout[0] = "s3";
		layout[5] = "s1";
		const std::array<std::string, 10> resolved =
			engine::client::ResolveActionBarLayout(layout, kit);
		assert(resolved[0] == "s3");
		assert(resolved[5] == "s1");
		assert(resolved[1].empty());
		std::puts("[OK] TestCustomLayoutPreserved");
	}

	// spellId absent du kit (contenu modifié) → slot vidé.
	void TestObsoleteSpellDropped()
	{
		const std::vector<SpellDisplay> kit = MakeKit();
		std::array<std::string, 10> layout{};
		layout[0] = "obsolete";
		layout[1] = "s2";
		const std::array<std::string, 10> resolved =
			engine::client::ResolveActionBarLayout(layout, kit);
		assert(resolved[0].empty());
		assert(resolved[1] == "s2");
		std::puts("[OK] TestObsoleteSpellDropped");
	}

	// Doublon défensif → seconde occurrence vidée.
	void TestDuplicateDropped()
	{
		const std::vector<SpellDisplay> kit = MakeKit();
		std::array<std::string, 10> layout{};
		layout[0] = "s1";
		layout[1] = "s1";
		const std::array<std::string, 10> resolved =
			engine::client::ResolveActionBarLayout(layout, kit);
		assert(resolved[0] == "s1");
		assert(resolved[1].empty());
		std::puts("[OK] TestDuplicateDropped");
	}
}

int main()
{
	TestEmptyLayoutUsesKitOrder();
	TestCustomLayoutPreserved();
	TestObsoleteSpellDropped();
	TestDuplicateDropped();
	std::puts("[OK] ActionBarLayoutTests");
	return 0;
}
```

- [ ] **Step 2: Écrire le header**

Create `src/client/gameplay/ActionBarLayout.h` :

```cpp
#pragma once

#include "src/client/gameplay/SpellKitCatalog.h"

#include <array>
#include <string>
#include <vector>

namespace engine::client
{
	/// Grimoire — résout le layout effectif des 10 slots de barre d'action.
	/// \param layout  layout persisté (slot i → spellId ; "" = vide).
	/// \param kit     kit du profil courant (SpellKitCatalog::FindKit).
	/// \return 10 spellId : si \p layout est entièrement vide → les sorts du kit
	///         dans l'ordre (slots au-delà du kit = "") ; sinon \p layout filtré
	///         (spellId absent du kit ou en doublon → slot vidé).
	std::array<std::string, 10> ResolveActionBarLayout(
		const std::array<std::string, 10>& layout,
		const std::vector<SpellDisplay>& kit);

	/// Retourne le SpellDisplay d'un spellId dans \p kit, ou nullptr.
	const SpellDisplay* FindSpellInKit(const std::vector<SpellDisplay>& kit, const std::string& spellId);
}
```

- [ ] **Step 3: Lancer le test (échec attendu)**

Run : cible `action_bar_layout_tests`.
Expected: échec compile/link — `ResolveActionBarLayout` non défini.

- [ ] **Step 4: Implémenter**

Create `src/client/gameplay/ActionBarLayout.cpp` :

```cpp
#include "src/client/gameplay/ActionBarLayout.h"

namespace engine::client
{
	const SpellDisplay* FindSpellInKit(const std::vector<SpellDisplay>& kit, const std::string& spellId)
	{
		for (const SpellDisplay& spell : kit)
		{
			if (spell.spellId == spellId)
			{
				return &spell;
			}
		}
		return nullptr;
	}

	std::array<std::string, 10> ResolveActionBarLayout(
		const std::array<std::string, 10>& layout,
		const std::vector<SpellDisplay>& kit)
	{
		std::array<std::string, 10> resolved{};

		const bool layoutEmpty = [&layout]() {
			for (const std::string& slot : layout)
			{
				if (!slot.empty())
				{
					return false;
				}
			}
			return true;
		}();

		if (layoutEmpty)
		{
			// Défaut : sorts du kit dans l'ordre, plafonné à 10.
			const size_t count = std::min<size_t>(kit.size(), resolved.size());
			for (size_t i = 0; i < count; ++i)
			{
				resolved[i] = kit[i].spellId;
			}
			return resolved;
		}

		// Layout custom : filtre les spellId hors-kit + doublons.
		for (size_t i = 0; i < layout.size(); ++i)
		{
			const std::string& spellId = layout[i];
			if (spellId.empty() || FindSpellInKit(kit, spellId) == nullptr)
			{
				continue;
			}
			bool duplicate = false;
			for (size_t prior = 0; prior < i; ++prior)
			{
				if (resolved[prior] == spellId)
				{
					duplicate = true;
					break;
				}
			}
			if (!duplicate)
			{
				resolved[i] = spellId;
			}
		}
		return resolved;
	}
}
```

- [ ] **Step 5: Enregistrer dans CMake**

Dans `src/CMakeLists.txt` : (a) ajouter `ActionBarLayout.cpp` aux listes sources client (à côté de `SpellKitCatalog.cpp` — chercher `SpellKitCatalog.cpp` et ajouter la ligne jumelle dans **chaque** liste où elle apparaît) ; (b) ajouter la cible de test après `spell_kit_library_tests` (~ligne 545) :

```cmake
  lcdlln_add_simple_test(action_bar_layout_tests
    ${CMAKE_SOURCE_DIR}/src/client/gameplay/ActionBarLayoutTests.cpp
    ${CMAKE_SOURCE_DIR}/src/client/gameplay/ActionBarLayout.cpp
    ${CMAKE_SOURCE_DIR}/src/client/gameplay/SpellKitCatalog.cpp)
```

- [ ] **Step 6: Lancer le test (succès attendu)**

Run : cible `action_bar_layout_tests`.
Expected: PASS — 4 `[OK]` + `[OK] ActionBarLayoutTests`.

- [ ] **Step 7: Commit**

```bash
git add src/client/gameplay/ActionBarLayout.h src/client/gameplay/ActionBarLayout.cpp src/client/gameplay/ActionBarLayoutTests.cpp src/CMakeLists.txt
git commit -m "feat(client): ResolveActionBarLayout (resolveur pur + tests)"
```

---

### Task 7: `GameplayUdpClient::SendSetActionBarLayout`

**Files:**
- Modify: `src/client/net/GameplayUdpClient.h`
- Modify: `src/client/net/GameplayUdpClient.cpp`

- [ ] **Step 1: Déclarer**

Dans `src/client/net/GameplayUdpClient.h`, après `SendCastRequest` (~ligne 66) :

```cpp
		/// Grimoire — envoie l'assignation des 10 slots de barre d'action.
		/// Le serveur valide (kit/unicité) et renvoie un ActionBarLayoutUpdate autoritaire.
		bool SendSetActionBarLayout(uint32_t clientId, const std::array<std::string, 10>& slots);
```

(Ajouter `#include <array>` en tête si absent.)

- [ ] **Step 2: Implémenter**

Dans `src/client/net/GameplayUdpClient.cpp`, après `SendCastRequest` (~ligne 319) :

```cpp
	bool GameplayUdpClient::SendSetActionBarLayout(uint32_t clientId, const std::array<std::string, 10>& slots)
	{
		engine::server::SetActionBarLayoutMessage msg{};
		msg.clientId = clientId;
		msg.slots = slots;
		const std::vector<std::byte> packet = engine::server::EncodeSetActionBarLayout(msg);
		const bool ok = SendBytes(packet);
		if (ok)
		{
			LOG_DEBUG(Net, "[GameplayUdpClient] SetActionBarLayout sent (client_id={})", clientId);
		}
		else
		{
			LOG_WARN(Net, "[GameplayUdpClient] SetActionBarLayout FAILED (client_id={})", clientId);
		}
		return ok;
	}
```

- [ ] **Step 3: Commit**

```bash
git add src/client/net/GameplayUdpClient.h src/client/net/GameplayUdpClient.cpp
git commit -m "feat(client): GameplayUdpClient::SendSetActionBarLayout"
```

---

### Task 8: UIModel — réception du layout autoritaire

**Files:**
- Modify: `src/client/ui_common/UIModel.h`
- Modify: `src/client/ui_common/UIModel.cpp`

- [ ] **Step 1: Champ modèle**

Dans `src/client/ui_common/UIModel.h`, dans `struct UIPlayerStats`, après `profileId;` :

```cpp
		/// Grimoire — layout autoritaire des 10 slots (slot i → spellId, "" = vide),
		/// reçu via ActionBarLayoutUpdate (enter-world / ACK serveur).
		std::array<std::string, 10> actionBarLayout{};
```

(Ajouter `#include <array>` en tête si absent.)

- [ ] **Step 2: Déclarer l'apply + le scratch**

Dans `src/client/ui_common/UIModel.h`, à côté de `ApplyCastBarUpdate` (déclaration) :

```cpp
		bool ApplyActionBarLayoutUpdate(std::span<const std::byte> packet);
```

Et un membre scratch à côté de `m_castBarUpdateMessage` :

```cpp
		engine::server::ActionBarLayoutUpdateMessage m_actionBarLayoutMessage{};
```

- [ ] **Step 3: Implémenter l'apply**

Dans `src/client/ui_common/UIModel.cpp`, après `ApplyCastBarUpdate` (~ligne 947) :

```cpp
	bool UIModelBinding::ApplyActionBarLayoutUpdate(std::span<const std::byte> packet)
	{
		if (!engine::server::DecodeActionBarLayoutUpdate(packet, m_actionBarLayoutMessage))
		{
			LOG_WARN(Net, "[UIModelBinding] ActionBarLayoutUpdate FAILED: decode error");
			return false;
		}
		m_model.playerStats.actionBarLayout = m_actionBarLayoutMessage.slots;
		LOG_INFO(Net, "[UIModelBinding] ActionBarLayoutUpdate applied (client_id={})",
			m_actionBarLayoutMessage.clientId);
		NotifyObservers(UIModelChangeStats);
		return true;
	}
```

- [ ] **Step 4: Brancher le dispatch**

Dans `src/client/ui_common/UIModel.cpp`, dans le `switch` sur `MessageKind` (à côté du `case ...CastBarUpdate:`, ~ligne 616) :

```cpp
		case engine::server::MessageKind::ActionBarLayoutUpdate:
			return ApplyActionBarLayoutUpdate(packet);
```

- [ ] **Step 5: Commit**

```bash
git add src/client/ui_common/UIModel.h src/client/ui_common/UIModel.cpp
git commit -m "feat(client): UIModel.actionBarLayout + ApplyActionBarLayoutUpdate (kind 89)"
```

---

### Task 9: Helper d'affichage clavier `KeyGlyph` (layout-aware)

**Files:**
- Modify: `src/client/app/Engine.cpp`

> Le **nom de config** reste stable (table `kRebindableKeys`, ex. « 1 ».. « 0 ») — on n'ajoute qu'un **glyphe d'affichage** layout-aware, sans toucher `KeyName`/`KeyFromName`.

- [ ] **Step 1: Ajouter `KeyGlyph` à côté de `KeyName`**

Dans `src/client/app/Engine.cpp`, après `KeyFromName` (~ligne 447) :

```cpp
		/// Glyphe d'AFFICHAGE d'une touche selon la disposition clavier active
		/// (AZERTY → la rangée du haut donne & é " ' ( - è _ ç à). Distinct de
		/// KeyName (nom de config stable/portable). Fallback : KeyName.
		std::string KeyGlyph(engine::platform::Key k)
		{
#if defined(_WIN32)
			const UINT vk = static_cast<UINT>(k); // l'enum Key == codes VK_* Win32
			const UINT ch = ::MapVirtualKeyW(vk, MAPVK_VK_TO_CHAR) & 0x7FFFu;
			if (ch != 0)
			{
				const wchar_t w = static_cast<wchar_t>(ch);
				char utf8[8] = {0};
				const int n = ::WideCharToMultiByte(CP_UTF8, 0, &w, 1, utf8, sizeof(utf8) - 1, nullptr, nullptr);
				if (n > 0)
				{
					return std::string(utf8, static_cast<size_t>(n));
				}
			}
#endif
			return KeyName(k);
		}
```

(Vérifier que `<windows.h>` est inclus dans Engine.cpp — il l'est pour l'input/WM_*. Sinon l'ajouter sous `#if defined(_WIN32)`.)

- [ ] **Step 2: Build (compile check)**

Run : build client (VS/CI).
Expected: compile OK. Pas de test unitaire (dépend de la disposition clavier de l'OS ; vérifié visuellement en jeu).

- [ ] **Step 3: Commit**

```bash
git add src/client/app/Engine.cpp
git commit -m "feat(client): KeyGlyph (glyphe touche layout-aware, AZERTY) sans casser KeyName"
```

---

### Task 10: Barre d'action 4 → 10 slots remappables, branchée sur le layout

**Files:**
- Modify: `src/client/app/Engine.cpp`

- [ ] **Step 1: Include du résolveur**

Dans `src/client/app/Engine.cpp`, ajouter en tête (zone includes) :

```cpp
#include "src/client/gameplay/ActionBarLayout.h"
```

- [ ] **Step 2: Remplacer la boucle barre d'action (4 → 10 + layout)**

Dans `src/client/app/Engine.cpp`, remplacer le bloc actuel (lignes ~10894-10977). Points modifiés : résolution du layout, `slotCount` plafonné à 10, touche par slot via `controls.keybind.action_slot_N` (défaut `Digit1..Digit0`), libellé via `KeyGlyph`, lookup du sort par spellId.

Remplacer :

```cpp
					const std::vector<engine::client::SpellDisplay>* actionKit =
						uiModel.playerStats.profileId.empty()
							? nullptr
							: m_spellCatalog.FindKit(uiModel.playerStats.profileId);
					if (actionKit != nullptr && !actionKit->empty())
					{
						const float nowSec = EngineNowSec();
						const float slotSize = 58.0f;
						const float slotGap = 8.0f;
						const size_t slotCount = std::min<size_t>(actionKit->size(), 4u);
```

par :

```cpp
					const std::vector<engine::client::SpellDisplay>* actionKit =
						uiModel.playerStats.profileId.empty()
							? nullptr
							: m_spellCatalog.FindKit(uiModel.playerStats.profileId);
					if (actionKit != nullptr && !actionKit->empty())
					{
						const float nowSec = EngineNowSec();
						const float slotSize = 58.0f;
						const float slotGap = 8.0f;
						// Grimoire — layout effectif des 10 slots (slot i → spellId).
						const std::array<std::string, 10> resolvedLayout =
							engine::client::ResolveActionBarLayout(uiModel.playerStats.actionBarLayout, *actionKit);
						const size_t slotCount = resolvedLayout.size(); // 10
```

- [ ] **Step 3: Adapter le corps de la boucle au layout + touches remappables**

Toujours dans le même bloc, remplacer l'accès au sort et le mapping touche. Remplacer :

```cpp
							const engine::client::SpellDisplay& spell = (*actionKit)[slotIndex];
							const float sx0 = barX + static_cast<float>(slotIndex) * (slotSize + slotGap);
```

par :

```cpp
							const std::string& slotSpellId = resolvedLayout[slotIndex];
							const engine::client::SpellDisplay* spellPtr =
								slotSpellId.empty() ? nullptr : engine::client::FindSpellInKit(*actionKit, slotSpellId);
							const float sx0 = barX + static_cast<float>(slotIndex) * (slotSize + slotGap);
							// Touche du slot (remappable) : défaut Digit1..Digit9 puis Digit0.
							const engine::platform::Key slotKey = KeyFromName(
								m_cfg.GetString("controls.keybind.action_slot_" + std::to_string(slotIndex + 1),
									(slotIndex < 9) ? std::string(1, static_cast<char>('1' + slotIndex)) : std::string("0")),
								(slotIndex < 9)
									? static_cast<engine::platform::Key>('1' + static_cast<int>(slotIndex))
									: engine::platform::Key::Digit0);
							if (spellPtr == nullptr)
							{
								// Slot vide : case grisée + glyphe touche, pas d'action.
								fg->AddRectFilled(ImVec2(sx0, barY), ImVec2(sx0 + slotSize, barY + slotSize),
									IM_COL32(14, 16, 22, 160), 6.0f);
								fg->AddRect(ImVec2(sx0, barY), ImVec2(sx0 + slotSize, barY + slotSize),
									IM_COL32(70, 70, 76, 160), 6.0f, 0, 2.0f);
								const std::string emptyKeyLabel = KeyGlyph(slotKey);
								fg->AddText(ImVec2(sx0 + 4.0f, barY + 2.0f), IM_COL32(150, 140, 110, 200), emptyKeyLabel.c_str());
								continue;
							}
							const engine::client::SpellDisplay& spell = *spellPtr;
```

Puis, plus bas dans la même itération, remplacer le libellé de touche. Remplacer :

```cpp
							const std::string keyLabel = std::to_string(slotIndex + 1);
```

par :

```cpp
							const std::string keyLabel = KeyGlyph(slotKey);
```

Et remplacer le test d'appui de touche. Remplacer :

```cpp
							// Touche 1-4 : envoi du CastRequest (Digit1..Digit4 = '1'..'4').
							if (keysAllowed
								&& m_input.WasPressed(static_cast<engine::platform::Key>('1' + static_cast<int>(slotIndex))))
```

par :

```cpp
							// Touche remappable du slot : envoi du CastRequest.
							if (keysAllowed && m_input.WasPressed(slotKey))
```

> Note : `slotCount` valant désormais 10, le calcul de `barWidth` existant (`slotGap * (slotCount - 1)`) reste valide (10 cases). Le `for (slotIndex < slotCount)` reste inchangé.

- [ ] **Step 4: Build + vérif visuelle**

Run : build client. Lancer le jeu, entrer en monde avec une classe → vérifier 10 slots, glyphes AZERTY (`& é " ' ( - è _ ç à`), cast via la rangée du haut, slots vides au-delà du kit.
Expected: barre à 10 slots, touches physiques AZERTY fonctionnelles.

- [ ] **Step 5: Commit**

```bash
git add src/client/app/Engine.cpp
git commit -m "feat(client): barre d'action 4->10 slots remappables + layout + glyphe AZERTY"
```

---

### Task 11: `GrimoireUiPresenter`

**Files:**
- Create: `src/client/grimoire/GrimoireUi.h`
- Create: `src/client/grimoire/GrimoireUi.cpp`
- Modify: `src/CMakeLists.txt`

- [ ] **Step 1: Header**

Create `src/client/grimoire/GrimoireUi.h` :

```cpp
#pragma once
// Grimoire / Carnet de techniques — presenter client du livre de sorts +
// assignation des 10 slots de barre d'action. Pas de rendu ImGui (cf.
// GrimoireImGuiRenderer). Lit le kit via SpellKitCatalog + le layout autoritaire
// du UIModel ; émet SetActionBarLayout via un callback (mis à jour optimiste).

#include "src/client/gameplay/SpellKitCatalog.h"

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace engine::client
{
	/// État snapshot exposé au renderer.
	struct GrimoireState
	{
		std::string profileId;                 ///< profil courant ("" = pas de barre).
		bool isCaster = false;                 ///< lanceur/healer/sacre → thème Grimoire.
		std::vector<SpellDisplay> spells;      ///< sorts connus (copie du kit).
		std::array<std::string, 10> slots{};   ///< layout résolu (slot i → spellId).
		std::string searchFilter;              ///< filtre de recherche courant (minuscule).
	};

	/// Presenter du Grimoire. Init() avant usage. Thread : main.
	class GrimoireUiPresenter final
	{
	public:
		GrimoireUiPresenter() = default;
		GrimoireUiPresenter(const GrimoireUiPresenter&) = delete;
		GrimoireUiPresenter& operator=(const GrimoireUiPresenter&) = delete;

		bool Init(const SpellKitCatalog* catalog);
		void Shutdown();
		bool IsInitialized() const { return m_initialized; }

		/// Callback d'envoi : (clientId, 10 slots) → true si émis.
		using SendCallback = std::function<bool(const std::array<std::string, 10>&)>;
		void SetSendCallback(SendCallback cb) { m_send = std::move(cb); }

		/// Recalcule l'état depuis le profil + le layout autoritaire du serveur.
		/// À appeler chaque frame (ou sur changement de stats). \p layout = layout
		/// reçu (UIModel) ; "" partout = défaut (ordre du kit).
		void Sync(const std::string& profileId, const std::array<std::string, 10>& serverLayout);

		/// Assigne \p spellId au \p slot (0-9) — mise à jour optimiste + envoi.
		/// Retire \p spellId d'un autre slot (unicité). spellId "" = vider le slot.
		void AssignSlot(uint32_t slot, const std::string& spellId);

		/// Met à jour le filtre de recherche (comparaison insensible à la casse).
		void SetSearchFilter(const std::string& filter);

		const GrimoireState& GetState() const { return m_state; }

	private:
		void RebuildSpells();

		bool m_initialized = false;
		const SpellKitCatalog* m_catalog = nullptr;
		GrimoireState m_state{};
		SendCallback m_send;
		uint32_t m_clientId = 0;
	};

	/// true si le profil est un profil de caster (lanceur/healer/sacre).
	bool IsCasterProfile(const std::string& profileId);
}
```

- [ ] **Step 2: Implémentation**

Create `src/client/grimoire/GrimoireUi.cpp` :

```cpp
#include "src/client/grimoire/GrimoireUi.h"
#include "src/client/gameplay/ActionBarLayout.h"
#include "src/shared/core/Log.h"

#include <algorithm>
#include <cctype>

namespace engine::client
{
	bool IsCasterProfile(const std::string& profileId)
	{
		return profileId == "lanceur" || profileId == "healer" || profileId == "sacre";
	}

	bool GrimoireUiPresenter::Init(const SpellKitCatalog* catalog)
	{
		if (m_initialized)
		{
			LOG_WARN(Core, "[GrimoireUiPresenter] Init ignored: already initialized");
			return true;
		}
		m_catalog = catalog;
		m_initialized = true;
		m_state = {};
		LOG_INFO(Core, "[GrimoireUiPresenter] Init OK");
		return true;
	}

	void GrimoireUiPresenter::Shutdown()
	{
		if (!m_initialized)
		{
			return;
		}
		m_initialized = false;
		m_state = {};
		LOG_INFO(Core, "[GrimoireUiPresenter] Destroyed");
	}

	void GrimoireUiPresenter::RebuildSpells()
	{
		m_state.spells.clear();
		if (m_catalog == nullptr || m_state.profileId.empty())
		{
			return;
		}
		const std::vector<SpellDisplay>* kit = m_catalog->FindKit(m_state.profileId);
		if (kit != nullptr)
		{
			m_state.spells = *kit;
		}
	}

	void GrimoireUiPresenter::Sync(const std::string& profileId, const std::array<std::string, 10>& serverLayout)
	{
		if (!m_initialized)
		{
			return;
		}
		if (profileId != m_state.profileId)
		{
			m_state.profileId = profileId;
			m_state.isCaster = IsCasterProfile(profileId);
			RebuildSpells();
		}
		m_state.slots = ResolveActionBarLayout(serverLayout, m_state.spells);
	}

	void GrimoireUiPresenter::AssignSlot(uint32_t slot, const std::string& spellId)
	{
		if (!m_initialized || slot >= m_state.slots.size())
		{
			return;
		}
		// Unicité : retire le sort d'un autre slot.
		if (!spellId.empty())
		{
			for (std::string& s : m_state.slots)
			{
				if (s == spellId)
				{
					s.clear();
				}
			}
		}
		m_state.slots[slot] = spellId; // mise à jour optimiste
		if (m_send)
		{
			(void)m_send(m_state.slots);
		}
	}

	void GrimoireUiPresenter::SetSearchFilter(const std::string& filter)
	{
		std::string lowered = filter;
		std::transform(lowered.begin(), lowered.end(), lowered.begin(),
			[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		m_state.searchFilter = lowered;
	}
}
```

- [ ] **Step 3: CMake**

Dans `src/CMakeLists.txt`, ajouter `src/client/grimoire/GrimoireUi.cpp` aux listes sources client (à côté de `src/client/skills/SkillBookUi.cpp` — dans **chaque** liste où SkillBookUi.cpp apparaît).

- [ ] **Step 4: Build**

Run : build client.
Expected: compile OK.

- [ ] **Step 5: Commit**

```bash
git add src/client/grimoire/GrimoireUi.h src/client/grimoire/GrimoireUi.cpp src/CMakeLists.txt
git commit -m "feat(client): GrimoireUiPresenter (kit + layout + assignation optimiste)"
```

---

### Task 12: `GrimoireImGuiRenderer` (drag&drop, scroll, recherche)

**Files:**
- Create: `src/client/render/GrimoireImGuiRenderer.h`
- Create: `src/client/render/GrimoireImGuiRenderer.cpp`
- Modify: `src/CMakeLists.txt`

- [ ] **Step 1: Header**

Create `src/client/render/GrimoireImGuiRenderer.h` :

```cpp
#pragma once
// Grimoire / Carnet de techniques — renderer ImGui. Lit GrimoireUiPresenter,
// propage le drag&drop d'assignation des slots. Aucun fetch/parse (presenter).

#include <cstdint>

namespace engine::client { class GrimoireUiPresenter; }

namespace engine::render
{
	class GrimoireImGuiRenderer
	{
	public:
		GrimoireImGuiRenderer() = default;

		void SetPresenter(engine::client::GrimoireUiPresenter* presenter) { m_presenter = presenter; }
		void SetEnabled(bool on) { m_enabled = on; }
		bool IsEnabled() const { return m_enabled; }
		void SetViewportSize(uint32_t w, uint32_t h) { m_viewportW = w; m_viewportH = h; }

		/// À appeler entre ImGui::NewFrame() et ImGui::Render() si presenter valide.
		void Render();

	private:
		engine::client::GrimoireUiPresenter* m_presenter = nullptr;
		bool m_enabled = false;
		uint32_t m_viewportW = 0;
		uint32_t m_viewportH = 0;
	};
}
```

- [ ] **Step 2: Implémentation**

Create `src/client/render/GrimoireImGuiRenderer.cpp` :

```cpp
#include "src/client/render/GrimoireImGuiRenderer.h"
#include "src/client/grimoire/GrimoireUi.h"

#if defined(_WIN32)
#include "imgui.h"

#include <algorithm>
#include <cstring>
#include <string>

namespace engine::render
{
	namespace
	{
		// Payload drag&drop : on transporte le spellId (chaîne courte, < 64).
		constexpr const char* kSpellPayloadId = "LN_GRIMOIRE_SPELL";
	}

	void GrimoireImGuiRenderer::Render()
	{
		if (m_presenter == nullptr || !m_enabled || !m_presenter->IsInitialized())
		{
			return;
		}
		const auto& state = m_presenter->GetState();

		const float panelW = 720.f;
		const float panelH = 540.f;
		const float vpW = (m_viewportW > 0) ? static_cast<float>(m_viewportW) : 1280.f;
		const float vpH = (m_viewportH > 0) ? static_cast<float>(m_viewportH) : 720.f;
		ImGui::SetNextWindowPos(ImVec2((vpW - panelW) * 0.5f, (vpH - panelH) * 0.5f), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowSize(ImVec2(panelW, panelH), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowBgAlpha(0.96f);

		const char* title = state.isCaster ? "Grimoire##ln_grimoire" : "Carnet de techniques##ln_grimoire";
		if (ImGui::Begin(title, nullptr, ImGuiWindowFlags_NoCollapse))
		{
			// --- Recherche
			static char searchBuf[64] = {0};
			if (ImGui::InputTextWithHint("##ln_grimoire_search", "Rechercher un sort...", searchBuf, sizeof(searchBuf)))
			{
				m_presenter->SetSearchFilter(searchBuf);
			}
			ImGui::Separator();

			ImGui::Columns(2, "##ln_grimoire_cols", true);

			// --- Colonne gauche : liste défilante des sorts (source du drag).
			ImGui::TextDisabled("%zu sorts connus", state.spells.size());
			ImGui::BeginChild("##ln_grimoire_list", ImVec2(0, 0), true);
			for (const engine::client::SpellDisplay& spell : state.spells)
			{
				// Filtre de recherche (insensible à la casse).
				if (!state.searchFilter.empty())
				{
					std::string lname = spell.name;
					std::transform(lname.begin(), lname.end(), lname.begin(),
						[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
					if (lname.find(state.searchFilter) == std::string::npos)
					{
						continue;
					}
				}
				ImGui::PushID(spell.spellId.c_str());
				const std::string label = spell.name + "  (coût " + std::to_string(spell.resourceCostPercent)
					+ "%, CD " + std::to_string(spell.cooldownMs / 1000u) + "s)";
				ImGui::Selectable(label.c_str());
				if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None))
				{
					ImGui::SetDragDropPayload(kSpellPayloadId, spell.spellId.c_str(),
						spell.spellId.size() + 1);
					ImGui::TextUnformatted(spell.name.c_str());
					ImGui::EndDragDropSource();
				}
				ImGui::PopID();
			}
			ImGui::EndChild();

			ImGui::NextColumn();

			// --- Colonne droite : 10 slots (cibles du drop).
			ImGui::TextDisabled("Barre d'action (10 slots)");
			for (uint32_t slotIndex = 0; slotIndex < state.slots.size(); ++slotIndex)
			{
				ImGui::PushID(static_cast<int>(slotIndex + 1000));
				const std::string& sid = state.slots[slotIndex];
				const engine::client::SpellDisplay* spell =
					sid.empty() ? nullptr : engine::client::FindSpellInKit(state.spells, sid);
				const std::string slotLabel = std::to_string(slotIndex + 1) + ".  "
					+ (spell ? spell->name : std::string("— vide —"));
				ImGui::Button(slotLabel.c_str(), ImVec2(-1.f, 30.f));
				if (ImGui::BeginDragDropTarget())
				{
					if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kSpellPayloadId))
					{
						const std::string dropped(static_cast<const char*>(payload->Data));
						m_presenter->AssignSlot(slotIndex, dropped);
					}
					ImGui::EndDragDropTarget();
				}
				ImGui::SameLine();
				if (!sid.empty() && ImGui::SmallButton("x"))
				{
					m_presenter->AssignSlot(slotIndex, std::string());
				}
				ImGui::PopID();
			}

			ImGui::Columns(1);
		}
		ImGui::End();
	}
}

#else  // !_WIN32 — stub no-op (pas d'ImGui hors client Windows).

namespace engine::render
{
	void GrimoireImGuiRenderer::Render() {}
}

#endif
```

- [ ] **Step 3: CMake**

Dans `src/CMakeLists.txt`, ajouter `src/client/render/GrimoireImGuiRenderer.cpp` aux listes sources client (à côté de `src/client/render/SkillBookImGuiRenderer.cpp`).

- [ ] **Step 4: Build**

Run : build client.
Expected: compile OK.

- [ ] **Step 5: Commit**

```bash
git add src/client/render/GrimoireImGuiRenderer.h src/client/render/GrimoireImGuiRenderer.cpp src/CMakeLists.txt
git commit -m "feat(client): GrimoireImGuiRenderer (liste defilante + recherche + drag&drop slots)"
```

---

### Task 13: Câblage Engine (membres, init, send, sync, toggle V, slash, render)

**Files:**
- Modify: `src/client/app/Engine.h`
- Modify: `src/client/app/Engine.cpp`

- [ ] **Step 1: Includes + membres Engine**

Dans `src/client/app/Engine.h`, includes :

```cpp
#include "src/client/grimoire/GrimoireUi.h"
#include "src/client/render/GrimoireImGuiRenderer.h"
```

Membres (à côté de `m_skillBookUi` / `m_skillBookImGui` / `m_skillBookVisible`) :

```cpp
	engine::client::GrimoireUiPresenter m_grimoireUi;
	std::unique_ptr<engine::render::GrimoireImGuiRenderer> m_grimoireImGui;
	bool m_grimoireVisible = false;
```

- [ ] **Step 2: Init presenter + send callback (UDP)**

Dans `src/client/app/Engine.cpp`, à côté de l'init de `m_skillBookUi` (~ligne 1552) :

```cpp
		if (!m_grimoireUi.Init(&m_spellCatalog))
		{
			LOG_WARN(Core, "[Boot] GrimoireUiPresenter init FAILED — panneau Grimoire desactive");
		}
		else
		{
			m_grimoireUi.SetSendCallback([this](const std::array<std::string, 10>& slots) -> bool {
				const uint32_t clientId = m_gameplayUdp.ServerClientId();
				if (clientId == 0u)
				{
					return false;
				}
				return m_gameplayUdp.SendSetActionBarLayout(clientId, slots);
			});
		}
```

- [ ] **Step 3: Créer le renderer**

À côté de la création de `m_skillBookImGui` (~ligne 8065) :

```cpp
				m_grimoireImGui = std::make_unique<engine::render::GrimoireImGuiRenderer>();
				m_grimoireImGui->SetPresenter(&m_grimoireUi);
```

- [ ] **Step 4: Shutdown**

À côté de `m_skillBookUi.Shutdown();` (~ligne 7539) :

```cpp
		m_grimoireUi.Shutdown();
```

- [ ] **Step 5: Toggle clavier remappable (défaut V) + slash**

À côté du toggle SkillBook (~ligne 7625), ajouter le toggle Grimoire :

```cpp
			const engine::platform::Key grimoireKey =
				KeyFromName(m_cfg.GetString("controls.keybind.grimoire", "V"), engine::platform::Key::V);
			if (inGameNoMenu && !chatBlocks && m_input.WasPressed(grimoireKey))
			{
				m_grimoireVisible = !m_grimoireVisible;
				LOG_INFO(Core, "[Engine] Grimoire toggle (visible={})", m_grimoireVisible);
			}
```

Pour les slash commands `/grimoire` et `/sorts` : repérer le `if`/`else if` qui gère `/skills` (~ligne 1860) et ajouter une branche jumelle :

```cpp
			else if (command == "/grimoire" || command == "/sorts")
			{
				m_grimoireVisible = !m_grimoireVisible;
				LOG_INFO(Core, "[Engine] /grimoire toggle (visible={})", m_grimoireVisible);
			}
```

- [ ] **Step 6: Sync + render conditionnel**

À côté du render SkillBook (~ligne 11765) :

```cpp
			m_grimoireUi.Sync(uiModel.playerStats.profileId, uiModel.playerStats.actionBarLayout);
			if (m_grimoireVisible && m_grimoireImGui && m_grimoireUi.IsInitialized())
			{
				m_grimoireImGui->SetEnabled(true);
				m_grimoireImGui->SetViewportSize(static_cast<uint32_t>(dw), static_cast<uint32_t>(dh));
				m_grimoireImGui->Render();
			}
```

- [ ] **Step 7: Build + vérif visuelle en jeu**

Run : build client. En jeu : **V** ouvre le Grimoire (titre adaptatif Grimoire/Carnet), liste défilante + recherche, glisser un sort sur un slot → la barre d'action reflète le changement, persistant après reconnexion.
Expected: panneau fonctionnel, drag&drop OK, persistance vérifiée.

- [ ] **Step 8: Commit**

```bash
git add src/client/app/Engine.h src/client/app/Engine.cpp
git commit -m "feat(client): cablage Grimoire (init UDP, sync, toggle V, /grimoire, render)"
```

---

### Task 14: Defaults de config + CODEBASE_MAP

**Files:**
- Modify: `game/data/settings/default_keybindings.json` (optionnel — documentaire)
- Modify: `CODEBASE_MAP.md`

- [ ] **Step 1: Documenter les binds**

Le runtime lit `controls.keybind.grimoire` (défaut « V ») et `controls.keybind.action_slot_1..10` (défaut `1..0`) directement via `m_cfg.GetString(..., défaut)` — **aucune entrée obligatoire** dans un fichier. Si une entrée documentaire est souhaitée dans `default_keybindings.json`, ajouter (sans casser le format existant action/displayName/key) :

```json
    { "action": "open_grimoire",   "displayName": "Open Grimoire",   "key": 86  }
```

(86 = VK_V. Note : ce fichier est déjà désynchronisé du code — cf. dette signalée au spec §7.6 ; l'ajout est purement documentaire.)

- [ ] **Step 2: CODEBASE_MAP**

Ajouter l'entrée « Grimoire (PR-2 client) » : `GrimoireUiPresenter` + `GrimoireImGuiRenderer`, résolveur `ActionBarLayout`, barre d'action 10 slots, binds `controls.keybind.grimoire`/`action_slot_N`, `KeyGlyph`.

- [ ] **Step 3: Commit**

```bash
git add game/data/settings/default_keybindings.json CODEBASE_MAP.md
git commit -m "docs: Grimoire PR-2 (CODEBASE_MAP + bind open_grimoire documentaire)"
```

---

### Task 15: PR-2

- [ ] **Step 1: Push + PR**

```bash
git push -u origin <branche-pr2>
```

Ouvrir la PR base `main` (après merge PR-1). Description : **déploiement lock-step avec PR-1** (client neuf ↔ shardd neuf). Inclure le rappel : merge PR-1 d'abord (CI verte), puis PR-2, puis déploiement simultané shardd + client.

---

## Self-review

- **Couverture spec** :
  - §2 objectif (référence + assignation + persistance) → Tasks 11/12 (UI), 1-4 (persistance serveur).
  - §5.1 layout 10 slots, unicité, validation kit → Task 1 (wire 10), Task 4 (validation serveur kit+unicité), Task 6 (résolveur + dédup défensif).
  - §5.2 défaut = ordre du kit si vide → Task 6 `ResolveActionBarLayout` + test `TestEmptyLayoutUsesKitOrder`.
  - §5.3 thème adaptatif → Task 11 `IsCasterProfile` + Task 12 titre adaptatif.
  - §6 serveur (persistance, restitution enter-world, handler) → Tasks 2/3/4.
  - §7.1 état client → Task 8 ; §7.2 barre 4→10 + touches → Task 10 ; §7.3/7.4 presenter/renderer → Tasks 11/12 ; §7.5 ouverture V + slash → Task 13 ; §7.6 keybind subsystem + `KeyGlyph` → Tasks 9/10/13.
  - §9 cas limites (1er login, spellId obsolète, doublon, catalogue vide) → Task 6 tests + résolveur ; catalogue vide → `FindKit` nullptr → `actionKit==nullptr` (barre masquée, déjà géré).
  - §10 tests → Task 1 (wire roundtrip+tronqué), Task 6 (résolveur). Persistance INI = patron verbatim, pas de cible de test repo (noté).
  - §11/§12 déploiement/PR → Tasks 5/15.
- **Placeholder scan** : aucun « TBD/TODO » ; chaque step de code montre le code complet.
- **Cohérence des types** : `std::array<std::string,10>` partout (wire, persistance, ConnectedClient, UIModel, presenter, résolveur) ; `ResolveActionBarLayout(layout, kit)` et `FindSpellInKit(kit, spellId)` signatures identiques entre Task 6 (def) et Tasks 10/11/12 (usage) ; kinds `SetActionBarLayout=88`/`ActionBarLayoutUpdate=89` cohérents Task 1 ↔ 4 ↔ 7 ↔ 8.
- **Déviation déploiement** : pas de bump `kProtocolVersion` (kinds rétro-additifs) — documentée en tête ; PR-1 = redéploiement shardd, PR-2 = lock-step.
