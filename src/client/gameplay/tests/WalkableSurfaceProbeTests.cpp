// walkable_surface_probe_tests — v1.0.143 : un PNJ à altitude réelle (ville-tour) est posé
// sur la surface marchable sous son Y répliqué : la dalle d'un palier, la marche d'une
// volée, et pas sur le palier de l'étage du dessous ni sur un mur.
#include "src/client/gameplay/WalkableSurfaceProbe.h"
#include "src/client/gameplay/CompositeWorldCollider.h"

#include <cmath>
#include <cstdio>

using namespace engine::gameplay;

namespace
{
	int g_fail = 0;
	void Check(bool ok, const char* what)
	{
		if (!ok) { std::printf("FAIL: %s\n", what); ++g_fail; }
	}

	/// Dalle (ou marche) marchable de 2 × 2 m centrée en (cx, cz), dessus à \p top.
	PropBox Slab(float cx, float cz, float top, float thick = 0.35f)
	{
		PropBox b;
		b.cx = cx; b.cz = cz; b.halfX = 1.0f; b.halfZ = 1.0f;
		b.loY = top - thick; b.hiY = top;
		b.wall = false; b.walkableTop = true;
		return b;
	}
}

int main()
{
	CompositeWorldCollider c(nullptr);
	c.AddBox(Slab(0.0f, 0.0f, 100.0f));        // palier de l'étage
	c.AddBox(Slab(0.0f, 0.0f, 95.83f));        // palier de l'étage du dessous (même XZ)
	c.AddBox(Slab(5.0f, 0.0f, 101.2f, 0.2f));  // marche d'une volée
	PropBox wall;                              // mur (pas de dessus marchable)
	wall.cx = 10.0f; wall.cz = 0.0f; wall.halfX = 1.0f; wall.halfZ = 0.2f; wall.loY = 90.0f; wall.hiY = 104.0f;
	c.AddBox(wall);
	float y = 0.0f;
	Check(ProbeWalkableSurfaceBelow(c, 0.0f, 0.0f, 100.05f, 0.6f, 3.0f, y) && std::fabs(y - 100.0f) < 0.02f,
		"palier : le PNJ est pose sur la dalle de son etage");
	Check(ProbeWalkableSurfaceBelow(c, 0.0f, 0.0f, 99.7f, 0.6f, 3.0f, y) && std::fabs(y - 100.0f) < 0.02f,
		"palier : Y replique un peu bas, la dalle juste au-dessus est retenue");
	Check(ProbeWalkableSurfaceBelow(c, 5.0f, 0.0f, 100.9f, 0.6f, 3.0f, y) && std::fabs(y - 101.2f) < 0.02f,
		"volee : le PNJ est pose sur la marche");
	Check(!ProbeWalkableSurfaceBelow(c, 10.0f, 0.0f, 100.0f, 0.6f, 3.0f, y), "mur : pas de surface marchable");
	Check(!ProbeWalkableSurfaceBelow(c, 30.0f, 0.0f, 100.0f, 0.6f, 3.0f, y), "vide : rien sous le PNJ");
	if (g_fail == 0) std::printf("walkable_surface_probe_tests: OK\n");
	return g_fail == 0 ? 0 : 1;
}
