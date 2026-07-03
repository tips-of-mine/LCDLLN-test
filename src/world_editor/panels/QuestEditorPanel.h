#pragma once

#include "src/world_editor/core/IPanel.h"
#include "src/world_editor/quests/QuestEditIo.h"

#include <string>
#include <vector>

namespace engine::editor::world::panels
{
	/// Panneau d'authoring des quêtes (SP4) : charge, édite, valide et
	/// enregistre l'ensemble des quêtes du contenu actif via `QuestEditIo`.
	/// Miroir structurel de `BuildingEditorPanel` (formulaire ImGui + statut
	/// texte + bouton Enregistrer), mais sans aperçu 3D — une quête est de la
	/// donnée pure (id/giver/turnIn/prereqs/steps/rewards/textes).
	///
	/// Dépendance injectée (non possédée) via `SetIo`, posée par le shell à
	/// l'Init. Le chargement disque (`QuestEditIo::Load`) est différé au
	/// premier `Render()` (pas de coût tant que le panneau reste masqué).
	class QuestEditorPanel final : public IPanel
	{
	public:
		const char* GetName() const override { return "Quest Editor"; }
		void Render() override;
		bool IsVisible() const override { return m_visible; }
		void SetVisible(bool visible) override { m_visible = visible; }

		/// Injecte la racine de contenu filesystem (ex. "game/data") utilisée
		/// par `QuestEditIo::Load`/`Save`. À appeler avant le premier `Render`.
		void SetContentRoot(std::string root) { m_contentRoot = std::move(root); }

		/// Injecte l'E/S de quêtes (non possédée, doit survivre au panneau —
		/// portée par le shell). À appeler avant le premier `Render`.
		void SetIo(engine::editor::world::quests::QuestEditIo* io) { m_io = io; }

	private:
		/// Charge (une seule fois) l'ensemble des quêtes depuis
		/// `m_contentRoot` via `m_io->Load`. Effet de bord : lecture disque,
		/// remplit `m_quests`, positionne `m_status` en cas d'échec. Marque
		/// `m_loaded = true` dans tous les cas (échec inclus) pour ne
		/// tenter le chargement qu'une fois par session panneau ; l'utilisateur
		/// peut forcer une relecture via le bouton « Recharger ».
		/// Thread : main thread (appelé depuis Render, phase ImGui).
		void EnsureLoaded();

		/// Réinitialise les buffers d'édition (id/giver/turnIn/prereqs/steps/
		/// rewards/textes) à partir de la quête sélectionnée `m_quests[m_selected]`.
		/// Sans effet si `m_selected` est hors plage. Effet de bord : aucun
		/// (mémoire locale au panneau uniquement).
		void LoadBuffersFromSelected();

		/// Construit une `EditedQuest` à partir des buffers d'édition courants
		/// (id/giver/turnIn/prereqs/steps/rewards/textes). Pure : ne touche
		/// pas `m_quests`, l'appelant décide de l'insertion/remplacement.
		engine::editor::world::quests::EditedQuest BuildQuestFromBuffers() const;

		/// Réinitialise les buffers d'édition à une quête vierge (utilisé par
		/// le bouton « Nouvelle quête »). Effet de bord : vide `m_status`.
		void ResetBuffersToNew();

		/// Rend la section « Charger » : combo listant les ids de `m_quests`
		/// + bouton « Nouvelle quête ». Sélectionner une entrée appelle
		/// `LoadBuffersFromSelected`. Effet de bord : état ImGui uniquement.
		/// Thread : main thread (ImGui).
		void RenderLoadSection();

		/// Rend les champs scalaires du formulaire (id/giver/turnIn). Effet
		/// de bord : état ImGui uniquement (écrit dans les buffers membres).
		void RenderIdentityFields();

		/// Rend la multi-sélection des prérequis (`m_prereqBuffer`) : une
		/// case à cocher par id de quête connu (hors la quête en cours
		/// d'édition, pour éviter l'auto-référence triviale ; les cycles plus
		/// longs restent détectés par `QuestEditIo::Validate`). Effet de bord :
		/// état ImGui + `m_prereqBuffer`.
		void RenderPrereqSection();

		/// Rend la multi-sélection des quêtes mutuellement exclusives (EXT-1,
		/// `m_excludesBuffer`) : une case à cocher par id de quête connu, HORS
		/// la quête en cours d'édition (`q.id != m_idBuf`) pour interdire
		/// l'auto-exclusion côté UI (règle miroir de `RenderPrereqSection`, mais
		/// sans cycle-check : l'exclusion mutuelle est permise). Cocher/décocher
		/// ajoute/retire l'id dans `m_excludesBuffer`.
		/// Effet de bord : état ImGui + `m_excludesBuffer` (modifié en place).
		/// Thread : main thread (ImGui).
		void RenderExcludesSection();

