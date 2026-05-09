#pragma once

#include "engine/core/Config.h"
#include "engine/network/CharacterPayloads.h"

#include <cstdint>
#include <string>
#include <vector>

namespace engine::client
{
	// -------------------------------------------------------------------------
	// Data types loaded from game/data/races/races.json
	// -------------------------------------------------------------------------

	/// One race entry loaded from game/data/races/races.json (M39.1).
	struct RaceDefinition
	{
		std::string              id;
		std::string              displayName;
		std::string              description;
		std::vector<std::string> racials;          ///< Short racial ability descriptions.
		std::vector<std::string> skinColorHex;     ///< Available skin colour hex strings.
		std::vector<std::string> hairColorHex;     ///< Available hair colour hex strings.
		std::vector<std::string> eyeColorHex;      ///< Available eye colour hex strings.
	};

	/// One class entry loaded from game/data/races/classes.json (M39.1).
	struct ClassDefinition
	{
		std::string              id;
		std::string              displayName;
		std::string              description;
		std::string              role;             ///< e.g. "tank_dps", "healer", "dps".
		std::vector<std::string> abilities;        ///< Short ability descriptions.
		std::vector<std::string> allowedRaceIds;   ///< Race ids that may take this class.
	};

	// -------------------------------------------------------------------------
	// Creation flow state
	// -------------------------------------------------------------------------

	/// Step of the character-creation wizard (M39.1).
	enum class CharacterCreationStep : uint8_t
	{
		RaceSelect    = 0, ///< Player chooses a race from the grid.
		ClassSelect   = 1, ///< Player chooses a class from the grid.
		Customization = 2, ///< Player adjusts appearance sliders / dropdowns.
		NameInput     = 3, ///< Player types the character name.
		Confirming    = 4, ///< Request sent, awaiting server response.
		Done          = 5, ///< Creation succeeded.
		Error         = 6, ///< Creation failed (duplicate name, etc.).
	};

	/// Full state of the character-creation screen (M39.1).
	struct CharacterCreationState
	{
		CharacterCreationStep step = CharacterCreationStep::RaceSelect;

		// ---- Selections ----
		uint32_t selectedRaceIndex  = 0; ///< Index into m_races.
		uint32_t selectedClassIndex = 0; ///< Index into the filtered class list.

		// ---- Customization (M39.1 spec: face, hair, skin, hair color, eye color) ----
		uint8_t faceType     = 0;
		uint8_t hairStyle    = 0;
		uint8_t skinColorIdx = 0;
		uint8_t hairColorIdx = 0;
		uint8_t eyeColorIdx  = 0;

		// ---- Name input ----
		std::string characterName;
		bool        nameValid        = false;
		std::string nameErrorMessage; ///< Displayed when nameValid == false.

		// ---- 3-D preview rotation (degrees, [0, 360)) ----
		float previewRotationDeg = 0.0f;

		// ---- Result ----
		std::string errorMessage; ///< Set on step == Error.
	};

	// -------------------------------------------------------------------------
	// CharacterCreationPresenter
	// -------------------------------------------------------------------------

	/// CPU-side presenter for the character-creation wizard (M39.1).
	///
	/// Manages the creation flow (race → class → customization → name → confirm),
	/// loads race/class definitions from content-relative JSON files, validates
	/// the character name client-side (length, charset, profanity), and builds
	/// the CharacterCreateRequestPayload ready to send to the server.
	///
	/// The presenter is rendering-agnostic: it exposes state that any rendering
	/// layer (debug text, future Vulkan UI) can consume.
	///
	/// Content paths (resolved via paths.content in config.json):
	///   races.json  → "races/races.json"   (overridable via "char_creation.races_path")
	///   classes.json → "races/classes.json" (overridable via "char_creation.classes_path")
	class CharacterCreationPresenter final
	{
	public:
		CharacterCreationPresenter() = default;
		~CharacterCreationPresenter();

		CharacterCreationPresenter(const CharacterCreationPresenter&) = delete;
		CharacterCreationPresenter& operator=(const CharacterCreationPresenter&) = delete;

		/// Load race/class definitions from content JSON and reset state.
		/// @param config  Engine config (paths.content + optional path overrides).
		/// @return true on success; false if definition files could not be read.
		bool Init(const engine::core::Config& config);

