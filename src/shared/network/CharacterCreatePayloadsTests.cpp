/**
 * Système de personnages PR2 — tests round-trip du payload CHARACTER_CREATE_REQUEST
 * avec le nouveau champ factionId (et vérification de la rétro-compat sans factionId).
 *
 * Tests d'encodage purs — pas de DB / pas de réseau. Robustes sous NDEBUG :
 * on n'utilise pas assert() mais un retour 0 (succès) / 1 (premier échec).
 */

#include "src/shared/network/CharacterPayloads.h"
#include "src/shared/network/ProtocolV1Constants.h"

#include <cstdio>
#include <string>

using namespace engine::network;

namespace
{
	// Vérifie une condition ; en cas d'échec, imprime sur stderr et signale l'échec
	// via *fail (drapeau partagé) — NDEBUG-safe (pas d'assert).
	void Check(bool cond, const char* msg, int* fail)
	{
		if (!cond)
		{
			std::fprintf(stderr, "[FAIL] %s\n", msg);
			*fail = 1;
		}
	}

	// Round-trip complet : build avec factionId, parse, vérifie tous les champs.
	void TestRoundTripWithFaction(int* fail)
	{
		CharacterCustomization custom{};
		custom.faceType  = 3;
		custom.hairStyle = 7;

		auto buf = BuildCharacterCreateRequestPayload(
			"Hero", "elfes", "voleur_tenebreux", custom, "female", "elfe");
		Check(!buf.empty(), "build (avec factionId) non vide", fail);

		auto parsed = ParseCharacterCreateRequestPayload(buf.data(), buf.size());
		Check(parsed.has_value(), "parse (avec factionId) OK", fail);
		if (parsed)
		{
			Check(parsed->name == "Hero", "name round-trip", fail);
			Check(parsed->raceId == "elfes", "raceId round-trip", fail);
			Check(parsed->classId == "voleur_tenebreux", "classId round-trip", fail);
			Check(parsed->customization.faceType == 3, "faceType round-trip", fail);
			Check(parsed->customization.hairStyle == 7, "hairStyle round-trip", fail);
			Check(parsed->gender == "female", "gender round-trip", fail);
			Check(parsed->factionId == "elfe", "factionId round-trip", fail);
		}
	}

	// Rétro-compat : build SANS factionId (arg défaut omis), parse, vérifie que
	// factionId est vide et que les autres champs sont intacts.
	void TestBackwardCompatNoFaction(int* fail)
	{
		CharacterCustomization custom{};
		custom.skinColorIdx = 2;

		// Builder appelé sans le nouvel argument factionId (valeur par défaut {}).
		auto buf = BuildCharacterCreateRequestPayload(
			"Hero", "elfes", "voleur_tenebreux", custom, "female");
		Check(!buf.empty(), "build (sans factionId) non vide", fail);

		auto parsed = ParseCharacterCreateRequestPayload(buf.data(), buf.size());
		Check(parsed.has_value(), "parse (sans factionId) OK", fail);
		if (parsed)
		{
			Check(parsed->name == "Hero", "name intact", fail);
			Check(parsed->raceId == "elfes", "raceId intact", fail);
			Check(parsed->classId == "voleur_tenebreux", "classId intact", fail);
			Check(parsed->customization.skinColorIdx == 2, "skinColorIdx intact", fail);
			Check(parsed->gender == "female", "gender intact", fail);
			Check(parsed->factionId.empty(), "factionId vide quand omis", fail);
			Check(parsed->appearance.empty(), "apparence vide quand omise", fail);
		}
	}

