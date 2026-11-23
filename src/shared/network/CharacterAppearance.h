#pragma once

/// @file src/shared/network/CharacterAppearance.h
/// @brief Apparence d'un personnage (personnalisation choisie à la création),
///        partagée par le client, le master et le shard.
///
/// L'apparence est un indice par emplacement (visage, coiffure, couleurs, barbe,
/// cornes…) dans les listes d'options du catalogue client
/// `game/data/configuration/customization/appearance_catalog.json`, plus des
/// proportions corporelles en pourcentage. Le serveur ne connaît pas le
/// catalogue : il ne contrôle que la structure et les bornes génériques, puis
/// stocke et relaie le bloc tel quel ; le client ramène tout indice hors liste
/// sur la première option (aucun personnage ne peut devenir invisible).
///
/// Format binaire (versionné, extensible par ajout) :
///   u8 version (= kAppearanceVersion)
///   u8 nombre d'emplacements N, puis N × u8 (indices, ordre de AppearanceSlot)
///   u8 nombre de proportions P, puis P × u8 (pourcentages, ordre de AppearanceProp)
/// Un décodeur ignore les emplacements / proportions au-delà de ceux qu'il
/// connaît et complète les manquants par leur valeur par défaut : un client
/// plus ancien ou plus récent lit toujours un bloc valide.
/// Taille actuelle : 1 + 1 + 12 + 1 + 5 = 20 octets (colonne SQL VARBINARY(32)).
///
/// Header-only (fonctions inline) : utilisable par les binaires serveur qui
/// compilent leurs sources partagées par chemin, sans dépendre d'engine_core.

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace engine::network
{
	/// Version du format binaire de l'apparence.
	inline constexpr uint8_t kAppearanceVersion = 1;
	/// Taille maximale d'un bloc d'apparence sérialisé (borne de la colonne SQL).
	inline constexpr std::size_t kAppearanceMaxBytes = 32;
	/// Indice maximal accepté pour un emplacement (le catalogue en a beaucoup moins).
	inline constexpr uint8_t kAppearanceMaxOptionIndex = 63;
	/// Bornes des proportions (pourcentage de la valeur de référence de la race).
	inline constexpr uint8_t kAppearancePropMin = 85;
	inline constexpr uint8_t kAppearancePropMax = 115;

	/// Emplacements de personnalisation (l'ordre fait partie du format binaire :
	/// n'ajouter qu'en fin de liste).
	enum class AppearanceSlot : uint8_t
	{
		Face = 0,   ///< visage (tête, yeux, sourcils)
		SkinTone,   ///< teinte de peau
		Hair,       ///< coiffure
		HairColor,  ///< couleur des cheveux (et de la pilosité)
		EyeColor,   ///< couleur des yeux
		Beard,      ///< barbe
		Moustache,  ///< moustache
		Ears,       ///< forme des oreilles (elfes)
		Horns,      ///< cornes (démons)
		Tail,       ///< longueur de la queue (démons)
		Wings,      ///< ailes (démons ; option 0 = sans ailes)
		Tusks,      ///< défenses (orcs)
		Count
	};

	/// Proportions corporelles (pourcentages) ; même règle d'ordre.
	enum class AppearanceProp : uint8_t
	{
		Height = 0,     ///< taille
		LegLength,      ///< longueur des jambes
		TorsoWidth,     ///< largeur du torse
		ShoulderWidth,  ///< largeur des épaules
		BodyMass,       ///< corpulence
		Count
	};

	inline constexpr std::size_t kAppearanceSlotCount = static_cast<std::size_t>(AppearanceSlot::Count);
	inline constexpr std::size_t kAppearancePropCount = static_cast<std::size_t>(AppearanceProp::Count);

	/// Apparence complète d'un personnage.
	struct CharacterAppearance
	{
		std::array<uint8_t, kAppearanceSlotCount> slots{};  ///< indices d'options (0 par défaut)
		std::array<uint8_t, kAppearancePropCount> props{100, 100, 100, 100, 100};

		/// Indice d'option d'un emplacement.
		uint8_t Get(AppearanceSlot s) const { return slots[static_cast<std::size_t>(s)]; }
		/// Fixe l'indice d'option d'un emplacement (borné à kAppearanceMaxOptionIndex).
		void Set(AppearanceSlot s, uint8_t v)
		{
			slots[static_cast<std::size_t>(s)] = v > kAppearanceMaxOptionIndex ? kAppearanceMaxOptionIndex : v;
		}
		/// Proportion en pourcentage.
		uint8_t Proportion(AppearanceProp p) const { return props[static_cast<std::size_t>(p)]; }
		/// Fixe une proportion (bornée à [kAppearancePropMin, kAppearancePropMax]).
		void SetProportion(AppearanceProp p, uint8_t v)
		{
			v = v < kAppearancePropMin ? kAppearancePropMin : (v > kAppearancePropMax ? kAppearancePropMax : v);
			props[static_cast<std::size_t>(p)] = v;
		}
		/// Proportion sous forme de facteur (1.0 = référence de la race).
		float ProportionFactor(AppearanceProp p) const { return static_cast<float>(Proportion(p)) / 100.0f; }

		bool operator==(const CharacterAppearance& o) const { return slots == o.slots && props == o.props; }
		bool operator!=(const CharacterAppearance& o) const { return !(*this == o); }

		/// Sérialise au format binaire décrit en tête de fichier.
		std::vector<uint8_t> Encode() const
		{
			std::vector<uint8_t> out;
			out.reserve(3 + kAppearanceSlotCount + kAppearancePropCount);
			out.push_back(kAppearanceVersion);
			out.push_back(static_cast<uint8_t>(kAppearanceSlotCount));
			out.insert(out.end(), slots.begin(), slots.end());
			out.push_back(static_cast<uint8_t>(kAppearancePropCount));
			out.insert(out.end(), props.begin(), props.end());
			return out;
		}

		/// Désérialise un bloc. Retourne std::nullopt si le bloc est mal formé
		/// (version 0, longueur incohérente, taille > kAppearanceMaxBytes). Les
		/// valeurs hors bornes sont ramenées dans les bornes (jamais de rejet pour
		/// une simple valeur).
		static std::optional<CharacterAppearance> Decode(const uint8_t* data, std::size_t size)
		{
			if (data == nullptr || size < 2u || size > kAppearanceMaxBytes || data[0] == 0u)
				return std::nullopt;
			CharacterAppearance a;
			std::size_t pos = 1;
			const std::size_t nSlots = data[pos++];
			if (pos + nSlots > size)
				return std::nullopt;
			for (std::size_t i = 0; i < nSlots; ++i, ++pos)
			{
				if (i < kAppearanceSlotCount)
					a.Set(static_cast<AppearanceSlot>(i), data[pos]);
			}
			if (pos < size)
			{
				const std::size_t nProps = data[pos++];
				if (pos + nProps > size)
					return std::nullopt;
				for (std::size_t i = 0; i < nProps; ++i, ++pos)
				{
					if (i < kAppearancePropCount)
						a.SetProportion(static_cast<AppearanceProp>(i), data[pos]);
				}
			}
			return a;
		}

		/// Décodage tolérant : bloc vide ou mal formé -> apparence par défaut.
		static CharacterAppearance DecodeOrDefault(const std::vector<uint8_t>& bytes)
		{
			auto a = Decode(bytes.data(), bytes.size());
			return a ? *a : CharacterAppearance{};
		}
	};

	/// Hexadécimal majuscule (stockage SQL via UNHEX/HEX : aucun octet nul ne
	/// transite dans les chaînes des requêtes préparées).
	inline std::string AppearanceToHex(const std::vector<uint8_t>& bytes)
	{
		static constexpr char kDigits[] = "0123456789ABCDEF";
		std::string out;
		out.reserve(bytes.size() * 2u);
		for (uint8_t b : bytes)
		{
			out.push_back(kDigits[b >> 4]);
			out.push_back(kDigits[b & 0x0Fu]);
		}
		return out;
	}

	/// Inverse d'AppearanceToHex. Chaîne vide, de longueur impaire, trop longue
	/// ou non hexadécimale -> vecteur vide (apparence par défaut).
	inline std::vector<uint8_t> AppearanceFromHex(std::string_view hex)
	{
		std::vector<uint8_t> out;
		if (hex.empty() || (hex.size() % 2u) != 0u || hex.size() > kAppearanceMaxBytes * 2u)
			return out;
		auto nibble = [](char c) -> int {
			if (c >= '0' && c <= '9') return c - '0';
			if (c >= 'A' && c <= 'F') return c - 'A' + 10;
			if (c >= 'a' && c <= 'f') return c - 'a' + 10;
			return -1;
		};
		out.reserve(hex.size() / 2u);
		for (std::size_t i = 0; i < hex.size(); i += 2u)
		{
			const int hi = nibble(hex[i]);
			const int lo = nibble(hex[i + 1u]);
			if (hi < 0 || lo < 0)
				return {};
			out.push_back(static_cast<uint8_t>((hi << 4) | lo));
		}
		return out;
	}

	/// Normalise un bloc reçu d'un client : décodé (bornes appliquées) puis
	/// ré-encodé ; un bloc absent ou mal formé donne l'apparence par défaut.
	inline std::vector<uint8_t> NormalizeAppearance(const std::vector<uint8_t>& bytes)
	{
		return CharacterAppearance::DecodeOrDefault(bytes).Encode();
	}
}