		/// Release resources and reset all state.
		void Shutdown();

		// ---- Step navigation ----

		/// Advance to the next creation step.
		/// - RaceSelect   → ClassSelect   (if a race is selected)
		/// - ClassSelect  → Customization (if a class is selected and compatible)
		/// - Customization → NameInput
		/// - NameInput    → Confirming   (only if the name is valid)
		/// Returns false when the current step cannot advance (validation failure).
		bool Next();

		/// Go back to the previous step (NameInput → Customization → ClassSelect → RaceSelect).
		void Back();

		// ---- Race selection ----

		/// Select the race at \p index.  Resets class/customization to defaults.
		bool SelectRace(uint32_t index);

		/// All loaded races (for grid rendering).
		const std::vector<RaceDefinition>& GetRaces() const { return m_races; }

		/// Currently selected race, or nullptr.
		const RaceDefinition* GetSelectedRace() const;

		// ---- Class selection ----

		/// Select the class at \p index within GetFilteredClasses().
		bool SelectClass(uint32_t index);

		/// Classes compatible with the currently selected race.
		std::vector<const ClassDefinition*> GetFilteredClasses() const;

		/// Currently selected class, or nullptr.
		const ClassDefinition* GetSelectedClass() const;

		// ---- Customization ----

		void SetFaceType(uint8_t v);
		void SetHairStyle(uint8_t v);
		void SetSkinColorIdx(uint8_t v);
		void SetHairColorIdx(uint8_t v);
		void SetEyeColorIdx(uint8_t v);

		/// Number of face types available for the current race (currently 4).
		uint8_t GetFaceTypeCount()     const { return 4u; }
		/// Number of hair styles available (currently 8).
		uint8_t GetHairStyleCount()    const { return 8u; }
		/// Number of skin colours in the selected race's palette.
		uint8_t GetSkinColorCount()    const;
		/// Number of hair colours in the selected race's palette.
		uint8_t GetHairColorCount()    const;
		/// Number of eye colours in the selected race's palette.
		uint8_t GetEyeColorCount()     const;

		// ---- 3-D preview rotation ----

		/// Apply a horizontal mouse-drag delta to the preview rotation.
		/// \p deltaPixels  Pixels moved horizontally since last frame.
		/// \p sensitivity  Degrees per pixel (default 0.5°/px).
		void ApplyPreviewDrag(float deltaPixels, float sensitivity = 0.5f);

		// ---- Name input + validation ----

		/// Set the current name draft and validate it immediately.
		/// Validation rules (M39.1 spec): 3-12 chars, alphanumeric + underscore,
		/// not in the built-in profanity blacklist.
		/// Sets state.nameValid and state.nameErrorMessage.
		void SetCharacterName(const std::string& name);

		// ---- Payload builder ----

		/// Build the request payload ready to send to the server.
		/// @return The payload; empty if the state is not in Confirming step.
		engine::network::CharacterCreateRequestPayload BuildRequestPayload() const;

		/// Mark the step as Done (call after server returns success).
		void MarkSuccess();

		/// Mark the step as Error (call after server returns failure).
		/// \p reason  Human-readable error from the server.
		void MarkError(const std::string& reason);

		// ---- State / debug ----

		const CharacterCreationState& GetState() const { return m_state; }

		/// Debug text dump of the full creation state (for overlay rendering).
		std::string BuildDebugText() const;

		bool IsInitialized() const { return m_initialized; }

	private:
		/// Load races from the content-relative JSON path.
		bool LoadRaces(const engine::core::Config& config, const std::string& relPath);

		/// Load classes from the content-relative JSON path.
		bool LoadClasses(const engine::core::Config& config, const std::string& relPath);

		/// Validate \p name according to ticket spec (3-12 chars, alphanumeric+_, profanity).
		/// Fills \p outError with a human-readable message when invalid.
		bool ValidateName(const std::string& name, std::string& outError) const;

		/// Static profanity blacklist (English + common variants).
		static bool IsProfane(const std::string& lower);

		std::vector<RaceDefinition>  m_races;
		std::vector<ClassDefinition> m_classes;
		uint32_t m_selectedClassFiltered = 0; ///< Index within filtered class list.
		CharacterCreationState m_state{};
		bool m_initialized = false;
	};

} // namespace engine::client
