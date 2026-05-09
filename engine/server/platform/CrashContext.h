#pragma once
// CMANGOS.37 (Phase 4.37a) — CrashContext : metadata enregistree par le
// serveur (build hash, uptime, derniere tick, derniere zone, etc.) au moment
// d'une signal de crash, pour produire un crash report structure pre-dump.
// La plomberie minidump/coredump native (signal handler, SetUnhandledFilter)
// est deferree — c'est le format texte du contexte que ce module valide.
// Header-only.

#include <cstdint>
#include <string>
#include <sstream>

namespace engine::server::platform
{
	struct CrashContext
	{
		std::string buildHash;
		uint64_t    uptimeMs   = 0;
		uint64_t    lastTickMs = 0;
		std::string lastZone;
		uint32_t    activeSessions = 0;
		std::string signalName;     ///< "SIGSEGV", "SIGABRT", "Unhandled exception", etc.
	};

	/// Serialize en texte multi-ligne (key: value). Format simple pour
	/// piper dans un fichier disque ou un log avant que le process meurt.
	inline std::string Format(const CrashContext& c)
	{
		std::ostringstream os;
		os << "buildHash: "      << c.buildHash      << "\n";
		os << "uptimeMs: "       << c.uptimeMs       << "\n";
		os << "lastTickMs: "     << c.lastTickMs     << "\n";
		os << "lastZone: "       << c.lastZone       << "\n";
		os << "activeSessions: " << c.activeSessions << "\n";
		os << "signal: "         << c.signalName     << "\n";
		return os.str();
	}

	/// Verifie que le contexte contient le minimum acceptable avant d'ecrire
	/// le rapport (eviter de polluer le disque avec des entrees vides apres
	/// un crash precoce ou.le buildHash n'a pas eu le temps d'etre rempli).
	inline bool IsValid(const CrashContext& c)
	{
		return !c.buildHash.empty() && !c.signalName.empty();
	}
}
