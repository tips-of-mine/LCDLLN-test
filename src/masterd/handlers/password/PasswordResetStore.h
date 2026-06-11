#pragma once

// M33.2 — In-memory store for password reset tokens and email verification codes.
//
// Audit 2026-06-10 (Lot B1) — THREAD-SAFE : appelé depuis les workers NetServer
// (PasswordResetHandler) ; l'ancien contrat « caller must serialise access »
// n'était pas honoré. Chaque méthode publique verrouille m_mutex (aucune
// méthode publique n'en appelle une autre).

#include <chrono>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

namespace engine::server
{
	/// In-memory store for password reset tokens and email verification codes (M33.2).
	/// - Reset tokens: 32-char hex (16 random bytes), 1-hour expiry, single-use.
	/// - Verification codes: 6-digit decimal string, 15-minute expiry.
	/// - Email rate limiting: max kMaxEmailsPerHour sends per account per rolling hour.
	class PasswordResetStore
	{
	public:
		/// Maximum email sends per account within a 1-hour rolling window (anti-spam).
		static constexpr int kMaxEmailsPerHour = 3;

		/// Generates a new 32-char hex reset token for the given account and stores it (1-hour expiry).
		/// Any previous unused token for the same account is superseded (new token is canonical).
		/// Returns the token string, or empty on generation failure.
		std::string CreateResetToken(uint64_t account_id);

		/// Validates a reset token. Returns the associated account_id if the token exists,
		/// is not expired, and has not been used. Returns nullopt otherwise (also logs reason).
		std::optional<uint64_t> ValidateResetToken(const std::string& token) const;

		/// Marks a reset token as used (single-use). Returns false if token not found.
		bool MarkResetTokenUsed(const std::string& token);

		/// Generates a new 6-digit verification code for the given account and stores it (15-minute expiry).
		/// Overwrites any existing code for the account.
		/// Returns the code string, or empty on generation failure.
		std::string CreateVerificationCode(uint64_t account_id);

		/// Validates an email verification code for the given account.
		/// Returns true if the code matches, is not expired, and is not already verified.
		bool ValidateVerificationCode(uint64_t account_id, const std::string& code) const;

		/// Marks the verification entry for the account as verified.
		/// Returns false if no entry found for account_id.
		bool MarkEmailVerified(uint64_t account_id);

		/// Returns true if the account is allowed to send another email (rate limit check).
		bool CanSendEmail(uint64_t account_id) const;

		/// Records that an email was sent for the account (increments rate-limit counter).
		void RecordEmailSent(uint64_t account_id);

	private:
		using Clock = std::chrono::system_clock;

		struct ResetTokenEntry
		{
			uint64_t   account_id;
			Clock::time_point expires_at;
			bool       used = false;
		};

		struct VerificationEntry
		{
			std::string       code;
			Clock::time_point expires_at;
			bool              verified = false;
		};

		struct EmailRateEntry
		{
			int                              count        = 0;
			/// Début de la fenêtre glissante anti-spam (1h). `std::nullopt`
			/// tant qu'aucun email n'a encore été envoyé pour ce compte —
			/// distingue ce cas de "fenêtre démarrée à epoch UNIX". Pattern
			/// aligné sur la convention défensive FU-3/FU-4. NB : ce store
			/// utilise `system_clock` (wall-clock) et non `steady_clock` —
			/// le sentinel `time_point{}` = epoch UNIX 1970 était inoffensif
			/// en prod car `now()` ≈ 2026 → la condition "fenêtre expirée"
			/// se déclenchait par coïncidence (~56 ans ≫ 1h). L'optional
			/// rend l'intention explicite et protège des futurs cas de test.
			std::optional<Clock::time_point> window_start;
		};

		/// Audit Lot B1 — protège les 3 maps contre les workers concurrents.
		mutable std::mutex m_mutex;
		/// token string → entry
		std::unordered_map<std::string, ResetTokenEntry> m_reset_tokens;
		/// account_id → verification entry (one active per account)
		std::unordered_map<uint64_t, VerificationEntry>  m_verifications;
		/// account_id → rate-limit bucket
		std::unordered_map<uint64_t, EmailRateEntry>     m_email_rate;
	};

} // namespace engine::server
