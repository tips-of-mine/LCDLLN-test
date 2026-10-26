// ETAPE3.3 — tests des règles de mouvement partagées (sans réseau).
// Pattern repo : int main() + vérifications + code retour.

#include "src/shared/movement/MovementRules.h"

#include <cmath>
#include <cstdio>

namespace
{
	int s_failCount = 0;

	void Check(bool cond, const char* msg)
	{
		if (!cond)
		{
			++s_failCount;
			std::fprintf(stderr, "[FAIL] %s\n", msg);
		}
	}

	using namespace engine::movement;

	constexpr float kSprint = 8.0f; // vitesse maximale autoritaire du test (m/s)

	/// Client honnête : avance de vitesse × 50 ms à chaque commande, horloge = temps réel.
	struct Walker
	{
		MovementState state;
		uint32_t seq = 0;
		uint32_t clientMs = 1000;
		uint64_t wallMs = 50'000;
		float x = 0.0f;
		float y = 0.0f;
		float z = 0.0f;
		uint32_t jumpMs = 0;
		uint32_t dodgeMs = 0;

		MoveResult Step(float speed, uint32_t periodMs = 50, uint32_t wallPeriodMs = 50)
		{
			clientMs += periodMs;
			wallMs += wallPeriodMs;
			x += speed * static_cast<float>(periodMs) / 1000.0f;
			MoveCommand c{};
			c.sequence = ++seq;
			c.clientTimeMs = clientMs;
			c.lastJumpMs = jumpMs;
			c.lastDodgeMs = dodgeMs;
			c.x = x;
			c.y = y;
			c.z = z;
			return ApplyMoveCommand(state, c, kSprint, wallMs);
		}
	};
}

