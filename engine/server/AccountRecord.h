#pragma once

#include "engine/server/LocalizedEmail.h"

#include <cstdint>
#include <string>

namespace engine::server
{
	/// Statut compte (auth) — aligné `accounts.account_status` (0=actif, 2=verrouillé, etc.).
	enum class AccountStatus : uint8_t
	{
		Active = 0,
		Locked = 2,
	};

	/// Enregistrement compte côté master (mémoire ou ligne MySQL `accounts`).
	struct AccountRecord
	{
		uint64_t account_id = 0;
		std::string login;
		std::string email;
		std::string first_name;
		std::string last_name;
		std::string birth_date;
		std::string final_hash; // Argon2 (champ `password_hash` en base)
		AccountStatus status = AccountStatus::Active;
		bool email_verified = false;
		AccountEmailLocale email_locale = AccountEmailLocale::English;
		std::string country_code; ///< Code pays ISO-2 (ex. "FR"). Vide si inconnu.
		std::string tag_id;       ///< Identifiant TAG-ID généré à l'inscription (ex. "FR60400123"). Vide si non généré.
	};
}
