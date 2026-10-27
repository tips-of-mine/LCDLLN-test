// ETAPE4.4 — agenda déterministe des PNJ. Voir DailyAgenda.h.

#include "src/shardd/gameplay/ambient/DailyAgenda.h"

#include <algorithm>
#include <cmath>

namespace engine::server::ambient
{
	namespace
	{
		constexpr double kDaySeconds = 86400.0;
		constexpr double kTwoPi = 6.283185307179586;

		using A = AgendaActivity;
		using P = AgendaPlace;

		// Routines de métier. Règle : le premier créneau commence à 0 h et reprend le
		// lieu et l'activité du dernier (la nuit traverse minuit sans déplacement).
		const std::vector<AgendaSlot> kFermier{
			{ 0.0f, P::Home, A::Sleep }, { 5.5f, P::Work, A::Work }, { 12.0f, P::Tavern, A::Eat },
			{ 13.0f, P::Work, A::Work }, { 19.0f, P::Home, A::Rest }, { 21.0f, P::Home, A::Sleep } };
		const std::vector<AgendaSlot> kArtisan{
			{ 0.0f, P::Home, A::Sleep }, { 6.5f, P::Work, A::Work }, { 12.0f, P::Tavern, A::Eat },
			{ 13.0f, P::Work, A::Work }, { 18.5f, P::Tavern, A::Eat }, { 21.0f, P::Home, A::Sleep } };
		const std::vector<AgendaSlot> kMarchand{
			{ 0.0f, P::Home, A::Sleep }, { 7.0f, P::Market, A::Work }, { 12.5f, P::Tavern, A::Eat },
			{ 13.5f, P::Market, A::Work }, { 19.0f, P::Square, A::Stroll }, { 21.0f, P::Home, A::Sleep } };
		const std::vector<AgendaSlot> kAubergiste{
			{ 0.0f, P::Home, A::Sleep }, { 8.0f, P::Tavern, A::Work }, { 15.0f, P::Square, A::Stroll },
			{ 16.0f, P::Tavern, A::Work }, { 23.5f, P::Home, A::Sleep } };
		const std::vector<AgendaSlot> kPretre{
			{ 0.0f, P::Home, A::Sleep }, { 6.0f, P::Temple, A::Pray }, { 12.0f, P::Home, A::Rest },
			{ 13.0f, P::Temple, A::Work }, { 18.0f, P::Square, A::Stroll }, { 20.0f, P::Temple, A::Pray },
			{ 22.0f, P::Home, A::Sleep } };
		const std::vector<AgendaSlot> kFlaneur{
			{ 0.0f, P::Home, A::Sleep }, { 8.0f, P::Square, A::Stroll }, { 10.0f, P::Market, A::Stroll },
			{ 12.0f, P::Tavern, A::Eat }, { 14.0f, P::Square, A::Stroll }, { 17.0f, P::Market, A::Stroll },
			{ 19.0f, P::Tavern, A::Eat }, { 22.0f, P::Home, A::Sleep } };

		/// Mélange 64 bits (splitmix64).
		uint64_t Mix(uint64_t x)
		{
			x += 0x9E3779B97F4A7C15ull;
			x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
			x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
			return x ^ (x >> 31);
		}

		uint64_t Mix(uint64_t a, uint64_t b, uint64_t c = 0u, uint64_t d = 0u)
		{
			return Mix(Mix(Mix(Mix(a) ^ b) ^ c) ^ d);
		}

		/// Réel uniforme [0, 1) tiré d'un mélange.
		double Unit(uint64_t h)
		{
			return static_cast<double>(h >> 11) * (1.0 / 9007199254740992.0);
		}

