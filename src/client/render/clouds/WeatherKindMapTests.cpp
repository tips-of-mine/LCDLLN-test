#include "src/client/render/clouds/WeatherKindMap.h"
#include "src/client/render/WeatherSystem.h"

#include <cassert>
#include <cstdio>

using engine::render::WeatherState;
using engine::render::MapServerKindToWeatherState;

// Le wire serveur : Clear=0,Rain=1,Snow=2,Storm=3,Sandstorm=4,Fog=5.
// Le client     : Clear=0,Rain=1,Snow=2,Fog=3,Storm=4 (indices DIFFERENTS).
void TestServerKindMapping()
{
    assert(MapServerKindToWeatherState(0) == WeatherState::Clear);
    assert(MapServerKindToWeatherState(1) == WeatherState::Rain);
    assert(MapServerKindToWeatherState(2) == WeatherState::Snow);
    assert(MapServerKindToWeatherState(3) == WeatherState::Storm); // serveur Storm=3
    assert(MapServerKindToWeatherState(4) == WeatherState::Storm); // Sandstorm -> Storm (repli)
    assert(MapServerKindToWeatherState(5) == WeatherState::Fog);   // serveur Fog=5
    assert(MapServerKindToWeatherState(99) == WeatherState::Clear);// inconnu -> Clear
    std::puts("[OK] TestServerKindMapping");
}

int main()
{
    TestServerKindMapping();
    std::puts("[ALL OK] WeatherKindMapTests");
    return 0;
}
