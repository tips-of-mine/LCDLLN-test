#pragma once

#include <chrono>
#include <cstdint>

namespace engine::core
{
	/// Abstraction d'horloge monotone : sépare l'usage de `steady_clock::now()`
	/// de la source du temps. Permet aux tests d'injecter un \ref FakeClock pour
	/// rendre déterministes les vérifications de timeouts/bans/sessions sans
	/// passer par `std::this_thread::sleep_for` (qui rend les tests flaky et
	/// difficiles à lancer en CI sous charge).
	///
	/// Le type de point retourné est volontairement `std::chrono::steady_clock::time_point`
	/// pour rester compatible avec le code existant qui stocke des
	/// `steady_clock::time_point` (SessionManager::Session, RateLimitAndBan::TokenBucket).
	class IClock
	{
	public:
		using TimePoint = std::chrono::steady_clock::time_point;

		virtual ~IClock() = default;

		/// Retourne le "maintenant" courant selon l'horloge.
		virtual TimePoint Now() const = 0;
	};

	/// Implémentation par défaut : délègue à `std::chrono::steady_clock::now()`.
	/// Sans état, thread-safe.
	class SteadyClock final : public IClock
	{
	public:
		TimePoint Now() const override;

		/// Instance partagée (singleton statique) utilisée comme valeur par
		/// défaut quand le code ne fournit pas d'horloge explicite.
		static SteadyClock& Instance();
	};

	/// Horloge contrôlable pour les tests : démarre à T=0, n'avance qu'à l'appel
	/// de \ref Advance ou \ref SetNow. Pas thread-safe (les tests sont
	/// single-threaded ou se synchronisent eux-mêmes).
	class FakeClock final : public IClock
	{
	public:
		FakeClock();

		TimePoint Now() const override;

		/// Avance l'horloge de \a delta millisecondes.
		void AdvanceMs(int64_t deltaMs);

		/// Avance l'horloge de \a delta secondes.
		void AdvanceSec(int64_t deltaSec);

		/// Force l'horloge à un point précis (utile pour réinitialiser).
		void SetNow(TimePoint tp);

	private:
		TimePoint m_now;
	};
}
