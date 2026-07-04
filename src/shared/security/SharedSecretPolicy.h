#pragma once
#include <string_view>

namespace engine::security
{
	/// Vrai si le secret partagé est vide ou égal à une valeur de développement connue
	/// (committée dans le repo) — donc impropre à la production.
	bool IsWeakSharedSecret(std::string_view secret);

	/// Vrai si l'opérateur a explicitement autorisé un secret de dev via
	/// la variable d'environnement LCDLLN_ALLOW_DEV_SECRET=1.
	bool DevSecretOverrideEnabled();
}
