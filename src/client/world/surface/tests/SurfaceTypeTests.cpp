// engine/world/surface/tests/SurfaceTypeTests.cpp
#include "engine/world/surface/SurfaceType.h"

#include <cstdio>
#include <cstring>

namespace
{
    int g_failed = 0;

    #define REQUIRE(cond) do { \
        if (!(cond)) { \
            std::fprintf(stderr, "[FAIL] %s:%d  %s\n", __FILE__, __LINE__, #cond); \
            ++g_failed; \
        } \
    } while (0)

    using engine::world::surface::SurfaceType;
    using engine::world::surface::ToString;
    using engine::world::surface::ParseSurfaceType;

    void Test_ToString_AllValues()
    {
        REQUIRE(ToString(SurfaceType::Dirt)         == "Dirt");
        REQUIRE(ToString(SurfaceType::Grass)        == "Grass");
        REQUIRE(ToString(SurfaceType::Mud)          == "Mud");
        REQUIRE(ToString(SurfaceType::Sand)         == "Sand");
        REQUIRE(ToString(SurfaceType::Rock)         == "Rock");
        REQUIRE(ToString(SurfaceType::Snow)         == "Snow");
        REQUIRE(ToString(SurfaceType::ShallowWater) == "ShallowWater");
        REQUIRE(ToString(SurfaceType::DeepWater)    == "DeepWater");
        REQUIRE(ToString(SurfaceType::LavaCooled)   == "LavaCooled");
        REQUIRE(ToString(SurfaceType::WheatField)   == "WheatField");
        REQUIRE(ToString(SurfaceType::CornField)    == "CornField");
        REQUIRE(ToString(SurfaceType::Road)         == "Road");
        REQUIRE(ToString(SurfaceType::Bridge)       == "Bridge");
    }

    void Test_ToString_OutOfRange()
    {
        REQUIRE(ToString(SurfaceType::_Count) == "_Invalid");
        REQUIRE(ToString(static_cast<SurfaceType>(999)) == "_Invalid");
    }

    void Test_ParseSurfaceType_AllValues()
    {
        SurfaceType out = SurfaceType::_Count;
        REQUIRE(ParseSurfaceType("Dirt", out)         && out == SurfaceType::Dirt);
        REQUIRE(ParseSurfaceType("Grass", out)        && out == SurfaceType::Grass);
        REQUIRE(ParseSurfaceType("Mud", out)          && out == SurfaceType::Mud);
        REQUIRE(ParseSurfaceType("Sand", out)         && out == SurfaceType::Sand);
        REQUIRE(ParseSurfaceType("Rock", out)         && out == SurfaceType::Rock);
        REQUIRE(ParseSurfaceType("Snow", out)         && out == SurfaceType::Snow);
        REQUIRE(ParseSurfaceType("ShallowWater", out) && out == SurfaceType::ShallowWater);
        REQUIRE(ParseSurfaceType("DeepWater", out)    && out == SurfaceType::DeepWater);
        REQUIRE(ParseSurfaceType("LavaCooled", out)   && out == SurfaceType::LavaCooled);
        REQUIRE(ParseSurfaceType("WheatField", out)   && out == SurfaceType::WheatField);
        REQUIRE(ParseSurfaceType("CornField", out)    && out == SurfaceType::CornField);
        REQUIRE(ParseSurfaceType("Road", out)         && out == SurfaceType::Road);
        REQUIRE(ParseSurfaceType("Bridge", out)       && out == SurfaceType::Bridge);
    }

    void Test_ParseSurfaceType_Unknown()
    {
        SurfaceType out = SurfaceType::Snow;  // sentinel non touchée
        REQUIRE(!ParseSurfaceType("Foobar", out));
        REQUIRE(out == SurfaceType::Snow);  // out inchangé
        REQUIRE(!ParseSurfaceType("", out));
        REQUIRE(out == SurfaceType::Snow);
    }

    void Test_EnumCount_Is13()
    {
        REQUIRE(static_cast<int>(SurfaceType::_Count) == 13);
    }
}

int main()
{
    Test_ToString_AllValues();
    Test_ToString_OutOfRange();
    Test_ParseSurfaceType_AllValues();
    Test_ParseSurfaceType_Unknown();
    Test_EnumCount_Is13();
    return g_failed;
}
