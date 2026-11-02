// ADM.5 — Monture et apparence (shard).
//
// /mount : la mécanique d'une monture — vitesse multipliée (×1,6 par défaut,
// cumulée avec /speed), état « monté » transmis au client (GmStateUpdate,
// drapeau kGmFlagMounted). Il n'existe pas encore de modèle 3D de monture ni
// d'objet « monture » : le personnage garde son apparence (chantier de contenu
// séparé, cf. docs/commandes/ADM.5_monture_apparence.md). /dismount = /mount off.

#include "src/shared/server_bootstrap/GmCommandExecutor.h"

#include <algorithm>

namespace engine::server
{
	void GmCommandExecutor::CmdMount()
	{
		// « [on|off|<vitesse 1-3>] [joueur] » ; sans argument : bascule.
		size_t targetIndex = 0;
		std::optional<bool> wanted;
		float multiplier = 0.0f;
		if (!args.empty())
		{
			if (const auto onOff = engine::admin::ParseOnOff(args[0]))
			{
				wanted = *onOff;
				targetIndex = 1;
			}
			else if (const auto v = engine::admin::ParseDouble(args[0]))
			{
				if (*v < 1.0 || *v > 3.0)
					return Usage("/mount [on|off|<vitesse 1 à 3>] [joueur]");
				wanted = true;
				multiplier = static_cast<float>(*v);
				targetIndex = 1;
			}
		}
		ConnectedClient* target = TargetOrSelf(targetIndex);
		if (target == nullptr)
			return;
		const bool on = wanted.value_or(!target->gm.mounted);
		target->gm.mounted = on;
		if (multiplier > 0.0f)
			target->gm.mountSpeedMultiplier = multiplier;
		else if (on && target->gm.mountSpeedMultiplier <= 0.0f)
			target->gm.mountSpeedMultiplier = gm::kGmDefaultMountMultiplier;
		ApplyGmState(*target);
		if (on)
		{
			Say(NameOf(*target) + " est monté (vitesse ×" + engine::admin::FormatFixed(target->gm.mountSpeedMultiplier, 1)
				+ ", vitesse maximale " + engine::admin::FormatFixed(target->maxMoveSpeed, 1) + " m/s).");
			NotifyTarget(*target, "Un maître de jeu vous a donné une monture.");
		}
		else
		{
			Say(NameOf(*target) + " met pied à terre.");
			NotifyTarget(*target, "Vous mettez pied à terre.");
		}
	}

	void GmCommandExecutor::CmdDismount()
	{
		args.insert(args.begin(), "off");
		CmdMount();
	}
}
