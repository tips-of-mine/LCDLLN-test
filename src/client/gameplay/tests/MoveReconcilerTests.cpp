// ETAPE3.3 — tests du mouvement tranché par le shard, côté client :
//   - le CharacterController réel, piloté comme dans le jeu (60 images/s, commandes à
//     20 Hz), n'est JAMAIS corrigé par les règles du shard en course, sprint, saut et
//     esquive (client et shard s'accordent) ;
//   - même avec une recharge d'esquive côté client (comme Engine) ;
//   - après une correction, MoveReconciler donne l'écart à appliquer, le personnage
//     décalé garde sa vitesse et son saut, et une correction de suite pour une commande
//     déjà en route ne recale que le reste de l'écart.
// Pattern repo : int main() + vérifications + code retour.

#include "src/client/gameplay/CharacterController.h"
#include "src/client/gameplay/MoveReconciler.h"
#include "src/shared/movement/MovementRules.h"

#include <cmath>
#include <cstdio>
#include <vector>

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

	using engine::gameplay::CharacterController;
	using engine::gameplay::IWorldCollider;
	using engine::gameplay::MoveInput;
	using engine::gameplay::MoveReconciler;
	using engine::math::Vec3;

	/// Sol plat à y = 0 (même principe que character_controller_jump_tests).
	class FlatFloor final : public IWorldCollider
	{
	public:
		bool SweepCapsule(const Capsule& capsule, const Vec3& startCenter, const Vec3& endCenter,
			SweepHit& outHit) const override
		{
			const float halfH = capsule.height * 0.5f;
			const float startBottom = startCenter.y - halfH;
			const float endBottom = endCenter.y - halfH;
			outHit.normal = Vec3(0.0f, 1.0f, 0.0f);
			if (endBottom >= 0.0f)
			{
				outHit.hit = false;
				outHit.fraction = 1.0f;
				return false;
			}
			if (startBottom <= 0.0f)
			{
				outHit.hit = true;
				outHit.fraction = 0.0f;
				return true;
			}
			const float denom = startBottom - endBottom;
			float frac = denom > 0.0f ? startBottom / denom : 0.0f;
			outHit.hit = true;
			outHit.fraction = frac < 0.0f ? 0.0f : (frac > 1.0f ? 1.0f : frac);
			return true;
		}
	};

	/// Joueur simulé comme dans Engine : une image = Update du contrôleur, toutes les
	/// 50 ms une commande part vers les règles du shard.
	struct Session
	{
		FlatFloor floor;
		CharacterController cc;
		MoveReconciler history;
		engine::movement::MovementState server;
		uint32_t seq = 0;
		double clientMs = 1000.0;
		uint64_t wallMs = 100'000;
		double sinceCommand = 0.0;
		uint32_t lastJumpMs = 0;
		uint32_t lastDodgeMs = 0;
		int corrections = 0;
		float maxSpeed = 13.0f;

		Session()
		{
			(void)cc.Init(Vec3(0.0f, 0.9f, 0.0f));
			engine::movement::ResetMovement(server, 0.0f, 0.9f, 0.0f);
		}

		void Frame(const MoveInput& input, bool dodge, float dt = 1.0f / 60.0f)
		{
			(void)cc.Update(dt, input, floor);
			clientMs += static_cast<double>(dt) * 1000.0;
			if (input.jumpPressed)
				lastJumpMs = static_cast<uint32_t>(clientMs);
			// Recharge d'esquive commune (comme Engine).
			const bool dodgeReady = lastDodgeMs == 0u
				|| clientMs - lastDodgeMs >= engine::movement::MovementLimits{}.dodgeCooldownMs;
			if (dodge && dodgeReady)
			{
				cc.ApplyDodgeImpulse(input.moveDirXZ);
				lastDodgeMs = static_cast<uint32_t>(clientMs);
			}
			sinceCommand += dt;
			wallMs += static_cast<uint64_t>(std::lround(dt * 1000.0f));
			if (sinceCommand >= 0.05)
			{
				sinceCommand = 0.0;
				engine::movement::MoveCommand c{};
				c.sequence = ++seq;
				c.clientTimeMs = static_cast<uint32_t>(clientMs);
				c.lastJumpMs = lastJumpMs;
				c.lastDodgeMs = lastDodgeMs;
				const Vec3 p = cc.GetPosition();
				c.x = p.x;
				c.y = p.y;
				c.z = p.z;
				history.RecordCommand(c.sequence, p.x, p.z);
				const auto r = engine::movement::ApplyMoveCommand(server, c, maxSpeed, wallMs);
				if (r.verdict == engine::movement::MoveVerdict::Corrected)
					++corrections;
			}
		}
	};

	MoveInput Sprint(bool jump = false)
	{
		MoveInput in{};
		in.moveDirXZ = Vec3(1.0f, 0.0f, 0.0f);
		in.sprint = true;
		in.jumpPressed = jump;
		return in;
	}
}

