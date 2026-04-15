#pragma once

#include "engine/core/Config.h"
#include "engine/network/CharacterPayloads.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace engine::client
{
	/// Maximum character name length (inclusive), per ticket spec.
	inline constexpr uint32_t kCharNameMinLength = 3u;
	/// Maximum character name length (inclusive), per ticket spec.
	inline constexpr uint32_t kCharNameMaxLength = 12u;

	/// Character creation step / screen (M39.1).
	enum class CharCreationStep : uint8_t
	{
		RaceSelection   = 0,
		ClassSelection  = 1,
		Customization   = 2,
		Confirmation    = 3
	};

	/// One race definition loaded from `character_creation/races.json` (M39.1).
	struct CharRaceDefinition
	{
		uint32_t    id          = 0;
		std::string key;          ///< Stable key, e.g. "human".
		std::string displayName;
		std::string description;
		std::string iconPath;     ///< Content-relative path for the race icon texture.
		std::string racialBonus;  ///< Short one-line racial trait summary.
	};

	/// One class definition loaded from `character_creation/classes.json` (M39.1).
	struct CharClassDefinition
	{
		uint32_t    id          = 0;
		std::string key;          ///< Stable key, e.g. "warrior".
		std::string displayName;
		std::string description;
		std::string iconPath;     ///< Content-relative path for the class icon texture.
		std::string roleDescription; ///< Role label, e.g. "Tank / DPS".
	};

	/// Customization slider/option values (M39.1).
	struct CharCustomizationParams
	{
		uint8_t faceType       = 0; ///< Face preset index [0, 3].
		uint8_t hairStyle      = 0; ///< Hair style index [0, 7].
		uint8_t skinColorIndex = 0; ///< Skin colour palette index [0, 5].
		uint8_t hairColorIndex = 0; ///< Hair colour palette index [0, 7].
		uint8_t eyeColorIndex  = 0; ///< Eye colour palette index [0, 5].
	};

	/// Fully resolved character creation screen state (M39.1).
	struct CharCreationPanelState
	{
		CharCreationStep step = CharCreationStep::RaceSelection;

		/// Available races/classes (populated after Init).
		std::vector<CharRaceDefinition>  races;
		std::vector<CharClassDefinition> classes;

		/// Currently selected indices (0 = first, UINT32_MAX = none).
		uint32_t selectedRaceId  = UINT32_MAX;
		uint32_t selectedClassId = UINT32_MAX;

		CharCustomizationParams customization{};

		std::string characterName;
		std::string nameValidationError;
		bool        nameIsValid = false;

		/// Character preview rotation angle in degrees [0, 360).
		float previewRotationDeg = 0.0f;

		/// Human-readable debug dump of the current creation state.
		std::string debugText;
		bool        layoutValid = false;
	};

	/// Character creation UI presenter: drives the race/class/customization
	/// screens and produces a CharacterCreateRequestPayload for the network layer
	/// (M39.1).
	class CharacterCreationUiPresenter final
	{
	public:
		/// Construct an uninitialized presenter.
		CharacterCreationUiPresenter() = default;

		/// Release presenter resources.
		~CharacterCreationUiPresenter();

		/// Initialise the presenter and load race/class definitions.
		/// \p config must expose a valid `paths.content` root.
		/// \p racesRelativePath   Content-relative path to `races.json`.
		/// \p classesRelativePath Content-relative path to `classes.json`.
		bool Init(const engine::core::Config& config,
		          std::string_view racesRelativePath   = "character_creation/races.json",
		          std::string_view classesRelativePath = "character_creation/classes.json");

		/// Shutdown and release all cached state.
		void Shutdown();

		/// Update the viewport-dependent layout.
		void SetViewportSize(uint32_t width, uint32_t height);

		/// Advance to a specific creation step.
		void SetStep(CharCreationStep step);

		/// Select a race by id.  Returns false when id is unknown.
		bool SelectRace(uint32_t raceId);

		/// Select a class by id.  Returns false when id is unknown.
		bool SelectClass(uint32_t classId);

		/// Update all five customization parameters at once.
		void SetCustomization(const CharCustomizationParams& params);

		/// Set a single customization option.
		void SetFaceType(uint8_t v);
		void SetHairStyle(uint8_t v);
		void SetSkinColor(uint8_t v);
		void SetHairColor(uint8_t v);
		void SetEyeColor(uint8_t v);

		/// Set the character name and run local validation.
		/// Returns false when the name fails local checks; check
		/// \ref GetState().nameValidationError for the reason.
		bool SetName(std::string_view name);

		/// Apply a horizontal mouse-drag delta to rotate the character preview.
		/// \p deltaDeg  Rotation increment in degrees (positive = clockwise).
		void ApplyPreviewRotation(float deltaDeg);

		/// Build the network payload ready to send (M39.1 step 8).
		/// Returns an empty optional when the creation state is incomplete.
		std::optional<engine::network::CharacterCreateRequestPayload> BuildCreateRequest() const;

		/// Immutable access to the current panel state.
		const CharCreationPanelState& GetState() const { return m_state; }

		bool IsInitialized() const { return m_initialized; }

	private:
		/// Load race definitions from the configured content-relative JSON file.
		bool LoadRaces(const engine::core::Config& config, std::string_view relativePath);

		/// Load class definitions from the configured content-relative JSON file.
		bool LoadClasses(const engine::core::Config& config, std::string_view relativePath);

		/// Run local name validation: length + allowed chars + basic profanity filter.
		/// Returns empty string on success, or an error message on failure.
		static std::string ValidateName(std::string_view name);

		/// Rebuild m_state.debugText from current selection state.
		void RebuildDebugText();

		CharCreationPanelState m_state{};
		uint32_t m_viewportWidth  = 0;
		uint32_t m_viewportHeight = 0;
		bool     m_initialized    = false;
	};

} // namespace engine::client
