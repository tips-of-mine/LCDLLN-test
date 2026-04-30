#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

namespace engine::server
{
	/// Phase 4 chat — Maps a master TCP connection to the character the user is currently
	/// playing (declared by the client via kOpcodeCharacterEnterWorldRequest after EnterWorld).
	///
	/// Used by ChatRelayHandler for :
	///  - sender display in CHAT_RELAY (preferred over the AccountStore login).
	///  - whisper target resolution (lookup connId by normalized character name).
	///
	/// Thread-safe. Snapshot/Find helpers copy under the mutex.
	class SessionCharacterMap
	{
	public:
		struct CharacterInfo
		{
			uint64_t    characterId = 0;
			std::string characterName;        ///< Original UTF-8, displayed as-is.
			std::string normalizedName;       ///< Lowercased ASCII for whisper target match.
		};

		/// Register or update the active character for \a connId.
		/// Removes any previous binding for the same connId and any name → conn binding
		/// that pointed at the old name (consistency under updates).
		void Set(uint32_t connId, uint64_t characterId, std::string characterName, std::string normalizedName);

		/// Drop the binding for \a connId (call on connection close).
		void Remove(uint32_t connId);

		/// Snapshot of the character info for \a connId, or nullopt if not registered.
		std::optional<CharacterInfo> GetByConnId(uint32_t connId) const;

		/// Resolve a whisper target by normalized name → connId. Nullopt if not online.
		std::optional<uint32_t> FindConnByNormalizedName(const std::string& normalizedName) const;

		/// Lowercase ASCII normalization (anything outside 0x00–0x7F is left untouched ;
		/// case folding restricted to 'A'–'Z'). Whisper lookup uses byte-equality on the
		/// normalized form, so non-ASCII names match exactly (no case-insensitive match
		/// for accentuated characters in v1).
		static std::string Normalize(const std::string& name);

	private:
		mutable std::mutex m_mutex;
		std::unordered_map<uint32_t, CharacterInfo>   m_byConn;
		std::unordered_map<std::string, uint32_t>     m_byNormalizedName;
	};
}
