#include "src/shared/network/WorldClockPayloads.h"
#include <cstdio>
#include <cmath>
using namespace engine::network::worldclock;
static int g_failed=0;
#define CHECK(c) do{ if(!(c)){ std::fprintf(stderr,"[FAIL] %s:%d %s\n",__FILE__,__LINE__,#c); ++g_failed; } }while(0)

int main()
{
    WorldClockStateResponse r;
    r.status = WorldClockStatus::Ok;
    r.serverTimeUnixMs = 123456789ull;
    r.epochRefUnixMs   = 1767225600000ull;
    r.timeScaleRealMinPerDay = 60.0f;
    r.offsetGameSec    = 43200.0;
    r.paused           = 1;
    r.pausedAtGameSec  = 7200.0;
    r.lunarPeriodGameSec = 16.0*86400.0;

    std::vector<uint8_t> buf;
    BuildWorldClockStateResponsePayload(r, buf);
    CHECK(buf.size() == 46);
    WorldClockStateResponse out;
    CHECK(ParseWorldClockStateResponsePayload(buf.data(), buf.size(), out));
    CHECK(out.status == WorldClockStatus::Ok);
    CHECK(out.serverTimeUnixMs == r.serverTimeUnixMs);
    CHECK(out.epochRefUnixMs == r.epochRefUnixMs);
    CHECK(std::fabs(out.offsetGameSec - r.offsetGameSec) < 1e-6);
    CHECK(out.paused == 1);
    CHECK(std::fabs(out.pausedAtGameSec - 7200.0) < 1e-6);
    CHECK(std::fabs(out.lunarPeriodGameSec - r.lunarPeriodGameSec) < 1e-3);

    // Reject-short : un payload tronque d'un octet doit etre refuse.
    CHECK(!ParseWorldClockStateResponsePayload(buf.data(), buf.size()-1, out));
    // Reject-extra : un octet de trop doit aussi etre refuse (taille != 46).
    {
        std::vector<uint8_t> extra = buf;
        extra.push_back(0u);
        CHECK(!ParseWorldClockStateResponsePayload(extra.data(), extra.size(), out));
    }

    std::vector<uint8_t> req;
    BuildWorldClockStateRequestPayload(req);
    CHECK(req.empty());
    WorldClockStateRequest rq;
    CHECK(ParseWorldClockStateRequestPayload(req.data(), req.size(), rq));

    std::vector<uint8_t> nbuf;
    BuildWorldClockChangeNotificationPayload(r, nbuf);
    WorldClockStateResponse n;
    CHECK(ParseWorldClockChangeNotificationPayload(nbuf.data(), nbuf.size(), n));
    CHECK(std::fabs(n.timeScaleRealMinPerDay - 60.0f) < 1e-3f);

    if(g_failed==0) std::printf("[OK] WorldClockPayloadsTests\n");
    return g_failed;
}
