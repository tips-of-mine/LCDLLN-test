#pragma once

namespace engine::editor::world
{
	/// Source d'eau posée par l'utilisateur (M100.36). Position monde en mètres.
	/// L'altitude `worldY` est résolue par raycast au moment de la pose et
	/// conservée pour stabilité (la heightmap peut bouger entre la pose et la
	/// simulation watershed). Le simulateur ré-échantillonne quand même la
	/// cellule courante à `Run` pour gérer les modifs intermédiaires (sculpt
	/// après pose).
	struct SpringSource
	{
		float worldX = 0.0f;
		float worldZ = 0.0f;
		float worldY = 0.0f;
	};
}
