#pragma once
// Chantier 2 SP1 — avatar modulaire : composé de plusieurs parties skinnées
// partageant UN squelette. Le rendu calcule les matrices d'os une seule fois
// (squelette + pose partagés) puis dessine chaque partie active. Logique pure
// (données CPU) — pas de Vulkan ici, testable en ctest.

#include <cstddef>
#include <cstdint>
#include <vector>

namespace engine::render::skinned
{
	struct SkinnedMesh;
	struct Skeleton;

	/// Slots visuels d'équipement. SP1 démontre le pipeline (pas tous peuplés
	/// d'assets). `Body` = corps de base (toujours présent). `Count` = borne.
	enum class EquipVisualSlot : uint32_t
	{
		Body = 0,
		Head,
		Chest,
		Legs,
		Feet,
		Hands,
		Weapon,
		Offhand,
		Count
	};

	/// Avatar composé de parties skinnées partageant le squelette du corps de base.
	/// Pointeurs non-possédés : les meshes appartiennent à un cache/registre externe.
	class ModularAvatar
	{
	public:
		/// Définit le corps de base ; son squelette devient la référence partagée.
		void SetBody(const SkinnedMesh* body);

		/// Pose (`mesh` != nullptr) ou retire (`mesh` == nullptr) une partie sur un
		/// slot. Rejette (retourne false + log) si le squelette de `mesh` n'est pas
		/// compatible avec celui du corps (même nombre d'os ET mêmes noms/ordre), ou
		/// si `slot` == Body / hors borne.
		bool SetPart(EquipVisualSlot slot, const SkinnedMesh* mesh);

		/// Retire la partie d'un slot (no-op sur Body / hors borne).
		void ClearPart(EquipVisualSlot slot);

		/// Parties à dessiner : le corps d'abord (s'il existe), puis les slots
		/// occupés dans l'ordre de l'enum. Toutes partagent le squelette du corps.
		std::vector<const SkinnedMesh*> ActiveParts() const;

		/// Squelette partagé (celui du corps), ou nullptr si aucun corps.
		const Skeleton* SharedSkeleton() const;

		bool HasBody() const { return m_body != nullptr; }

	private:
		/// true si `mesh` partage le squelette du corps (même nb d'os + mêmes
		/// noms dans le même ordre). false si pas de corps ou `mesh` nul.
		bool SkeletonCompatible(const SkinnedMesh* mesh) const;

		const SkinnedMesh* m_body = nullptr;
		const SkinnedMesh* m_parts[static_cast<std::size_t>(EquipVisualSlot::Count)] = {};
	};
}
