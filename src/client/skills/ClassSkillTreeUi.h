#pragma once
// SP-D — Arbre de compétences par-classe — presenter client.
// Lit ClassSkillCatalog + les skills connus (UIModel.knownSkillIds) ; ne contient
// aucun appel ImGui (cf. ClassSkillTreeImGuiRenderer). Émet SendChooseClassSkill
// via un callback fourni par Engine.

#include "src/client/gameplay/ClassSkillCatalog.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace engine::client
{
	/// Statut d'une cellule de l'arbre (un skill à un tier donné).
	enum class SkillCellStatus
	{
		Chosen,    ///< Skill déjà appris par le joueur.
		Available, ///< Tier ≤ playerLevel et aucun skill déjà choisi à ce niveau.
		Locked,    ///< Tier > playerLevel ou niveau déjà pourvu.
	};

	/// Cellule d'une branche de l'arbre : skill + statut calculé.
	struct SkillTreeCell
	{
		ClassSkillDisplay skill;
		SkillCellStatus   status = SkillCellStatus::Locked;
	};

	/// Une branche de l'arbre (single / aoe / def), triée par tier 1..60.
	struct SkillTreeBranch
	{
		std::string               id;    ///< "single", "aoe" ou "def".
		std::vector<SkillTreeCell> tiers; ///< Entrée i = tier i+1 (index par ordre du catalogue).
	};

	/// État snapshot exposé au renderer (mis à jour par Sync).
	struct ClassSkillTreeState
	{
		std::string                classId;
		uint32_t                   playerLevel = 1;
		std::vector<SkillTreeBranch> branches; ///< 3 branches : single, aoe, def.
	};

	/// Presenter de l'arbre de compétences. Init() avant usage. Thread : main.
	class ClassSkillTreeUiPresenter final
	{
	public:
		ClassSkillTreeUiPresenter() = default;
		ClassSkillTreeUiPresenter(const ClassSkillTreeUiPresenter&) = delete;
		ClassSkillTreeUiPresenter& operator=(const ClassSkillTreeUiPresenter&) = delete;

		/// Initialise le presenter avec le catalogue des skills par-classe.
		/// \param catalog Pointeur sur le catalogue chargé (durée de vie > presenter).
		/// \return true si OK, false si catalog est nullptr.
		bool Init(const ClassSkillCatalog* catalog);

		/// Libère l'état ; presenter réinitialisable via Init().
		void Shutdown();

		/// \return true si Init() a réussi et Shutdown() n'a pas encore été appelé.
		bool IsInitialized() const { return m_initialized; }

		/// Callback de choix : (level, skillId) → true si l'émission a réussi.
		/// Câblé par Engine sur GameplayUdpClient::SendChooseClassSkill.
		using ChooseCallback = std::function<bool(uint32_t level, const std::string& skillId)>;

		/// Enregistre le callback de choix (appelé par ChooseSkill).
		void SetChooseCallback(ChooseCallback cb) { m_choose = std::move(cb); }

		/// Reconstruit les branches depuis le catalogue pour la classe \p classId.
		/// Calcule le statut de chaque cellule :
		///   - Chosen    : skillId ∈ knownSkillIds.
		///   - Available : tier ≤ playerLevel ET aucun skill connu n'a level == tier.
		///   - Locked    : tier > playerLevel OU niveau déjà pourvu.
		/// À appeler dès réception d'un ClassProgressionUpdate ou changement de niveau.
		/// \param classId       Identifiant de la classe du joueur.
		/// \param knownSkillIds Liste des skillId déjà appris (UIModel).
		/// \param playerLevel   Niveau actuel du joueur (1-60).
		void Sync(const std::string& classId,
		          const std::vector<std::string>& knownSkillIds,
		          uint32_t playerLevel);

		/// Demande au serveur de choisir \p skillId pour le niveau \p level.
		/// Appelle le callback ChooseCallback ; la réconciliation se fera via
		/// ClassProgressionUpdate → Sync.
		/// \param level   Niveau de palier ciblé (1-60).
		/// \param skillId Identifiant du skill à apprendre.
		void ChooseSkill(uint32_t level, const std::string& skillId);

		/// Retourne l'état courant (branches + classId + playerLevel).
		const ClassSkillTreeState& GetState() const { return m_state; }

	private:
		/// Retourne true si un skill connu possède level == \p tier.
		/// \param knownSkillIds Liste des skillId connus.
		/// \param allSkills     Tous les skills de la classe (pour le lookup level).
		/// \param tier          Tier (= niveau) à tester.
		bool HasKnownSkillAtLevel(const std::vector<std::string>& knownSkillIds,
		                          const std::vector<ClassSkillDisplay>& allSkills,
		                          uint32_t tier) const;

		bool                 m_initialized = false;
		const ClassSkillCatalog* m_catalog = nullptr;
		ClassSkillTreeState  m_state{};
		ChooseCallback       m_choose;
	};
}