		/// Début (s de jeu depuis minuit) du créneau \p i le jour \p day, avec le décalage
		/// de la variante. Le décalage reste sous 45 % des écarts avec les créneaux voisins :
		/// l'ordre des créneaux ne change jamais.
		double SlotStart(const std::vector<AgendaSlot>& slots, size_t i, int64_t day, uint64_t seed, double jitterSeconds)
		{
			const double base = static_cast<double>(slots[i].startHour) * 3600.0;
			if (i == 0u || jitterSeconds <= 0.0)
				return base;
			const double prev = static_cast<double>(slots[i - 1u].startHour) * 3600.0;
			const double next = (i + 1u < slots.size()) ? static_cast<double>(slots[i + 1u].startHour) * 3600.0 : kDaySeconds;
			const double maxJitter = (std::min)(jitterSeconds, 0.45 * (std::min)(base - prev, next - base));
			const double u = Unit(Mix(seed, static_cast<uint64_t>(day), static_cast<uint64_t>(i), 0x51u));
			return base + (u * 2.0 - 1.0) * maxJitter;
		}

		/// Point d'arrêt sur le lieu du créneau \p slotIndex le jour \p day.
		RoutinePoint PlacePoint(const AgendaDefinition& def, AgendaPlace place, uint64_t seed, int64_t day,
			size_t slotIndex, RoutinePoint fallback)
		{
			const std::vector<RoutinePoint>* points = &def.places[static_cast<size_t>(place)];
			if (points->empty())
				points = &def.places[static_cast<size_t>(AgendaPlace::Home)];
			if (points->empty())
				return fallback;
			// Domicile et travail : toujours les mêmes pour un PNJ ; auberge, marché, place,
			// temple : choisis chaque jour.
			const bool fixed = place == AgendaPlace::Home || place == AgendaPlace::Work;
			const uint64_t pick = fixed ? Mix(seed, static_cast<uint64_t>(place), 0x7Au)
				: Mix(seed, static_cast<uint64_t>(day), static_cast<uint64_t>(slotIndex), static_cast<uint64_t>(place));
			RoutinePoint p = (*points)[static_cast<size_t>(pick % points->size())];
			// Petit décalage stable (≤ 1,5 m, sauf au domicile) : deux PNJ du même lieu ne
			// se tiennent pas au même point.
			if (place != AgendaPlace::Home)
			{
				const uint64_t h = Mix(seed, static_cast<uint64_t>(slotIndex), static_cast<uint64_t>(place), 0x0Fu);
				const double angle = Unit(h) * kTwoPi;
				const double radius = 0.4 + Unit(Mix(h)) * 1.1;
				p.x += static_cast<float>(std::cos(angle) * radius);
				p.z += static_cast<float>(std::sin(angle) * radius);
			}
			return p;
		}
	}

	const std::vector<AgendaSlot>* FindProfession(std::string_view profession)
	{
		if (profession == "fermier") return &kFermier;
		if (profession == "artisan") return &kArtisan;
		if (profession == "marchand") return &kMarchand;
		if (profession == "aubergiste") return &kAubergiste;
		if (profession == "pretre") return &kPretre;
		if (profession == "flaneur") return &kFlaneur;
		return nullptr;
	}

	std::vector<std::string_view> ProfessionIds()
	{
		return { "fermier", "artisan", "marchand", "aubergiste", "pretre", "flaneur" };
	}

	bool ParseAgendaPlace(std::string_view text, AgendaPlace& out)
	{
		if (text == "home") { out = AgendaPlace::Home; return true; }
		if (text == "work") { out = AgendaPlace::Work; return true; }
		if (text == "tavern") { out = AgendaPlace::Tavern; return true; }
		if (text == "market") { out = AgendaPlace::Market; return true; }
		if (text == "square") { out = AgendaPlace::Square; return true; }
		if (text == "temple") { out = AgendaPlace::Temple; return true; }
		return false;
	}

	uint64_t AgendaSeed(std::string_view spawnerId, uint32_t slotIndex)
	{
		uint64_t h = 1469598103934665603ull;
		for (const char c : spawnerId)
			h = (h ^ static_cast<uint8_t>(c)) * 1099511628211ull;
		h = (h ^ slotIndex) * 1099511628211ull;
		return Mix(h);
	}

