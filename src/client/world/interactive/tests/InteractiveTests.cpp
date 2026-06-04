// M100.32 — Tests Interactive Props : round-trip binaire + simulateur
// (animation des 5 types + compensation de latence) + round-trip réseau
// (3 messages) + relai serveur (no-validation + sync initial).
//
// Headless. Lié à engine_core (InteractiveSimulator + InteractivePayloads) ;
// format binaire + InteractiveStateRelay header-only.

#include "src/client/world/interactive/InteractiveInstances.h"
#include "src/client/world/interactive/InteractiveSimulator.h"
#include "src/client/world/interactive/InteractiveTypes.h"
#include "src/masterd/InteractiveStateRelay.h"
#include "src/shared/network/InteractivePayloads.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace engine::world::interactive;
using namespace engine::network;

namespace
{
	int g_failed = 0;

#define REQUIRE(cond) do { \
	if (!(cond)) { \
		std::fprintf(stderr, "[FAIL] %s:%d  %s\n", __FILE__, __LINE__, #cond); \
		++g_failed; \
	} \
} while (0)

	bool Near(float a, float b, float eps = 1e-4f) { return (a - b < eps) && (b - a < eps); }

	InteractivePropInstance MakeDef(InteractiveType type, float openVal, float dur)
	{
		InteractivePropInstance d;
		d.id = 1;
		d.type = type;
		d.openAngleDeg = openVal;
		d.animDurationSec = dur;
		d.initialState = 0;
		return d;
	}

	// ---------------------------------------------------------------------
	// Round-trip binaire interactives.bin
	// ---------------------------------------------------------------------
	void Test_Interactives_Roundtrip()
	{
		std::vector<InteractivePropInstance> in;
		InteractivePropInstance a;
		a.id = 42; a.type = InteractiveType::DoorHinge;
		a.position = { 1.0f, 2.0f, 3.0f }; a.rotationY = 1.57f;
		a.meshAssetId = 0xABCDu;
		a.pivotLocal = { -0.4f, 0.0f, 0.0f }; a.axisLocal = { 0.0f, 1.0f, 0.0f };
		a.openAngleDeg = 90.0f; a.animDurationSec = 0.5f; a.initialState = 0;
		a.audioOpenEvent = "door_creak_open"; a.audioCloseEvent = "door_thud_close";

		InteractivePropInstance b;
		b.id = 7; b.type = InteractiveType::DoorSliding;
		b.openAngleDeg = 2.5f; b.animDurationSec = 1.0f; b.initialState = 1;
		b.audioOpenEvent = ""; b.audioCloseEvent = "slide";

		InteractivePropInstance c;
		c.id = 99; c.type = InteractiveType::ChestSimple;
		c.meshAssetId = 5;

		in = { a, b, c };
		std::vector<uint8_t> bytes = SaveInteractivesBin(in);
		std::vector<InteractivePropInstance> out;
		std::string err;
		REQUIRE(LoadInteractivesBin(bytes, out, err));
		REQUIRE(out.size() == 3);
		if (out.size() == 3)
		{
			REQUIRE(out[0].id == 42 && out[0].type == InteractiveType::DoorHinge);
			REQUIRE(Near(out[0].position.x, 1.0f) && Near(out[0].position.z, 3.0f));
			REQUIRE(Near(out[0].rotationY, 1.57f));
			REQUIRE(out[0].meshAssetId == 0xABCDu);
			REQUIRE(Near(out[0].pivotLocal.x, -0.4f));
			REQUIRE(Near(out[0].openAngleDeg, 90.0f));
			REQUIRE(out[0].audioOpenEvent == "door_creak_open");
			REQUIRE(out[0].audioCloseEvent == "door_thud_close");
			REQUIRE(out[1].type == InteractiveType::DoorSliding);
			REQUIRE(out[1].initialState == 1);
			REQUIRE(out[1].audioOpenEvent.empty() && out[1].audioCloseEvent == "slide");
			REQUIRE(out[2].id == 99 && out[2].type == InteractiveType::ChestSimple);
		}
	}

