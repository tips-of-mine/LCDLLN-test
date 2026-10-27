#pragma once
// ETAPE3.2 (chantiers 4.2, 4.5, 4.6) — snapshots en delta sur base acquittée.
//
// Avant : chaque snapshot (10 Hz) renvoyait l'état COMPLET de toutes les entités de
// l'ensemble d'intérêt, en flottants 32 bits, avec nom et genre des joueurs à chaque
// fois. Un morceau perdu faisait perdre tout le snapshot.
//
// Maintenant :
//   - chaque snapshot porte un numéro (snapshotSeq) et le numéro de la base
//     (baselineSeq) par rapport à laquelle il est écrit ; 0 = snapshot complet ;
//   - le client acquitte le dernier snapshot qu'il a reconstruit en entier (champ
//     ackSnapshotSeq de l'Input, 20 Hz) ; le shard écrit le suivant par rapport à ce
//     snapshot acquitté. Rien n'est renvoyé en mode fiable : un snapshot perdu est
//     simplement remplacé par le suivant (écrit lui aussi par rapport à une base que
//     le client possède) ;
//   - une entité inchangée depuis la base n'est pas envoyée ; une entité modifiée
//     n'envoie que les champs qui ont changé (masque) ; une entité sortie de l'intérêt
//     est listée dans les retraits ;
//   - les valeurs sont quantifiées (4.6) : positions au centimètre (petit déplacement
//     = 1 ou 2 octets par axe en delta), orientation sur 16 bits, vitesses en cm/s ;
//     les identités (nom, genre, archétype) ne partent qu'à l'apparition ou au
//     changement.
//
// Le delta est calculé sur les valeurs QUANTIFIÉES, identiques des deux côtés : pas
// de dérive d'arrondi entre la base du shard et celle du client.
//
// Aucune dépendance au réseau ni à la boucle de jeu (testable seul, cf.
// snapshot_delta_tests).

