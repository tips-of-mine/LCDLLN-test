// M100.34 — Implémentation LayersDocument (logique pure).

#include "src/world_editor/LayersDocument.h"

namespace engine::editor::world
{
	LayersDocument::LayersDocument()
	{
		// Calque 0 = "Default", les autres "Layer N" masquables/verrouillables.
		m_layers[0].name = "Default";
		for (uint8_t i = 1; i < kLayerCount; ++i)
			m_layers[i].name = "Layer " + std::to_string(static_cast<int>(i));
	}

	Layer& LayersDocument::LayerAt(uint8_t index)
	{
		if (index >= kLayerCount) index = 0;
		return m_layers[index];
	}

	const Layer& LayersDocument::LayerAt(uint8_t index) const
	{
		if (index >= kLayerCount) index = 0;
		return m_layers[index];
	}

	void LayersDocument::SetLayerName(uint8_t index, const std::string& name)
	{
		LayerAt(index).name = name;
	}

	void LayersDocument::SetVisible(uint8_t index, bool visible)
	{
		LayerAt(index).visible = visible;
	}

	void LayersDocument::SetLocked(uint8_t index, bool locked)
	{
		LayerAt(index).locked = locked;
	}

	void LayersDocument::AssignEntity(uint64_t entityKey, uint8_t layerIndex)
	{
		if (layerIndex >= kLayerCount) layerIndex = 0;
		m_assignment[entityKey] = layerIndex;
	}

	uint8_t LayersDocument::GetEntityLayer(uint64_t entityKey) const
	{
		auto it = m_assignment.find(entityKey);
		return it == m_assignment.end() ? 0u : it->second;
	}

	bool LayersDocument::IsEntityVisible(uint64_t entityKey) const
	{
		return LayerAt(GetEntityLayer(entityKey)).visible;
	}

	bool LayersDocument::IsEntityLocked(uint64_t entityKey) const
	{
		return LayerAt(GetEntityLayer(entityKey)).locked;
	}
}
