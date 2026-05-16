#pragma once

#include "src/world_editor/zone_presets/CustomizationApplier.h"
#include "src/world_editor/zone_presets/OperationDispatcher.h"
#include "src/world_editor/zone_presets/ZonePreset.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>

namespace engine::editor::world
{
	class CommandStack;
	class WaterDocument;
}

namespace engine::editor::world::zone_presets
{
	/// État d'avancement transmis au progressCallback de l'exécuteur
	/// (M100.46 §D.1).
	struct ExecutionProgress
	{
		uint32_t    currentStep      = 0u;
		uint32_t    totalSteps       = 0u;
		std::string currentStepLabel; ///< « Étape 3/8 : place_cave »
		float       fractionComplete = 0.0f;
		bool        isCancelled      = false;
	};

	/// Résumé d'exécution renvoyé par `Execute`.
	struct ExecutionSummary
	{
		uint32_t totalSteps         = 0u;
		uint32_t commandsPushed     = 0u; ///< nombre d'ICommand vraiment exécutées
		uint32_t unsupportedSkipped = 0u; ///< type connu mais pas câblé (incrément 2b)
		uint32_t failed             = 0u; ///< dispatch failed (catalog introuvable, etc.)
		bool     wasCancelled       = false;
	};

	/// Moteur d'exécution d'un zone preset (M100.46 incrément 2b).
	///
	/// Workflow (cf. spec §D) :
	///   1. Vide la zone (`ResetEditedZoneDocuments`) — destructif en MVP.
	///   2. Boucle sur `preset.operations` :
	///      a. dispatch → `ICommand` (ou skip si non supporté/failed).
	///      b. push sur le `CommandStack` (= Execute + permet Ctrl+Z par
	///         étape).
	///      c. progressCallback invoqué synchrone après chaque étape.
	///      d. arrêt si `progressCallback` retourne false OU si
	///         `RequestCancel()` a été appelé entretemps.
	///
	/// Une opération non supportée par le dispatcher (11 des 14 types en
	/// MVP incrément 2b) est ignorée silencieusement avec un log info,
	/// conformément au comportement du spec pour la section `decoration`
	/// avec types inconnus. Le compteur `unsupportedSkipped` du résumé
	/// permet à l'UI d'informer l'utilisateur.
	class ZonePresetExecutor
	{
	public:
		using ProgressCallback = std::function<bool(const ExecutionProgress&)>;

		ZonePresetExecutor() = default;

		/// Exécute `preset`. `commandStack` reçoit les commandes (une par
		/// opération supportée). `progressCallback` peut être nul. Renvoie
		/// un résumé d'exécution.
		ExecutionSummary Execute(const ZonePreset& preset,
			const CustomizationParams& custom,
			engine::editor::world::CommandStack& commandStack,
			const DispatchContext& dispatchCtx,
			engine::editor::world::WaterDocument& water,
			const ProgressCallback& progressCallback);

		/// Demande l'arrêt de l'exécution en cours. Appelable depuis le
		/// thread UI. L'arrêt prend effet **après** la commande courante
		/// (pas en plein milieu d'une simulation). Les commandes déjà
		/// poussées restent sur le stack — annulables individuellement.
		void RequestCancel() { m_cancelRequested.store(true, std::memory_order_release); }

		bool IsCancelRequested() const noexcept
		{
			return m_cancelRequested.load(std::memory_order_acquire);
		}

	private:
		std::atomic<bool> m_cancelRequested{ false };
	};
}
