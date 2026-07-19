#pragma once
// MasterJobScheduler — registre MINIMAL de jobs périodiques du master
// (Roadmap-4, 2026-07-19). Remplace les blocs « if (now - last >= interval) »
// écrits à la main dans la boucle principale : un job = un nom + un
// intervalle + un callable. Header-only, mono-thread (la boucle principale
// appelle Tick() ; les jobs longs gèrent eux-mêmes leur asynchronisme, ex.
// BirthdayEmailJob envoie le SMTP sur un thread détaché).
//
// Journalisation : liste des jobs au boot (LogRegistered), LOG_DEBUG à
// chaque exécution avec la durée — un job qui dérape se voit dans les logs.
// Premier passage : chaque job s'exécute dès le premier Tick() (utile pour
// les jobs « rattrapage au boot » comme l'e-mail d'anniversaire).

#include "src/shared/core/Log.h"

#include <chrono>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace engine::server
{
	class MasterJobScheduler
	{
	public:
		using JobFn = std::function<void()>;

		/// Enregistre un job périodique. \param name identifiant de log
		/// (snake_case). \param interval période minimale entre deux
		/// exécutions. \param fn exécuté sur le thread de la boucle
		/// principale — doit rester court (déléguer le lent à un thread).
		void Register(std::string name, std::chrono::seconds interval, JobFn fn)
		{
			m_jobs.push_back(Job{ std::move(name), interval,
				std::chrono::steady_clock::time_point{}, std::move(fn) });
		}

		/// Journalise la liste des jobs enregistrés (appeler une fois au boot).
		void LogRegistered() const
		{
			for (const Job& job : m_jobs)
			{
				LOG_INFO(Net, "[MasterJobScheduler] job '{}' (période {} s)",
					job.name, static_cast<long long>(job.interval.count()));
			}
		}

		/// Exécute les jobs dus (appelé à chaque itération de la boucle
		/// principale ; la cadence effective est bornée par les intervalles).
		/// Effets de bord : ceux des jobs + logs de durée.
		void Tick()
		{
			const auto now = std::chrono::steady_clock::now();
			for (Job& job : m_jobs)
			{
				if (job.lastRun != std::chrono::steady_clock::time_point{}
					&& now - job.lastRun < job.interval)
				{
					continue;
				}
				job.lastRun = now;
				const auto t0 = std::chrono::steady_clock::now();
				job.fn();
				const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
					std::chrono::steady_clock::now() - t0).count();
				LOG_DEBUG(Net, "[MasterJobScheduler] job '{}' exécuté en {} ms",
					job.name, static_cast<long long>(ms));
			}
		}

		size_t Size() const { return m_jobs.size(); }

	private:
		struct Job
		{
			std::string name;
			std::chrono::seconds interval{ 0 };
			std::chrono::steady_clock::time_point lastRun{};
			JobFn fn;
		};
		std::vector<Job> m_jobs;
	};
}