	// ---------------------------------------------------------------------
	// Simulateur — animations
	// ---------------------------------------------------------------------
	void Test_Simulator_AnimatesDoorHinge()
	{
		InteractivePropInstance d = MakeDef(InteractiveType::DoorHinge, 90.0f, 0.5f);
		InteractiveRuntimeState rt = MakeInitialRuntimeState(d);
		REQUIRE(Near(rt.openFactor, 0.0f));
		REQUIRE(Near(ComputeOpenAngleDeg(d, rt), 0.0f));

		ToggleInteractive(rt);
		REQUIRE(rt.targetState == 1);
		UpdateInteractive(rt, d, 0.25f); // mi-course
		REQUIRE(Near(rt.openFactor, 0.5f));
		REQUIRE(Near(ComputeOpenAngleDeg(d, rt), 45.0f));
		UpdateInteractive(rt, d, 0.5f); // dépasse la fin → clampé
		REQUIRE(Near(rt.openFactor, 1.0f));
		REQUIRE(Near(ComputeOpenAngleDeg(d, rt), 90.0f));
		REQUIRE(!IsAnimating(rt));

		// Fermeture.
		ToggleInteractive(rt);
		UpdateInteractive(rt, d, 0.5f);
		REQUIRE(Near(rt.openFactor, 0.0f));
		REQUIRE(Near(ComputeOpenAngleDeg(d, rt), 0.0f));
	}

	void Test_Simulator_AnimatesSliding()
	{
		InteractivePropInstance d = MakeDef(InteractiveType::DoorSliding, 2.0f, 1.0f); // 2 m
		InteractiveRuntimeState rt = MakeInitialRuntimeState(d);
		REQUIRE(Near(ComputeOpenAngleDeg(d, rt), 0.0f)); // pas un type rotatif
		ToggleInteractive(rt);
		UpdateInteractive(rt, d, 0.5f);
		REQUIRE(Near(ComputeSlideOffsetMeters(d, rt), 1.0f)); // mi-course = 1 m
		UpdateInteractive(rt, d, 0.5f);
		REQUIRE(Near(ComputeSlideOffsetMeters(d, rt), 2.0f));
	}

	void Test_Simulator_AnimatesTrapdoor()
	{
		InteractivePropInstance d = MakeDef(InteractiveType::Trapdoor, 80.0f, 0.4f);
		InteractiveRuntimeState rt = MakeInitialRuntimeState(d);
		ToggleInteractive(rt);
		UpdateInteractive(rt, d, 0.4f);
		REQUIRE(Near(rt.openFactor, 1.0f));
		REQUIRE(Near(ComputeOpenAngleDeg(d, rt), 80.0f));
		REQUIRE(Near(ComputeSlideOffsetMeters(d, rt), 0.0f)); // pas un slider
	}

	void Test_Simulator_AnimatesChest()
	{
		InteractivePropInstance d = MakeDef(InteractiveType::ChestSimple, 60.0f, 0.3f);
		InteractiveRuntimeState rt = MakeInitialRuntimeState(d);
		ToggleInteractive(rt);
		UpdateInteractive(rt, d, 0.15f); // mi-course
		REQUIRE(Near(ComputeOpenAngleDeg(d, rt), 30.0f));
		UpdateInteractive(rt, d, 0.15f);
		REQUIRE(Near(ComputeOpenAngleDeg(d, rt), 60.0f));
	}

	// ---------------------------------------------------------------------
	// Compensation de latence
	// ---------------------------------------------------------------------
	void Test_Client_RemoteAnimationLatencyCompensation()
	{
		InteractivePropInstance d = MakeDef(InteractiveType::DoorHinge, 90.0f, 0.5f);
		InteractiveRuntimeState rt = MakeInitialRuntimeState(d); // fermé, openFactor=0

		// Évènement distant survenu 0.25 s plus tôt (50% de la durée).
		ApplyRemoteState(rt, d, /*newState=*/1, /*latencySec=*/0.25f);
		// La porte ne saute pas à 0 ni à 1 : elle démarre déjà à mi-course.
		REQUIRE(rt.targetState == 1);
		REQUIRE(Near(rt.openFactor, 0.5f));
		REQUIRE(IsAnimating(rt));

		// Le reste de l'animation aboutit à l'ouverture complète.
		UpdateInteractive(rt, d, 0.25f);
		REQUIRE(Near(rt.openFactor, 1.0f));

		// Latence supérieure à la durée → clamp à la cible (pas de dépassement).
		InteractiveRuntimeState rt2 = MakeInitialRuntimeState(d);
		ApplyRemoteState(rt2, d, 1, 5.0f);
		REQUIRE(Near(rt2.openFactor, 1.0f));
	}