	AgendaState EvaluateAgenda(const AgendaDefinition& def, uint64_t seed, double gameSeconds,
		float walkSpeedMetersPerSecond, double gameSecondsPerRealSecond, RoutinePoint fallback)
	{
		AgendaState state{};
		const std::vector<AgendaSlot>* slots = def.enabled ? FindProfession(def.profession) : nullptr;
		if (slots == nullptr || slots->empty() || !std::isfinite(gameSeconds))
		{
			state.activity = AgendaActivity::Rest;
			state.target = fallback;
			state.position = fallback;
			return state;
		}
		const double jitter = std::clamp(static_cast<double>(def.jitterMinutes), 0.0, 180.0) * 60.0;
		const int64_t day = static_cast<int64_t>(std::floor(gameSeconds / kDaySeconds));
		const double timeOfDay = gameSeconds - static_cast<double>(day) * kDaySeconds;

		// Créneau courant : le dernier dont le début (décalé) est passé.
		size_t current = 0;
		for (size_t i = 1; i < slots->size(); ++i)
		{
			if (SlotStart(*slots, i, day, seed, jitter) <= timeOfDay)
				current = i;
		}
		const double startOfDay = static_cast<double>(day) * kDaySeconds;
		state.slotIndex = static_cast<uint32_t>(current);
		state.slotStartGameSeconds = startOfDay + SlotStart(*slots, current, day, seed, jitter);
		state.place = (*slots)[current].place;
		state.activity = (*slots)[current].activity;
		state.target = PlacePoint(def, state.place, seed, day, current, fallback);

		// Lieu du créneau précédent (veille pour le premier créneau) : point de départ.
		const size_t previous = current == 0u ? slots->size() - 1u : current - 1u;
		const int64_t previousDay = current == 0u ? day - 1 : day;
		const RoutinePoint origin = PlacePoint(def, (*slots)[previous].place, seed, previousDay, previous, fallback);
		const double dx = static_cast<double>(state.target.x) - static_cast<double>(origin.x);
		const double dz = static_cast<double>(state.target.z) - static_cast<double>(origin.z);
		const double distance = std::sqrt(dx * dx + dz * dz);
		const double speed = (std::max)(static_cast<double>(walkSpeedMetersPerSecond), 0.1);
		const double rate = (std::max)(gameSecondsPerRealSecond, 1e-3);
		const double travelGameSeconds = distance / speed * rate;
		const double elapsed = gameSeconds - state.slotStartGameSeconds;
		if (distance > 0.5 && elapsed >= 0.0 && elapsed < travelGameSeconds)
		{
			const double k = elapsed / travelGameSeconds;
			state.travelling = true;
			state.position.x = static_cast<float>(static_cast<double>(origin.x) + dx * k);
			state.position.z = static_cast<float>(static_cast<double>(origin.z) + dz * k);
		}
		else
		{
			state.position = state.target;
		}
		state.indoors = state.activity == AgendaActivity::Sleep && !state.travelling;
		return state;
	}

	RoutinePoint StrollPoint(const AgendaDefinition& def, uint64_t seed, const AgendaState& state,
		double gameSeconds, double periodGameSeconds)
	{
		if (state.activity != AgendaActivity::Stroll || state.travelling || !std::isfinite(gameSeconds))
			return state.target;
		const double period = (std::max)(periodGameSeconds, 1.0);
		const uint64_t step = static_cast<uint64_t>(static_cast<int64_t>(std::floor(gameSeconds / period)));
		const uint64_t h = Mix(seed, step, 0x57u);
		const double angle = Unit(h) * kTwoPi;
		const double radius = Unit(Mix(h)) * std::clamp(static_cast<double>(def.strollRadiusMeters), 0.0, 30.0);
		RoutinePoint p = state.target;
		p.x += static_cast<float>(std::cos(angle) * radius);
		p.z += static_cast<float>(std::sin(angle) * radius);
		return p;
	}
}
