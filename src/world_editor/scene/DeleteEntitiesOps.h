#pragma once

#include <algorithm>
#include <cstdint>
#include <utility>
#include <vector>

namespace engine::editor::scene
{
	/// Supprime de `vec` les éléments aux `indices` donnés (ordre quelconque,
	/// doublons tolérés). Retire en ordre décroissant pour ne pas invalider les
	/// index restants. Retourne les `(index, copie)` retirés, **triés par index
	/// croissant** (prêt pour `RestoreByIndexAscending`). Index hors borne ignorés.
	template <class T>
	std::vector<std::pair<uint32_t, T>> RemoveByIndexDescending(
		std::vector<T>& vec, std::vector<uint32_t> indices)
	{
		std::sort(indices.begin(), indices.end());
		indices.erase(std::unique(indices.begin(), indices.end()), indices.end());

		std::vector<std::pair<uint32_t, T>> removed;
		removed.reserve(indices.size());
		// Croissant pour le snapshot…
		for (uint32_t idx : indices)
			if (idx < vec.size())
				removed.emplace_back(idx, vec[idx]);
		// …mais on supprime en décroissant.
		for (auto it = indices.rbegin(); it != indices.rend(); ++it)
			if (*it < vec.size())
				vec.erase(vec.begin() + static_cast<std::ptrdiff_t>(*it));
		return removed;
	}

	/// Réinsère les `(index, copie)` (supposés triés croissants) à leur position
	/// d'origine. Reconstruit l'état d'avant `RemoveByIndexDescending` exactement.
	template <class T>
	void RestoreByIndexAscending(std::vector<T>& vec,
		const std::vector<std::pair<uint32_t, T>>& removed)
	{
		for (const auto& [idx, item] : removed)
		{
			const size_t clamped = idx <= vec.size() ? idx : vec.size();
			vec.insert(vec.begin() + static_cast<std::ptrdiff_t>(clamped), item);
		}
	}
}