int main()
{
	// 1. Client honnête (vitesses du contrôleur, sprint 13 m/s) : jamais corrigé.
	{
		Session s;
		for (int i = 0; i < 600; ++i)
			s.Frame(Sprint(i % 50 == 0), i % 120 == 60); // sauts et esquives réguliers
		Check(s.corrections == 0, "course + sauts + esquives du vrai contrôleur : aucune correction");
		Check(s.cc.GetPosition().x > 100.0f, "le personnage a bien avancé (10 s de sprint)");
	}

	// 2. Vitesse locale trafiquée (contrôleur à 20 m/s, shard à 13) : corrigé.
	{
		Session s;
		s.cc.SetMoveSpeeds(20.0f, 20.0f, 20.0f);
		for (int i = 0; i < 120; ++i)
			s.Frame(Sprint(), false);
		Check(s.corrections > 0, "vitesse trafiquée : corrections");
		Check(s.server.x < s.cc.GetPosition().x - 5.0f, "le shard garde une position en retrait");
	}

	// 1b. Esquives demandées en rafale (toutes les 0,3 s) : la recharge client les
	//     espace comme le shard, aucune correction même à vitesse de course.
	{
		Session s;
		s.maxSpeed = 9.0f; // < 11 m/s : une esquive refusée par le shard se verrait
		for (int i = 0; i < 600; ++i)
		{
			MoveInput in{};
			in.moveDirXZ = Vec3(1.0f, 0.0f, 0.0f);
			s.Frame(in, i % 18 == 0);
		}
		Check(s.corrections == 0, "esquives en rafale + recharge client : aucune correction");
	}

	// 3. Correction : écart appliqué par décalage, état conservé, commandes en route.
	{
		Session s;
		for (int i = 0; i < 60; ++i)
			s.Frame(Sprint(), false);
		const uint32_t correctedSeq = s.seq - 3; // le shard répond pour une commande ancienne
		// Position officielle : 2 m en arrière de la position envoyée pour la commande N.
		MoveReconciler probe = s.history;
		MoveReconciler::Offset none{};
		(void)probe.ApplyCorrection(correctedSeq, 0.0, 0.0, none);
		const double sentX = -none.dx; // position envoyée pour N (écart vers 0)
		const double sentZ = -none.dz;
		MoveReconciler::Offset off{};
		Check(s.history.ApplyCorrection(correctedSeq, sentX - 2.0, sentZ, off), "commande connue");
		Check(std::fabs(off.dx + 2.0) < 1e-6 && std::fabs(off.dz) < 1e-6, "écart = officielle - envoyée");
		const Vec3 velocityBefore = s.cc.GetVelocity();
		const Vec3 before = s.cc.GetPosition();
		s.cc.Translate(static_cast<float>(off.dx), static_cast<float>(off.dz));
		Check(std::fabs(s.cc.GetPosition().x - (before.x - 2.0f)) < 1e-4f, "personnage décalé de 2 m");
		Check(std::fabs(s.cc.GetPosition().y - before.y) < 1e-6f, "hauteur locale conservée");
		Check(std::fabs(s.cc.GetVelocity().x - velocityBefore.x) < 1e-6f, "vitesse conservée (pas de reprise d'élan)");
		Check(s.history.Size() == 3u, "commandes <= N oubliées, 3 suivantes gardées");
		// Correction de suite pour N+1 (déjà en route avant le recalage) : la position
		// officielle est l'ancienne envoyée - 2 m ; l'écart restant est nul.
		MoveReconciler probe2 = s.history;
		MoveReconciler::Offset shifted{};
		(void)probe2.ApplyCorrection(correctedSeq + 1u, 0.0, 0.0, shifted);
		const double shiftedX = -shifted.dx; // position envoyée pour N+1, déjà décalée
		MoveReconciler::Offset follow{};
		Check(s.history.ApplyCorrection(correctedSeq + 1u, shiftedX, -shifted.dz, follow), "correction de suite");
		Check(std::fabs(follow.dx) < 1e-6 && std::fabs(follow.dz) < 1e-6, "correction de suite : rien à recaler");
		// Commande oubliée : refus.
		MoveReconciler::Offset gone{};
		Check(!s.history.ApplyCorrection(correctedSeq, 0.0, 0.0, gone), "commande oubliée : refus");
	}

	// 4. Historique borné, numéros non croissants ignorés.
	{
		MoveReconciler h;
		for (uint32_t i = 1; i <= MoveReconciler::kMaxCommands + 50u; ++i)
			h.RecordCommand(i, i, 0.0);
		h.RecordCommand(5, 0.0, 0.0);
		Check(h.Size() == MoveReconciler::kMaxCommands, "historique plafonné");
		MoveReconciler::Offset o{};
		Check(!h.ApplyCorrection(10, 0.0, 0.0, o), "commande sortie de l'historique : refus");
		h.Clear();
		Check(h.Size() == 0u, "Clear vide l'historique");
	}

	if (s_failCount == 0)
		std::puts("[OK] move_reconciler_tests");
	return s_failCount == 0 ? 0 : 1;
}