		/// Rend la liste éditable des étapes (`m_stepsBuffer`) : combo type
		/// (kill/collect/talk/enter), champ target, champ requiredCount,
		/// boutons Suppr par ligne + bouton « Ajouter une étape » en fin de
		/// liste. Effet de bord : état ImGui + `m_stepsBuffer`
		/// (+ `m_stepLabelsBuffer`, gardé de même taille que `m_stepsBuffer`).
		void RenderStepsSection();

		/// Rend les champs de récompense : xp, or, liste d'items
		/// (itemId + quantité, +/× par ligne). Effet de bord : état ImGui +
		/// `m_rewardXpBuffer`/`m_rewardGoldBuffer`/`m_rewardItemsBuffer`.
		void RenderRewardsSection();

		/// Rend la section EXT-2 « re-réalisation » : un `Combo` de mode
		/// (Aucun/Répétable/Quotidienne/Hebdo/Cooldown → `m_repeatModeBuffer`),
		/// un `DragInt` « Cooldown (h) » affiché UNIQUEMENT en mode Cooldown
		/// (→ `m_cooldownHoursBuffer`), un `Checkbox` « Auto-complete »
		/// (→ `m_autoCompleteBuffer`) et (EXT-3) un `Checkbox` « Partage en groupe »
		/// (→ `m_partySharedBuffer`). Effet de bord : état ImGui + ces 4
		/// buffers membres. Thread : main thread (phase ImGui).
		void RenderRepeatSection();

		/// Rend les champs de texte lisible (titre/description/libellés
		/// d'étape, un par étape de `m_stepsBuffer`, taille synchronisée par
		/// `RenderStepsSection`). Effet de bord : état ImGui uniquement.
		void RenderTextsSection();

		/// Rend le bouton « Enregistrer » : construit la quête courante via
		/// `BuildQuestFromBuffers`, l'insère/remplace dans une copie de
		/// `m_quests`, appelle `m_io->Validate` puis, si valide,
		/// `m_io->Save(m_contentRoot, ...)`. Met à jour `m_status` (succès ou
		/// liste d'erreurs). Effet de bord : ÉCRITURE DISQUE (3 fichiers sous
		/// `<contentRoot>/quests/`) en cas de validation réussie ; recharge
		/// `m_quests` depuis le résultat sauvegardé pour rester cohérent avec
		/// le disque. Thread : main thread (ImGui + I/O synchrone).
		void RenderSaveSection();

		bool m_visible = true;
		bool m_loaded = false; // Load() déjà tenté cette session panneau

		std::string m_contentRoot = "game/data";
		engine::editor::world::quests::QuestEditIo* m_io = nullptr;

		std::vector<engine::editor::world::quests::EditedQuest> m_quests;
		int m_selected = -1; // index dans m_quests (-1 = nouvelle quête / aucune)

		// --- Buffers d'édition (formulaire courant, pas encore enregistré) ---
		char m_idBuf[64] = "";
		char m_giverBuf[64] = "";
		char m_turnInBuf[64] = "";
		std::vector<std::string> m_prereqBuffer;
		std::vector<std::string> m_excludesBuffer; // EXT-1 : quêtes mutuellement exclusives

		std::vector<engine::editor::world::quests::EditedStep> m_stepsBuffer;
		uint32_t m_rewardXpBuffer = 0;
		uint32_t m_rewardGoldBuffer = 0;
		std::vector<engine::editor::world::quests::EditedRewardItem> m_rewardItemsBuffer;

		// EXT-2 : re-réalisation. `m_repeatModeBuffer` indexe le combo (0=None..4=Cooldown,
		// même ordre que QuestRepeatMode) ; `m_cooldownHoursBuffer` pertinent si
		// mode==Cooldown ; `m_autoCompleteBuffer` = fin automatique sans retour PNJ.
		engine::editor::world::quests::QuestRepeatMode m_repeatModeBuffer =
			engine::editor::world::quests::QuestRepeatMode::None;
		uint32_t m_cooldownHoursBuffer = 0;
		bool m_autoCompleteBuffer = false;
		// EXT-3 : partage groupe. Si vrai, le crédit d'étape est propagé aux
		// coéquipiers à portée (fan-out shard) ; sérialisé en `"partyShared"`.
		bool m_partySharedBuffer = false;

		char m_titleBuf[128] = "";
		char m_descriptionBuf[512] = "";
		std::vector<std::string> m_stepLabelsBuffer; // même taille que m_stepsBuffer

		std::string m_status; // dernier message (succès/erreurs), affiché en bas
	};
}
