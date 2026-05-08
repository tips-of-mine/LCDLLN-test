#pragma once
// CMANGOS.01 (Phase 2.01b) — ChatChannelRegistry : canaux dynamiques
// in-memory côté master. Permet aux joueurs de /join un canal nommé
// (avec password optionnel), maintient la liste des membres, applique
// une ban-list par canal.
//
// Pas de persistance dans cette PR : tous les canaux dynamiques sont
// volatils (perdus au reboot du master). Les canaux "système"
// (Say/Yell/Guild/...) ne passent PAS par ce registry — ils sont
// résolus directement dans `ChatRelayHandler`.
//
// Design :
//   - Canal identifié par son nom normalisé (lower-case ASCII).
//   - Owner : premier joueur à `Join` un canal qui n'existe pas. Peut
//     `SetPassword`, `Ban`, `Unban`. Si l'owner se déconnecte, son rôle
//     est transféré au plus ancien membre encore présent.
//   - Password : stocké en clair côté master (RAM). Vide = canal libre.
//   - Ban-list : set d'`account_id` ; `Join` retourne `Banned`.
//   - Membership : set d'`account_id` par canal.

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace engine::server::chat
{
	enum class ChannelJoinResult : uint8_t
	{
		/// Joueur ajouté (création du canal le cas échéant).
		OK = 0,
		/// Mot de passe incorrect.
		WrongPassword = 1,
		/// Joueur banni du canal par l'owner.
		Banned = 2,
		/// Nom invalide (vide, trop long, ou caractères interdits).
		InvalidName = 3,
		/// Le joueur est déjà membre — no-op idempotent.
		AlreadyMember = 4,
	};

	struct ChannelInfo
	{
		std::string                       name;            // normalisé
		uint64_t                          ownerAccountId;
		bool                              hasPassword;
		size_t                            memberCount;
	};

	class ChatChannelRegistry
	{
	public:
		ChatChannelRegistry() = default;

		/// Ajoute \p accountId au canal \p channel. Crée le canal si
		/// inexistant (dans ce cas \p accountId devient l'owner).
		///
		/// \param channel  Nom case-insensitive (sera normalisé). 1..32 chars,
		///                 ASCII printable hors espace/'/'.
		/// \param password Optionnel. Si le canal a un password set et que
		///                 \p password ne match pas → `WrongPassword`.
		ChannelJoinResult Join(uint64_t accountId,
			std::string_view channel,
			std::string_view password = {});

		/// Retire \p accountId du canal. Idempotent. Si l'owner part et
		/// que d'autres membres restent, l'ownership passe au prochain
		/// (premier dans la table interne — ordre non-stable mais
		/// déterministe pour une session). Si plus aucun membre, le
		/// canal est supprimé du registry.
		void Leave(uint64_t accountId, std::string_view channel);

		/// Retire \p accountId de TOUS les canaux. Pour le logout/déconnexion.
		void LeaveAll(uint64_t accountId);

		/// Membres d'un canal (snapshot, ordre non-spécifié).
		std::vector<uint64_t> Members(std::string_view channel) const;

		/// True si \p accountId est membre du canal \p channel.
		bool IsMember(uint64_t accountId, std::string_view channel) const;

		/// Set / clear le password. Réservé à l'owner du canal.
		/// Retourne true si OK ; false si \p actor n'est pas l'owner.
		/// Password vide = clear (canal libre).
		bool SetPassword(uint64_t actor, std::string_view channel, std::string_view password);

		/// Ban un account du canal. Réservé à l'owner. Le banni sort
		/// automatiquement du canal s'il y est encore.
		bool Ban(uint64_t actor, std::string_view channel, uint64_t target);

		/// Unban un account. Réservé à l'owner.
		bool Unban(uint64_t actor, std::string_view channel, uint64_t target);

		/// True si \p accountId est banni du canal.
		bool IsBanned(uint64_t accountId, std::string_view channel) const;

		/// Information publique sur un canal (nullopt si inexistant).
		std::optional<ChannelInfo> Info(std::string_view channel) const;

		/// Nombre de canaux actifs (utile pour métriques / debug).
		size_t ChannelCount() const;

	private:
		struct Channel
		{
			std::string                     name;          // normalisé
			uint64_t                        owner = 0;
			std::string                     password;      // vide = pas de password
			std::unordered_set<uint64_t>    members;
			std::unordered_set<uint64_t>    bans;
		};

		/// Normalise + valide un nom de canal. Retourne la chaîne vide si
		/// invalide.
		static std::string NormalizeName(std::string_view name);

		mutable std::mutex                          m_mutex;
		std::unordered_map<std::string, Channel>    m_channels;
	};
}