#include "src/shared/network/ReplicationTypes.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace engine::network::snapshot
{
	/// État d'une entité dans le domaine du fil (quantifié).
	struct WireEntity
	{
		engine::server::EntityId entityId = 0;
		int32_t positionCmX = 0;      ///< position globale, centimètres.
		int32_t positionCmY = 0;
		int32_t positionCmZ = 0;
		uint16_t yaw = 0;             ///< orientation, 65536 pas par tour.
		int32_t velocityCmX = 0;      ///< vitesse, cm/s.
		int32_t velocityCmY = 0;
		int32_t velocityCmZ = 0;
		uint32_t currentHealth = 0;
		uint32_t maxHealth = 0;
		uint32_t stateFlags = 0;
		uint8_t animationState = 0;
		uint32_t playerClientId = 0;
		uint32_t archetypeId = 0;
		std::string characterName;    ///< 64 octets au plus.
		std::string gender;           ///< 8 octets au plus.
		/// Hors fil : entité de faible priorité (PNJ d'ambiance, chantier 4.5) mise à jour
		/// un snapshot sur deux. Jamais sérialisé.
		bool halfRate = false;

		/// Égalité des champs transmis (halfRate exclu).
		bool SameWireState(const WireEntity& other) const;
	};

	/// Convertit un état de jeu (flottants) en état du fil (quantifié). Les noms et
	/// genres trop longs sont tronqués aux bornes du décodeur (64 / 8 octets).
	WireEntity Quantize(const engine::server::SnapshotEntity& entity);

	/// Convertit un état du fil en état de jeu (orientation ramenée dans ]-π, π]).
	engine::server::SnapshotEntity Dequantize(const WireEntity& entity);

	/// Un snapshot complet : ses entités triées par identifiant croissant.
	struct Frame
	{
		uint32_t seq = 0;
		std::vector<WireEntity> entities;
	};

	/// En-tête de chaque morceau de snapshot.
	struct ChunkHeader
	{
		uint32_t clientId = 0;
		uint32_t serverTick = 0;
		uint32_t snapshotSeq = 0;      ///< numéro de ce snapshot (>= 1).
		uint32_t baselineSeq = 0;      ///< base utilisée (0 = snapshot complet).
		uint16_t connectedClients = 0;
		uint16_t chunkIndex = 0;
		uint16_t chunkCount = 1;
		uint32_t receivedPackets = 0;
		uint32_t sentPackets = 0;
	};

	/// Budget de charge utile d'un morceau (octets) : 1200 o de datagramme moins
	/// l'enveloppe chiffrée (29 o) et l'en-tête du protocole (8 o), avec marge.
	inline constexpr size_t kDefaultChunkPayloadBudget = 1100u;
	/// Taille fixe de l'en-tête d'un morceau dans la charge utile.
	inline constexpr size_t kChunkHeaderSize = 34u;
	/// Plafond de morceaux par snapshot (protège le décodeur). Large devant le pire cas
	/// de kMaxEntitiesPerSnapshot entités complètes (~170 o chacune) plus leurs retraits.
	inline constexpr uint16_t kMaxChunksPerSnapshot = 128u;
	/// Plafond d'entités dans un snapshot (le shard en envoie au plus
	/// server.replication.view_cap_entities, 100 par défaut).
	inline constexpr size_t kMaxEntitiesPerSnapshot = 512u;

	/// Écrit \p current (trié par identifiant) par rapport à \p baseline (nullptr =
	/// complet), découpé en morceaux d'au plus \p chunkPayloadBudget octets de charge
	/// utile. \p headerTemplate fournit les champs communs (chunkIndex / chunkCount /
	/// baselineSeq sont remplis ici). Chaque élément retourné est un paquet complet du
	/// protocole de jeu (MessageKind::Snapshot). Au moins un paquet ; vide seulement si
	/// le snapshot dépasserait kMaxChunksPerSnapshot (rien ne doit alors partir).
	std::vector<std::vector<std::byte>> EncodeSnapshotDelta(const ChunkHeader& headerTemplate,
		const std::vector<WireEntity>& current, const Frame* baseline,
		size_t chunkPayloadBudget = kDefaultChunkPayloadBudget);

	/// Mise à jour d'une entité dans un morceau décodé.
	struct EntityUpdate
	{
		uint8_t mask = 0;   ///< champs présents (cf. kField*).
		/// Position : delta (cm) si l'entité est dans la base, valeur absolue sinon.
		int64_t position[3] = { 0, 0, 0 };
		/// Autres champs présents (entityId toujours rempli ; positions inutilisées).
		WireEntity values;
	};

	/// Un morceau décodé (pas encore appliqué à une base).
	struct DecodedChunk
	{
		ChunkHeader header;
		std::vector<engine::server::EntityId> removed;
		std::vector<EntityUpdate> updates;
	};

	/// Décode un paquet Snapshot. \return false si l'en-tête ou la charge utile est
	/// mal formé (le paquet est alors ignoré).
	bool DecodeSnapshotChunk(std::span<const std::byte> packet, DecodedChunk& outChunk);

	/// Bits du masque de champs d'une entrée.
	inline constexpr uint8_t kFieldPosition = 1u << 0;
	inline constexpr uint8_t kFieldYaw = 1u << 1;
	inline constexpr uint8_t kFieldVelocity = 1u << 2;
	inline constexpr uint8_t kFieldHealth = 1u << 3;
	inline constexpr uint8_t kFieldMaxHealth = 1u << 4;
	inline constexpr uint8_t kFieldStateFlags = 1u << 5;
	inline constexpr uint8_t kFieldAnimation = 1u << 6;
	inline constexpr uint8_t kFieldIdentity = 1u << 7;
	inline constexpr uint8_t kFieldAll = 0xFFu;

	/// Côté shard, un par client : garde les derniers snapshots envoyés, choisit la
	/// base (le dernier snapshot acquitté encore en mémoire) et numérote.
	/// Pas thread-safe (thread de jeu).
	class SnapshotSender
	{
	public:
		/// Snapshots envoyés gardés en mémoire (1,6 s à 10 Hz). Un acquittement plus
		/// ancien ne sert plus de base : le snapshot suivant part complet.
		static constexpr size_t kHistorySize = 16u;

		/// Prend en compte l'acquittement du client. Ignore un numéro jamais envoyé
		/// ou plus ancien que le dernier acquittement.
		void OnAck(uint32_t seq);

		/// Construit les paquets du prochain snapshot. \p current peut être dans
		/// n'importe quel ordre (trié ici). Les entités halfRate gardent l'état de la
		/// base un snapshot sur deux (chantier 4.5).
		/// \param headerTemplate champs communs (clientId, serverTick, compteurs).
		std::vector<std::vector<std::byte>> BuildPackets(const ChunkHeader& headerTemplate,
			std::vector<WireEntity> current, size_t chunkPayloadBudget = kDefaultChunkPayloadBudget);

		/// Oublie tout (nouvelle session client, changement de zone).
		void Reset();

		/// Dernier numéro envoyé (0 = aucun).
		uint32_t LastSentSeq() const { return m_nextSeq - 1u; }
		/// Dernier numéro acquitté (0 = aucun).
		uint32_t LastAckedSeq() const { return m_lastAcked; }
		/// Base utilisée par le dernier snapshot (0 = complet). Diagnostic, tests.
		uint32_t LastBaselineSeq() const { return m_lastBaseline; }

	private:
		const Frame* FindFrame(uint32_t seq) const;

		std::array<Frame, kHistorySize> m_history{};
		uint32_t m_nextSeq = 1;
		uint32_t m_lastAcked = 0;
		uint32_t m_lastBaseline = 0;
	};

	/// Côté client : rassemble les morceaux, applique le delta à la base, garde les
	/// derniers snapshots reconstruits et fournit le numéro à acquitter.
	/// Pas thread-safe (thread principal).
	class SnapshotReceiver
	{
	public:
		/// Snapshots reconstruits gardés (le shard ne remonte jamais plus loin que
		/// SnapshotSender::kHistorySize).
		static constexpr size_t kHistorySize = 32u;

		/// Résultat de l'application d'un paquet.
		enum class Result : uint8_t
		{
			Pending,   ///< Morceau gardé, snapshot incomplet.
			Committed, ///< Snapshot complet reconstruit : \p outEntities rempli.
			Dropped,   ///< Paquet mal formé, trop ancien, ou base inconnue.
		};

		/// Applique un paquet Snapshot. En cas de Committed, \p outEntities contient
		/// l'ensemble des entités du snapshot (état de jeu) et \p outHeader son
		/// en-tête (chunkIndex 0, chunkCount 1).
		Result Apply(std::span<const std::byte> packet, std::vector<engine::server::SnapshotEntity>& outEntities,
			ChunkHeader& outHeader);

		/// Numéro à acquitter : dernier snapshot reconstruit (0 = aucun).
		uint32_t LatestCommittedSeq() const { return m_latestCommitted; }

		/// Oublie tout (nouvelle socket / nouvelle session).
		void Reset();

		/// Snapshots abandonnés (base inconnue, morceau manquant remplacé) — diagnostic.
		uint64_t DroppedCount() const { return m_dropped; }

	private:
		const Frame* FindFrame(uint32_t seq) const;
		bool Commit(std::vector<engine::server::SnapshotEntity>& outEntities, ChunkHeader& outHeader);

		std::array<Frame, kHistorySize> m_history{};
		uint32_t m_latestCommitted = 0;
		uint64_t m_dropped = 0;

		// Snapshot en cours de rassemblement.
		bool m_assembling = false;
		ChunkHeader m_assemblyHeader{};
		std::vector<bool> m_chunksSeen;
		uint16_t m_chunksReceived = 0;
		std::vector<DecodedChunk> m_assemblyChunks;
	};
}
