#pragma once
// RetiredGameplayHandler — réponse du master aux opcodes des anciennes fonctions
// de gameplay « V1 » qui étaient simulées dans masterd (ETAPE1.1, plan
// d'amélioration issu de l'analyse City of Heroes).
//
// Règle d'architecture : masterd est un relais pur, sans aucune simulation de
// gameplay ; toute simulation vit dans shardd. Les handlers suivants ont donc été
// supprimés du master : échange (Trade), recherche de groupe (LFG), compétences
// (Skills), arène, champs de bataille, JcJ extérieur, météo, événements de jeu,
// objets interactifs, guildes V1 (2 guildes codées en dur), hôtel des ventes V1
// (8 annonces codées en dur) et jets de loot.
//
// Un client ancien qui envoie encore l'un de ces opcodes reçoit un paquet ERROR
// (UNKNOWN_OPCODE, message lisible) au lieu d'être ignoré en silence : sa requête
// ne reste pas en attente jusqu'au timeout.
//
// Thread-safety : HandlePacket est appelé depuis le thread réseau du master,
// comme les autres handlers ; aucun état mutable.

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace engine::server
{
	class NetServer;

	/// Vrai si \p opcode est une requête client d'une fonction de gameplay retirée du master.
	bool IsRetiredGameplayOpcode(uint16_t opcode);

	/// Nom court de la fonction retirée qui portait \p opcode (ex. « trade »),
	/// ou vue vide si l'opcode n'est pas retiré. Sert aux logs et au message d'erreur.
	std::string_view RetiredGameplayFeature(uint16_t opcode);

	class RetiredGameplayHandler
	{
	public:
		void SetServer(NetServer* server) { m_server = server; }

		/// Répond ERROR(UNKNOWN_OPCODE) à une requête d'une fonction retirée.
		/// Effet de bord : un paquet envoyé sur \p connId, une ligne de log (niveau Info).
		/// \param requestId request_id de la requête, recopié dans la réponse pour que
		///        le client résolve sa requête en attente.
		void HandlePacket(uint32_t connId, uint16_t opcode, uint32_t requestId,
			uint64_t sessionIdHeader, const uint8_t* payload, size_t payloadSize);

	private:
		NetServer* m_server = nullptr;
	};
}
