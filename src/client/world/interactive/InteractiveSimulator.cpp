// M100.32 — Implémentation InteractiveSimulator (logique pure).

#include "src/client/world/interactive/InteractiveSimulator.h"

namespace engine::world::interactive
{
	namespace
	{
		inline float Clamp01(float v)
		{
			if (v < 0.0f) return 0.0f;
			if (v > 1.0f) return 1.0f;
			return v;
		}

		inline bool IsRotational(InteractiveType t)
		{
			return t != InteractiveType::DoorSliding;
		}
	}

	InteractiveRuntimeState MakeInitialRuntimeState(const InteractivePropInstance& def)
	{
		InteractiveRuntimeState rt;
		rt.targetState = def.initialState != 0u ? 1u : 0u;
		rt.openFactor  = rt.targetState != 0u ? 1.0f : 0.0f;
		return rt;
	}

	uint8_t ToggleInteractive(InteractiveRuntimeState& rt)
	{
		rt.targetState = rt.targetState != 0u ? 0u : 1u;
		return rt.targetState;
	}

	void SetInteractiveTarget(InteractiveRuntimeState& rt, uint8_t newState)
	{
		rt.targetState = newState != 0u ? 1u : 0u;
	}

	void UpdateInteractive(InteractiveRuntimeState& rt, const InteractivePropInstance& def, float dtSec)
	{
		const float target = rt.targetState != 0u ? 1.0f : 0.0f;

		// Durée non positive : application instantanée.
		if (def.animDurationSec <= 0.0f)
		{
			rt.openFactor = target;
			return;
		}

		if (dtSec < 0.0f) dtSec = 0.0f;
		const float step = dtSec / def.animDurationSec; // fraction de course parcourue.

		if (rt.openFactor < target)
		{
			rt.openFactor = Clamp01(rt.openFactor + step);
			if (rt.openFactor > target) rt.openFactor = target;
		}
		else if (rt.openFactor > target)
		{
			rt.openFactor = Clamp01(rt.openFactor - step);
			if (rt.openFactor < target) rt.openFactor = target;
		}
	}

	void ApplyRemoteState(InteractiveRuntimeState& rt, const InteractivePropInstance& def,
		uint8_t newState, float latencySec)
	{
		SetInteractiveTarget(rt, newState);
		if (latencySec < 0.0f) latencySec = 0.0f;

		const float target = rt.targetState != 0u ? 1.0f : 0.0f;

		// Durée non positive : la porte est déjà à destination.
		if (def.animDurationSec <= 0.0f)
		{
			rt.openFactor = target;
			return;
		}

		// Avance la course de la fraction écoulée depuis l'évènement distant.
		const float advanced = Clamp01(latencySec / def.animDurationSec);
		if (target >= 1.0f)
			rt.openFactor = Clamp01(rt.openFactor + advanced);
		else
			rt.openFactor = Clamp01(rt.openFactor - advanced);
	}

	float ComputeOpenAngleDeg(const InteractivePropInstance& def, const InteractiveRuntimeState& rt)
	{
		if (!IsRotational(def.type)) return 0.0f;
		return def.openAngleDeg * Clamp01(rt.openFactor);
	}

	float ComputeSlideOffsetMeters(const InteractivePropInstance& def, const InteractiveRuntimeState& rt)
	{
		if (IsRotational(def.type)) return 0.0f;
		return def.openAngleDeg * Clamp01(rt.openFactor);
	}

	bool IsAnimating(const InteractiveRuntimeState& rt)
	{
		const float target = rt.targetState != 0u ? 1.0f : 0.0f;
		return rt.openFactor != target;
	}
}
