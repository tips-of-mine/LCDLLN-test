#pragma once

#include "src/world_editor/volumes/MeshInsertInstance.h"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace engine::editor::world::volumes
{
	/// Magic du format binaire `instances/mesh_inserts.bin` ("LCMI" little-endian).
	constexpr uint32_t kMeshInsertsBinMagic   = 0x494D434Cu;  // 'L' 'C' 'M' 'I'
	/// Version courante (M100.40 v1).
	constexpr uint32_t kMeshInsertsBinVersion = 1u;

	/// Sérialise la liste d'instances au format `mesh_inserts.bin` (M100.40 v1).
	/// Header 16 octets : `[magic(4)][version(4)][instanceCount(4)][reserved(4)]`.
	/// Chaque instance encodée séquentiellement :
	///   - `guid` (uint64)
	///   - `gltfRelativePath` (u16 length + UTF-8)
	///   - `worldPosition` (3 float32)
	///   - `eulerRotationDeg` (3 float32)
	///   - `uniformScale` (float32)
	///   - `insertCategory` (u16 length + UTF-8)
	///   - `displayName` (u16 length + UTF-8)
	///   - `flags` (u8 bitfield : bit0=hasInteriorVolume, bit1=castsShadow,
	///     bit2=receivesAudioReverb, bit3=allowsWaterIngress)
	///   - `lightProbeIntensity` (float32)
	///
	/// \param instances Liste à sérialiser (peut être vide).
	/// \param outBytes  Buffer rempli (resize+write).
	/// \param outError  Renseigné en cas d'échec.
	/// \return true sur succès.
	bool SaveMeshInsertsBin(const std::vector<MeshInsertInstance>& instances,
		std::vector<uint8_t>& outBytes, std::string& outError);

	/// Désérialise un `mesh_inserts.bin`. Valide magic + version. Reset
	/// `outInstances`. Si fichier vide / minimal (instanceCount = 0),
	/// retourne true avec liste vide.
	///
	/// \param bytes         Fichier complet.
	/// \param outInstances  Liste parsée.
	/// \param outError      Renseigné en cas d'échec.
	bool LoadMeshInsertsBin(std::span<const uint8_t> bytes,
		std::vector<MeshInsertInstance>& outInstances, std::string& outError);
}
