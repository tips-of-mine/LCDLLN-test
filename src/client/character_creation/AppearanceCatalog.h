#pragma once

/// @file src/client/character_creation/AppearanceCatalog.h
/// @brief Catalogue de personnalisation des personnages (client).
///
/// Lit `game/data/configuration/customization/appearance_catalog.json`, généré
/// par `tools/asset_pipeline/character_appearance/build_character_appearance.py`.
/// Pour chaque race jouable et chaque genre, le catalogue donne le corps de base,
/// les maillages du corps à masquer (capuche) et, par emplacement
/// (engine::network::AppearanceSlot), la liste ordonnée des options : pièces .glb
/// par niveau de détail (LOD0/1/2) ou couleurs (texture + teinte d'affichage).
///
/// Une apparence (engine::network::CharacterAppearance) est un indice par
/// emplacement dans ces listes. Resolve() la traduit en chemins concrets en
/// ramenant tout indice hors liste sur la première option : un personnage reste
/// toujours affichable, même si le catalogue a évolué depuis sa création.
///
/// Logique pure (aucune dépendance Vulkan) : testable en ctest.

#include "src/shared/network/CharacterAppearance.h"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace engine::core
{
	class Config;
}

namespace engine::client
{
	/// Une option d'un emplacement (pièce ou couleur).
	struct AppearanceOption
	{
		std::string id;                  ///< identifiant stable (ex. « long », « roux »)
		std::string name;                ///< libellé affiché (français)
		std::vector<std::string> lods;   ///< pièces .glb LOD0..2 (vide = aucune pièce, ex. « Rasé »)
		std::string texture;             ///< couleurs : texture BaseColor (cheveux, yeux, peau)
		std::string hex;                 ///< couleurs : teinte d'affichage « #RRGGBB » (pastille de l'UI)
		uint32_t tintRgba8 = 0xFFFFFFFFu; ///< couleurs : teinte multiplicative du matériau (cf. MaterialFlags::Tint)
	};

	/// Données d'un couple race × genre.
	struct AppearanceRaceGender
	{
		std::string body;                              ///< corps de base (.glb, relatif à game/data)
		std::vector<std::string> hiddenBodyMeshes;     ///< sous-chaînes de noms de maillages du corps à masquer
		std::array<std::vector<AppearanceOption>, engine::network::kAppearanceSlotCount> slots;

		/// Options d'un emplacement (vide si la race n'a pas cet emplacement).
		const std::vector<AppearanceOption>& Options(engine::network::AppearanceSlot s) const
		{
			return slots[static_cast<std::size_t>(s)];
		}
	};

	/// Apparence résolue en chemins concrets (après bornage des indices).
	struct ResolvedAppearance
	{
		std::string body;                         ///< corps de base
		std::vector<std::string> hiddenBodyMeshes;
		std::vector<std::string> parts;           ///< pièces .glb du LOD demandé (visage, coiffure…)
		std::string skinTexture;                  ///< BaseColor de la peau
		std::string hairTexture;                  ///< BaseColor des cheveux (et pilosité, sourcils)
		std::string eyeTexture;                   ///< BaseColor des yeux
		uint32_t skinTint = 0xFFFFFFFFu;          ///< teintes RGBA8 (R = octet de poids faible)
		uint32_t hairTint = 0xFFFFFFFFu;
		uint32_t eyeTint = 0xFFFFFFFFu;
		float heightScale = 1.0f;                 ///< facteur de taille
		float widthScale = 1.0f;                  ///< facteur de largeur (torse × épaules × corpulence)
	};

	/// Catalogue de personnalisation (cf. en-tête de fichier).
	class AppearanceCatalog final
	{
	public:
		/// Chemin du catalogue relatif à `paths.content`.
		static constexpr const char* kRelativePath = "configuration/customization/appearance_catalog.json";

		/// Charge le catalogue depuis `paths.content`. Retourne false (catalogue vide)
		/// si le fichier est absent ou invalide — non bloquant : le client garde
		/// alors l'ancien rendu (corps seul). Main thread.
		bool Init(const engine::core::Config& config);

		/// Variante testable sans I/O : charge un texte JSON en mémoire.
		bool LoadFromText(std::string_view jsonText, std::string& outError);

		/// Vrai si au moins une race est chargée.
		bool IsLoaded() const { return !m_entries.empty(); }

		/// Données d'une race et d'un genre, ou nullptr.
		const AppearanceRaceGender* Find(const std::string& raceId, const std::string& gender) const;

		/// Races présentes dans le catalogue (ordre du fichier).
		const std::vector<std::string>& Races() const { return m_races; }

		/// Libellé d'un emplacement (« Coiffure », « Cornes »…).
		const std::string& SlotLabel(engine::network::AppearanceSlot s) const
		{
			return m_slotLabels[static_cast<std::size_t>(s)];
		}

		/// Noms des matériaux glTF portés par les pièces (routage des matériaux).
		const std::vector<std::string>& SkinMaterialNames() const { return m_skinMaterials; }
		const std::string& HairMaterialName() const { return m_hairMaterial; }
		const std::string& EyeMaterialName() const { return m_eyeMaterial; }
		const std::string& HornMaterialName() const { return m_hornMaterial; }
		const std::string& TuskMaterialName() const { return m_tuskMaterial; }
		const std::string& HornTexture() const { return m_hornTexture; }
		const std::string& TuskTexture() const { return m_tuskTexture; }

		/// Ramène chaque indice de \p a dans les bornes des options de la race et du
		/// genre (indice hors liste -> 0). Sans effet si la race est inconnue.
		void Sanitize(const std::string& raceId, const std::string& gender,
		              engine::network::CharacterAppearance& a) const;

		/// Résout une apparence en chemins (LOD \p lod borné à [0, 2]). Race ou genre
		/// inconnus -> ResolvedAppearance vide (body vide) : l'appelant garde son repli.
		ResolvedAppearance Resolve(const std::string& raceId, const std::string& gender,
		                           const engine::network::CharacterAppearance& a, int lod) const;

		/// Tous les chemins de fichiers référencés (pièces, corps, textures) — sert au
		/// contrôle d'intégrité (tests) et au préchargement.
		std::vector<std::string> AllReferencedFiles() const;

	private:
		static std::string Key(const std::string& raceId, const std::string& gender) { return raceId + "|" + gender; }

		std::unordered_map<std::string, AppearanceRaceGender> m_entries;
		std::vector<std::string> m_races;
		std::array<std::string, engine::network::kAppearanceSlotCount> m_slotLabels;
		std::vector<std::string> m_skinMaterials;
		std::string m_hairMaterial = "MI_Hair";
		std::string m_eyeMaterial = "MI_Eye";
		std::string m_hornMaterial = "MI_Horn";
		std::string m_tuskMaterial = "MI_Tusk";
		std::string m_hornTexture;
		std::string m_tuskTexture;
	};

	/// Nom de l'emplacement dans le catalogue JSON (« face », « hairColor »…).
	const char* AppearanceSlotKey(engine::network::AppearanceSlot s);

	/// « #RRGGBB » -> RGBA8 (R dans l'octet de poids faible, alpha 255). Chaîne
	/// invalide -> \p fallback.
	uint32_t ParseHexColorRgba8(const std::string& hex, uint32_t fallback = 0xFFFFFFFFu);
}
