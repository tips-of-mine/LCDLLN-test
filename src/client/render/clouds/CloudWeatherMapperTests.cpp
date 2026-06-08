#include "src/client/render/clouds/CloudWeatherMapper.h"
#include "src/client/render/WeatherSystem.h"

#include <cassert>
#include <cstdio>

using engine::render::CloudParams;
using engine::render::CloudWeatherMapper;
using engine::render::WeatherState;

// Clear -> couverture faible ; Storm -> couverture quasi pleine + dense + sombre.
void TestKindExtremes()
{
	CloudParams clear = CloudWeatherMapper::ParamsFor(WeatherState::Clear);
	CloudParams storm = CloudWeatherMapper::ParamsFor(WeatherState::Storm);

	assert(clear.coverage < 0.35f);
	assert(storm.coverage > 0.85f);
	assert(storm.density  > clear.density);
	assert(storm.tintR    < clear.tintR); // Storm plus sombre que Clear.
	std::puts("[OK] TestKindExtremes");
}

// Tous les états retournent des params bornés et cohérents (base < top).
void TestAllKindsSane()
{
	const WeatherState all[] = { WeatherState::Clear, WeatherState::Rain,
		WeatherState::Snow, WeatherState::Fog, WeatherState::Storm };
	for (WeatherState s : all)
	{
		CloudParams p = CloudWeatherMapper::ParamsFor(s);
		assert(p.coverage >= 0.0f && p.coverage <= 1.0f);
		assert(p.density  >= 0.0f);
		assert(p.baseAltMeters < p.topAltMeters);
	}
	std::puts("[OK] TestAllKindsSane");
}

int main()
{
	TestKindExtremes();
	TestAllKindsSane();
	std::puts("[ALL OK] CloudWeatherMapperTests");
	return 0;
}