	// ---------------------------------------------------------------------
	// Round-trip réseau (3 messages)
	// ---------------------------------------------------------------------
	void Test_Network_StateChangeRoundtrip()
	{
		// StateChange
		{
			auto buf = BuildInteractiveStateChangePayload(0x1122334455667788ull, 1, 0xDEADBEEFull);
			auto p = ParseInteractiveStateChangePayload(buf.data(), buf.size());
			REQUIRE(p.has_value());
			if (p) { REQUIRE(p->id == 0x1122334455667788ull); REQUIRE(p->newState == 1); REQUIRE(p->clientTimeMs == 0xDEADBEEFull); }
		}
		// Broadcast
		{
			auto buf = BuildInteractiveStateBroadcastPayload(7ull, 0, 123456ull);
			auto p = ParseInteractiveStateBroadcastPayload(buf.data(), buf.size());
			REQUIRE(p.has_value());
			if (p) { REQUIRE(p->id == 7ull); REQUIRE(p->newState == 0); REQUIRE(p->serverTimeMs == 123456ull); }
		}
		// Sync
		{
			std::vector<InteractiveSyncEntry> entries = { {1ull, 1}, {2ull, 0}, {99ull, 1} };
			auto buf = BuildInteractiveStateSyncPayload(entries);
			auto p = ParseInteractiveStateSyncPayload(buf.data(), buf.size());
			REQUIRE(p.has_value());
			if (p)
			{
				REQUIRE(p->entries.size() == 3);
				REQUIRE(p->entries[0].id == 1ull && p->entries[0].state == 1);
				REQUIRE(p->entries[2].id == 99ull && p->entries[2].state == 1);
			}
		}
		// Malformé (trop court) → nullopt.
		{
			std::vector<uint8_t> tooShort = { 0x01, 0x02 };
			REQUIRE(!ParseInteractiveStateChangePayload(tooShort.data(), tooShort.size()).has_value());
		}
	}

	// ---------------------------------------------------------------------
	// Relai serveur — pas de validation gameplay
	// ---------------------------------------------------------------------
	void Test_Server_NoGameplayValidation()
	{
		engine::server::interactive::InteractiveStateRelay relay;
		relay.Seed(10, 0);
		relay.Seed(20, 1);
		REQUIRE(relay.Count() == 2);

		// id connu : appliqué.
		REQUIRE(relay.ApplyStateChange(10, 1) == engine::server::interactive::ChangeResult::Applied);
		bool found = false;
		REQUIRE(relay.GetState(10, &found) == 1 && found);

		// id inconnu : ignoré (UnknownId), aucune erreur, count inchangé.
		REQUIRE(relay.ApplyStateChange(999, 1) == engine::server::interactive::ChangeResult::UnknownId);
		REQUIRE(relay.Count() == 2);
		relay.GetState(999, &found);
		REQUIRE(!found);
	}

	void Test_Server_InitialSyncOnConnect()
	{
		engine::server::interactive::InteractiveStateRelay relay;
		relay.Seed(1, 0);
		relay.Seed(2, 0);
		relay.Seed(3, 1);
		// Un joueur ouvre l'objet 2.
		relay.ApplyStateChange(2, 1);

		auto snap = relay.Snapshot();
		REQUIRE(snap.size() == 3);
		// Vérifie les états (ordre non garanti — on recherche par id).
		int seen = 0;
		for (const auto& [id, st] : snap)
		{
			if (id == 1) { REQUIRE(st == 0); ++seen; }
			if (id == 2) { REQUIRE(st == 1); ++seen; }
			if (id == 3) { REQUIRE(st == 1); ++seen; }
		}
		REQUIRE(seen == 3);
	}
}

int main()
{
	Test_Interactives_Roundtrip();
	Test_Simulator_AnimatesDoorHinge();
	Test_Simulator_AnimatesSliding();
	Test_Simulator_AnimatesTrapdoor();
	Test_Simulator_AnimatesChest();
	Test_Client_RemoteAnimationLatencyCompensation();
	Test_Network_StateChangeRoundtrip();
	Test_Server_NoGameplayValidation();
	Test_Server_InitialSyncOnConnect();

	if (g_failed == 0)
		std::printf("[interactive_tests] all tests passed\n");
	else
		std::fprintf(stderr, "[interactive_tests] %d check(s) failed\n", g_failed);
	return g_failed;
}