int main()
{
	// 1. Course honnête à la vitesse maximale : toujours acceptée, vitesse officielle juste.
	{
		Walker w;
		ResetMovement(w.state, 0, 0, 0);
		bool allAccepted = true;
		MoveResult last{};
		for (int i = 0; i < 200; ++i)
		{
			last = w.Step(kSprint);
			allAccepted = allAccepted && last.verdict == MoveVerdict::Accepted;
		}
		Check(allAccepted, "course honnête acceptée pendant 10 s");
		Check(std::fabs(last.velocityX - kSprint) < 0.01f, "vitesse officielle = vitesse réelle");
		Check(std::fabs(w.state.x - w.x) < 0.001f, "position officielle = position du client");
	}

	// 2. Vitesse trafiquée (+50 %) : corrigée, ramenée à la limite.
	{
		Walker w;
		ResetMovement(w.state, 0, 0, 0);
		(void)w.Step(kSprint);
		const MoveResult r = w.Step(kSprint * 1.5f);
		Check(r.verdict == MoveVerdict::Corrected, "vitesse +50 % corrigée");
		Check(w.state.x < w.x, "position officielle en retrait de la position trichée");
		Check(r.maxDistanceMeters < kSprint * 1.5f * 0.05f, "distance autorisée inférieure à la triche");
	}

	// 3. Horloge client accélérée (speedhack temporel) : le budget de temps rattrape.
	{
		Walker w;
		ResetMovement(w.state, 0, 0, 0);
		int corrected = 0;
		for (int i = 0; i < 100; ++i)
		{
			// Le client prétend 100 ms simulées toutes les 50 ms réelles, à vitesse légale.
			if (w.Step(kSprint, 100, 50).verdict == MoveVerdict::Corrected)
				++corrected;
		}
		Check(corrected > 50, "horloge accélérée : corrigée dès que la marge est épuisée");
		const float realLimit = kSprint * (100.0f * 0.05f * 1.02f + 0.3f) * 1.1f + 100.0f * 0.05f;
		Check(w.state.x < realLimit, "distance officielle bornée par le temps réel");
	}

	// 4. Temps « mis de côté » limité à 1 s.
	{
		Walker w;
		ResetMovement(w.state, 0, 0, 0);
		(void)w.Step(kSprint);
		w.wallMs += 60'000; // immobile une minute sans commande
		w.clientMs += 60'000;
		// Puis il prétend avoir couru 60 s d'un coup.
		w.x += kSprint * 60.0f;
		MoveCommand c{};
		c.sequence = ++w.seq;
		c.clientTimeMs = w.clientMs;
		c.x = w.x;
		const MoveResult r = ApplyMoveCommand(w.state, c, kSprint, w.wallMs);
		Check(r.verdict == MoveVerdict::Corrected, "60 s d'un coup : corrigé");
		Check(r.acceptedDurationMs <= 1000u, "une commande ne couvre jamais plus d'1 s");
	}

	// 5. Commande perdue : la suivante couvre les deux périodes (instants, pas durées).
	{
		Walker w;
		ResetMovement(w.state, 0, 0, 0);
		(void)w.Step(kSprint);
		w.clientMs += 50;
		w.wallMs += 50;
		w.x += kSprint * 0.05f; // commande perdue
		const MoveResult r = w.Step(kSprint);
		Check(r.verdict == MoveVerdict::Accepted && r.acceptedDurationMs == 100u, "perte d'une commande sans correction");
	}

	// 6. Paquets en retard, rejoués, horloge qui recule : ignorés.
	{
		Walker w;
		ResetMovement(w.state, 0, 0, 0);
		(void)w.Step(kSprint);
		(void)w.Step(kSprint);
		MoveCommand old{};
		old.sequence = 1;
		old.clientTimeMs = w.clientMs;
		Check(ApplyMoveCommand(w.state, old, kSprint, w.wallMs).verdict == MoveVerdict::Stale, "numéro ancien ignoré");
		MoveCommand back{};
		back.sequence = w.seq + 1;
		back.clientTimeMs = w.clientMs - 10;
		Check(ApplyMoveCommand(w.state, back, kSprint, w.wallMs).verdict == MoveVerdict::Stale, "horloge qui recule ignorée");
	}

	// 7. Esquive : 11 m/s pendant 0,45 s acceptés, une fois par seconde au plus.
	{
		Walker w;
		ResetMovement(w.state, 0, 0, 0);
		(void)w.Step(kSprint);
		w.dodgeMs = w.clientMs + 10;
		bool dodgeOk = true;
		for (int i = 0; i < 9; ++i)
			dodgeOk = dodgeOk && w.Step(11.0f).verdict == MoveVerdict::Accepted;
		Check(dodgeOk, "esquive acceptée sur sa durée");
		bool tooSoon = false;
		w.dodgeMs = w.clientMs + 10; // nouvelle esquive 450 ms après la première : refusée
		for (int i = 0; i < 4; ++i)
			tooSoon = tooSoon || w.Step(11.0f).verdict == MoveVerdict::Corrected;
		Check(tooSoon, "esquive répétée trop tôt : vitesse corrigée");
	}

	// 8. Montée : pente 45° acceptée, mur vertical refusé, saut accepté une fois.
	{
		Walker w;
		ResetMovement(w.state, 0, 0, 0);
		(void)w.Step(5.0f);
		w.y += 0.25f; // 0,25 m de montée pour 0,25 m d'avance
		Check(w.Step(5.0f).verdict == MoveVerdict::Accepted, "pente de 45° acceptée");
		w.y += 3.0f;
		{
			const MoveResult r = w.Step(5.0f);
			Check(r.verticalClamped && r.verdict == MoveVerdict::Accepted, "3 m de montée d'un coup : hauteur freinée, pas de recalage");
			Check(w.state.y < w.y - 2.0f, "hauteur officielle en retrait");
		}
		w.y = w.state.y;
		w.jumpMs = w.clientMs + 5;
		w.y += 0.55f + 0.25f; // saut + pente
		Check(!w.Step(5.0f).verticalClamped, "saut accepté");
		w.y += 1.0f;
		Check(w.Step(5.0f).verticalClamped, "second saut sans nouvel instant : freiné");
		w.y = w.state.y;
		w.jumpMs = w.jumpMs + 100; // nouveau saut 100 ms après : trop tôt
		w.y += 1.0f;
		Check(w.Step(0.0f).verticalClamped, "saut répété avant 0,4 s : freiné");
		// Vol soutenu : la hauteur officielle monte au plus vite permis, pas plus.
		const float startY = w.state.y;
		for (int i = 0; i < 20; ++i)
		{
			w.y += 1.0f; // 20 m/s vers le haut, sur place
			(void)w.Step(0.0f);
		}
		Check(w.state.y - startY < 4.0f, "vol : 1 s à 20 m/s ne fait monter la hauteur officielle que de quelques mètres");
	}

	// 9. Chute : descente rapide acceptée, téléport vers le bas refusé.
	{
		Walker w;
		ResetMovement(w.state, 0, 500.0f, 0);
		w.y = 500.0f;
		w.y -= 2.5f; // ~50 m/s pendant 50 ms
		Check(w.Step(0.0f).verdict == MoveVerdict::Accepted, "chute rapide acceptée");
		w.y -= 300.0f;
		Check(w.Step(0.0f).verticalClamped, "300 m vers le bas d'un coup : hauteur freinée");
	}

	// 10. Coordonnées invalides : la position officielle ne bouge pas.
	{
		Walker w;
		ResetMovement(w.state, 10.0f, 0, 0);
		w.x = 10.0f;
		MoveCommand c{};
		c.sequence = 1;
		c.clientTimeMs = 2000;
		c.x = std::nanf("");
		Check(ApplyMoveCommand(w.state, c, kSprint, 60'000).verdict == MoveVerdict::Corrected
			&& w.state.x == 10.0f, "NaN refusé, position conservée");
	}

	// 11. Reset (téléport) : budget remis, numéros anciens toujours refusés.
	{
		Walker w;
		ResetMovement(w.state, 0, 0, 0);
		for (int i = 0; i < 5; ++i)
			(void)w.Step(kSprint);
		ResetMovement(w.state, 1000.0f, 0, 0);
		MoveCommand old{};
		old.sequence = 3;
		old.clientTimeMs = w.clientMs + 50;
		old.x = 1000.0f;
		Check(ApplyMoveCommand(w.state, old, kSprint, w.wallMs + 50).verdict == MoveVerdict::Stale,
			"après un téléport, un numéro ancien reste refusé");
		w.x = 1000.0f;
		Check(w.Step(kSprint).verdict == MoveVerdict::Accepted, "après un téléport, le mouvement repart");
	}

	// 12. Rafale de commandes sans temps écoulé (horloge figée) : aucune marge gagnée.
	{
		Walker w;
		ResetMovement(w.state, 0, 0, 0);
		(void)w.Step(kSprint);
		const float start = w.state.x;
		for (int i = 0; i < 1000; ++i)
		{
			MoveCommand c{};
			c.sequence = ++w.seq;
			c.clientTimeMs = w.clientMs; // horloge figée
			c.x = w.state.x + 0.05f;
			c.y = w.state.y + 0.1f;
			(void)ApplyMoveCommand(w.state, c, kSprint, w.wallMs);
		}
		Check(w.state.x - start < 0.001f, "rafale à horloge figée : pas d'avance horizontale");
		Check(w.state.y < 0.001f, "rafale à horloge figée : pas de montée");
		// Horloge +1 ms par commande : au plus la vitesse permise sur le temps réel couvert.
		const float start2 = w.state.x;
		for (int i = 0; i < 300; ++i)
		{
			MoveCommand c{};
			c.sequence = ++w.seq;
			c.clientTimeMs = ++w.clientMs;
			c.x = w.state.x + 0.05f;
			(void)ApplyMoveCommand(w.state, c, kSprint, w.wallMs);
		}
		Check(w.state.x - start2 < kSprint * 0.6f * 1.1f + 0.05f, "rafale à +1 ms : bornée par le budget de temps");
	}

	if (s_failCount == 0)
		std::puts("[OK] movement_rules_tests");
	return s_failCount == 0 ? 0 : 1;
}