	// Personnalisation : bloc d'apparence appendu au payload de création.
	void TestAppearanceRoundTrip(int* fail)
	{
		CharacterAppearance a;
		a.Set(AppearanceSlot::Hair, 4);
		a.Set(AppearanceSlot::HairColor, 2);
		a.Set(AppearanceSlot::Beard, 3);
		a.Set(AppearanceSlot::Horns, 1);
		a.Set(AppearanceSlot::Wings, 2);
		a.SetProportion(AppearanceProp::Height, 108);
		const std::vector<uint8_t> bytes = a.Encode();
		Check(bytes.size() <= kAppearanceMaxBytes, "bloc d'apparence <= 32 octets", fail);

		auto buf = BuildCharacterCreateRequestPayload("Hero", "demons", "demoniste", CharacterCustomization{},
			"male", "legion", bytes);
		auto parsed = ParseCharacterCreateRequestPayload(buf.data(), buf.size());
		Check(parsed.has_value(), "parse (avec apparence) OK", fail);
		if (parsed)
		{
			Check(parsed->factionId == "legion", "factionId intact avant l'apparence", fail);
			Check(parsed->appearance == bytes, "octets d'apparence round-trip", fail);
			auto back = CharacterAppearance::Decode(parsed->appearance.data(), parsed->appearance.size());
			Check(back.has_value() && *back == a, "apparence décodée identique", fail);
		}
	}

	// Décodage tolérant : bornes, bloc tronqué, emplacements inconnus ignorés.
	void TestAppearanceDecodeRules(int* fail)
	{
		Check(!CharacterAppearance::Decode(nullptr, 0).has_value(), "bloc vide rejeté", fail);
		const uint8_t truncated[] = { 1, 12, 1, 2 };
		Check(!CharacterAppearance::Decode(truncated, sizeof(truncated)).has_value(), "bloc tronqué rejeté", fail);
		// Version future avec 14 emplacements et 6 proportions : les excédents sont ignorés.
		std::vector<uint8_t> future = { 2, 14 };
		for (int i = 0; i < 14; ++i) future.push_back(static_cast<uint8_t>(i == 2 ? 200 : 1));
		future.push_back(6);
		for (int i = 0; i < 6; ++i) future.push_back(i == 0 ? 50 : 110);
		auto a = CharacterAppearance::Decode(future.data(), future.size());
		Check(a.has_value(), "bloc d'une version future accepté", fail);
		if (a)
		{
			Check(a->Get(AppearanceSlot::Hair) == kAppearanceMaxOptionIndex, "indice borné", fail);
			Check(a->Proportion(AppearanceProp::Height) == kAppearancePropMin, "proportion bornée", fail);
			Check(a->Proportion(AppearanceProp::BodyMass) == 110, "proportion conservée", fail);
		}
		Check(CharacterAppearance::DecodeOrDefault({}) == CharacterAppearance{}, "défaut si absent", fail);
	}

	// Liste des personnages : l'apparence est relue entrée par entrée.
	void TestListEntryAppearance(int* fail)
	{
		CharacterListEntry e;
		e.character_id = 42;
		e.name = "Hero";
		e.race_str = "orcs";
		e.gender = "female";
		e.faction_str = "dzorak";
		CharacterAppearance a;
		a.Set(AppearanceSlot::Tusks, 2);
		e.appearance = a.Encode();
		CharacterListEntry e2 = e;
		e2.character_id = 43;
		e2.appearance.clear();
		auto pkt = BuildCharacterListResponsePacket(1, { e, e2 }, 7, 9);
		Check(!pkt.empty(), "build liste non vide", fail);
		// Le payload commence après l'en-tête fixe du protocole v1.
		Check(pkt.size() > kProtocolV1HeaderSize, "paquet plus long que l'en-tête", fail);
		if (pkt.size() <= kProtocolV1HeaderSize)
			return;
		auto parsed = ParseCharacterListResponsePayload(pkt.data() + kProtocolV1HeaderSize,
			pkt.size() - kProtocolV1HeaderSize);
		Check(parsed.has_value() && parsed->entries.size() == 2u, "liste relue", fail);
		if (parsed && parsed->entries.size() == 2u)
		{
			Check(parsed->entries[0].character_id == 42u, "id entrée 1", fail);
			Check(parsed->entries[0].appearance == e.appearance, "apparence entrée 1", fail);
			Check(parsed->entries[1].appearance.empty(), "apparence entrée 2 vide", fail);
		}
	}
}

int main()
{
	int fail = 0;
	TestRoundTripWithFaction(&fail);
	TestBackwardCompatNoFaction(&fail);
	TestAppearanceRoundTrip(&fail);
	TestAppearanceDecodeRules(&fail);
	TestListEntryAppearance(&fail);
	return fail;
}
