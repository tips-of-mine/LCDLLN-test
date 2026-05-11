#pragma once
// WorldObject : Object + position 3D dans une map. Toute entite ayant une
// presence physique (Player, Creature, GameObject, Corpse) en herite.
//
// Position : (x, y, z, orientation) en metres + radians, world-space.
// MapId : identifie la carte logique. ZoneId : sous-zone (capitale, etc.).
//
// AddToWorld / RemoveFromWorld : flag binaire pour l'instant. L'integration
// avec la grille spatiale (Wave 18 GridVisitor) viendra dans la PR suivante.

#include "src/shardd/entities/Object.h"
#include "src/shardd/entities/UpdateField.h"
#include "src/shardd/entities/UpdateFieldIndices.h"

#include <cstdint>
#include <cstddef>

namespace engine::server::entities
{
	/// WorldObject : herite Object, ajoute position 3D + appartenance map/zone.
	class WorldObject : public Object
	{
	public:
		/// \param guid identifiant immuable
		/// \param fieldCount nombre total de champs (sous-classe peut etendre)
		WorldObject(ObjectGuid guid, size_t fieldCount = kWorldObjectFieldCount)
			: Object(guid, fieldCount)
			, m_mapId(kWorldObjectFieldMapId, &Mask())
			, m_zoneId(kWorldObjectFieldZoneId, &Mask())
			, m_posX(kWorldObjectFieldPosX, &Mask())
			, m_posY(kWorldObjectFieldPosY, &Mask())
			, m_posZ(kWorldObjectFieldPosZ, &Mask())
			, m_orientation(kWorldObjectFieldOrientation, &Mask())
		{}

		~WorldObject() override = default;

		/// Set position 3D + orientation. Marque les 4 champs dirty si change.
		/// \param x metres world-space
		/// \param y metres world-space
		/// \param z metres world-space
		/// \param orientation radians, yaw autour de l'axe vertical
		void SetPosition(float x, float y, float z, float orientation)
		{
			m_posX.Set(x);
			m_posY.Set(y);
			m_posZ.Set(z);
			m_orientation.Set(orientation);
		}

		float GetPosX() const noexcept { return m_posX.Get(); }
		float GetPosY() const noexcept { return m_posY.Get(); }
		float GetPosZ() const noexcept { return m_posZ.Get(); }
		float GetOrientation() const noexcept { return m_orientation.Get(); }

		void SetMapId(uint32_t id) { m_mapId.Set(id); }
		uint32_t GetMapId() const noexcept { return m_mapId.Get(); }

		void SetZoneId(uint32_t id) { m_zoneId.Set(id); }
		uint32_t GetZoneId() const noexcept { return m_zoneId.Get(); }

		/// Marque l'objet comme present dans le monde. Stub : l'integration grid
		/// effective viendra dans Wave 18 (GridVisitor + GridNotifier sur
		/// SpatialPartition existant).
		void AddToWorld() noexcept { m_inWorld = true; }

		/// Retire l'objet du monde (inverse de AddToWorld).
		void RemoveFromWorld() noexcept { m_inWorld = false; }

		/// True si l'objet est actuellement publie dans la grille.
		bool IsInWorld() const noexcept { return m_inWorld; }

	private:
		UpdateField<uint32_t> m_mapId;
		UpdateField<uint32_t> m_zoneId;
		UpdateField<float>    m_posX;
		UpdateField<float>    m_posY;
		UpdateField<float>    m_posZ;
		UpdateField<float>    m_orientation;
		bool                  m_inWorld = false;
	};
}
