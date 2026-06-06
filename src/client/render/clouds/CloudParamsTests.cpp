#include "src/client/render/clouds/CloudParams.h"

#include <cassert>
#include <cmath>
#include <cstdio>

using engine::render::CloudParams;

static bool Near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) < eps; }

// Lerp à t=0 rend a, à t=1 rend b, à t=0.5 rend la moyenne.
void TestLerpEndpointsAndMid()
{
	CloudParams a{};
	a.coverage = 0.0f; a.density = 0.0f; a.baseAltMeters = 100.0f; a.topAltMeters = 200.0f;
	CloudParams b{};
	b.coverage = 1.0f; b.density = 2.0f; b.baseAltMeters = 300.0f; b.topAltMeters = 600.0f;

	CloudParams at0 = CloudParams::Lerp(a, b, 0.0f);
	assert(Near(at0.coverage, 0.0f));
	assert(Near(at0.density, 0.0f));

	CloudParams at1 = CloudParams::Lerp(a, b, 1.0f);
	assert(Near(at1.coverage, 1.0f));
	assert(Near(at1.topAltMeters, 600.0f));

	CloudParams mid = CloudParams::Lerp(a, b, 0.5f);
	assert(Near(mid.coverage, 0.5f));
	assert(Near(mid.density, 1.0f));
	assert(Near(mid.baseAltMeters, 200.0f));
	assert(Near(mid.topAltMeters, 400.0f));
	std::puts("[OK] TestLerpEndpointsAndMid");
}

// t hors [0,1] est clampé.
void TestLerpClamps()
{
	CloudParams a{}; a.coverage = 0.2f;
	CloudParams b{}; b.coverage = 0.8f;
	CloudParams below = CloudParams::Lerp(a, b, -1.0f);
	CloudParams above = CloudParams::Lerp(a, b, 5.0f);
	assert(Near(below.coverage, 0.2f));
	assert(Near(above.coverage, 0.8f));
	std::puts("[OK] TestLerpClamps");
}

int main()
{
	TestLerpEndpointsAndMid();
	TestLerpClamps();
	std::puts("[ALL OK] CloudParamsTests");
	return 0;
}
