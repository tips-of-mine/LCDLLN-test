#pragma once

// M100.20 — Paramètres de vent (miroir C++ du constant buffer GLSL WindParams).

namespace engine::world::wind
{
	struct WindParamsCpu
	{
		float directionX = 1.0f;       // direction xz normalisée
		float directionZ = 0.0f;
		float forceMps = 4.0f;
		float turbulenceFreq = 0.2f;
		float turbulenceAmp = 0.5f;
		float waveSpeed = 3.0f;
		float waveLengthMeters = 6.0f;
		float waveAmplitude = 0.3f;
		float timeSeconds = 0.0f;
	};
}
