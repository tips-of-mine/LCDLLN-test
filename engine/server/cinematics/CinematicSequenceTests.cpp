#include "engine/server/cinematics/CinematicSequence.h"
#include "engine/core/Log.h"

#include <cmath>

namespace
{
	using engine::server::cinematics::CameraKeyframe;
	using engine::server::cinematics::CinematicSequence;
	using engine::server::cinematics::InterpolatedFrame;
	using engine::server::cinematics::SampleAt;

	bool ApproxEq(float a, float b, float eps = 1e-3f)
	{ return std::fabs(a - b) <= eps; }

	bool TestEmpty()
	{
		CinematicSequence s;
		InterpolatedFrame f;
		if (SampleAt(s, 0, f)) return false;
		s.keyframes.push_back({0, 0,0,0, 1,0,0, ""});
		if (SampleAt(s, 0, f)) return false;  // need 2 keyframes
		LOG_INFO(Core, "[CinematicTests] empty/single OK");
		return true;
	}

	bool TestInterpolation()
	{
		CinematicSequence s;
		s.keyframes.push_back({0, 0,0,0, 1,0,0, ""});
		s.keyframes.push_back({1000, 10,5,0, 0,1,0, ""});

		InterpolatedFrame f;
		if (!SampleAt(s, 500, f)) return false;
		if (!ApproxEq(f.posX, 5) || !ApproxEq(f.posY, 2.5f)) return false;
		LOG_INFO(Core, "[CinematicTests] linear interp OK");
		return true;
	}

	bool TestOutOfRange()
	{
		CinematicSequence s;
		s.keyframes.push_back({100, 0,0,0, 0,0,0, ""});
		s.keyframes.push_back({200, 1,1,1, 0,0,0, ""});

		InterpolatedFrame f;
		if (SampleAt(s, 50, f)) return false;   // before
		if (SampleAt(s, 300, f)) return false;  // after
		LOG_INFO(Core, "[CinematicTests] out of range OK");
		return true;
	}
}

int main(int argc, char** argv)
{
	(void)argc; (void)argv;
	engine::core::LogSettings logSettings;
	logSettings.level = engine::core::LogLevel::Info;
	logSettings.console = true;
	engine::core::Log::Init(logSettings);

	const bool ok = TestEmpty() && TestInterpolation() && TestOutOfRange();
	if (ok) LOG_INFO(Core, "[CinematicTests] ALL OK");
	else LOG_ERROR(Core, "[CinematicTests] FAIL");
	engine::core::Log::Shutdown();
	return ok ? 0 : 1;
}
