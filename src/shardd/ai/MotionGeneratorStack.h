#pragma once
// CMANGOS.20 (Phase 3.20a) — MotionGeneratorStack : pile prioritaire
// de generateurs de mouvement pour creature AI. Un generateur de
// priorite plus haute prend la main (push), puis on revient au
// precedent quand il est termine (pop).
//
// Header-only. Pas de wire ni DB.

#include <cstdint>
#include <memory>
#include <vector>

namespace engine::server::ai
{
	enum class GeneratorKind : uint8_t
	{
		Idle      = 0,
		Random    = 1,    ///< errance autour du spawn
		Waypoint  = 2,    ///< chemin scripte
		Chase     = 3,    ///< poursuite cible (combat)
		Flee      = 4,    ///< fuite
		Stunned   = 5,    ///< incapacite
	};

	/// Interface : chaque generateur expose son kind + un Update.
	class IMotionGenerator
	{
	public:
		virtual ~IMotionGenerator() = default;
		virtual GeneratorKind Kind() const = 0;
		/// Retourne true tant que le generateur veut continuer ; false
		/// = pop (exhausted ou conditions non remplies).
		virtual bool Update(uint64_t deltaMs) = 0;
	};

	class MotionGeneratorStack
	{
	public:
		void Push(std::unique_ptr<IMotionGenerator> gen)
		{
			m_stack.push_back(std::move(gen));
		}

		void Clear() { m_stack.clear(); }

		size_t Size() const noexcept { return m_stack.size(); }

		IMotionGenerator* Top() const noexcept
		{
			return m_stack.empty() ? nullptr : m_stack.back().get();
		}

		/// Avance le generateur courant. Si Update retourne false, le
		/// pop. Si la pile devient vide, retourne false (l'AI doit
		/// pousser un Idle par defaut).
		bool Tick(uint64_t deltaMs)
		{
			while (!m_stack.empty())
			{
				if (m_stack.back()->Update(deltaMs))
					return true;
				m_stack.pop_back();
			}
			return false;
		}

	private:
		std::vector<std::unique_ptr<IMotionGenerator>> m_stack;
	};

	// --- Implémentations minimales pour test + démo. ---

	class IdleGenerator final : public IMotionGenerator
	{
	public:
		GeneratorKind Kind() const override { return GeneratorKind::Idle; }
		bool Update(uint64_t) override { return true; }  // never exhausts
	};

	/// Random walks pour `durationMs` puis se termine.
	class RandomGenerator final : public IMotionGenerator
	{
	public:
		explicit RandomGenerator(uint64_t durationMs) : m_remaining(durationMs) {}
		GeneratorKind Kind() const override { return GeneratorKind::Random; }
		bool Update(uint64_t deltaMs) override
		{
			if (deltaMs >= m_remaining)
			{
				m_remaining = 0;
				return false;
			}
			m_remaining -= deltaMs;
			return true;
		}
	private:
		uint64_t m_remaining;
	};
}
