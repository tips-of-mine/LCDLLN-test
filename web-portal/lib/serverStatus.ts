// Présence "en ligne" des joueurs, lue en direct depuis le master.
//
// Le master expose une route HTTP `GET /online-accounts` sur son HealthEndpoint
// (port server.health.port, défaut 3842) renvoyant :
//   { "authenticated": [accountId, ...], "inWorld": [accountId, ...] }
//   - authenticated : session master active (login jeu réussi, éventuellement au menu).
//   - inWorld        : EnterWorld validé (réellement en jeu).
//
// Le portail interroge cette route côté serveur (jamais exposée au navigateur).
// L'URL de base vient de MASTER_STATUS_URL (ex. "http://127.0.0.1:3842").
//
// Dégradation gracieuse : si la variable est absente, l'URL invalide, le master
// injoignable ou la réponse malformée, on renvoie des ensembles VIDES sans lever
// d'erreur — la page admin reste fonctionnelle, seules les pastilles disparaissent.
// Pour rester diagnosticable malgré cette dégradation, on logge la raison de
// l'échec (logWarn) côté serveur : un admin qui ne voit aucune pastille peut
// regarder les logs portail pour distinguer "non configuré" / "injoignable".

import { logWarn } from "@/lib/log";

export interface OnlineAccounts {
  authenticated: Set<number>;
  inWorld: Set<number>;
}

const EMPTY: OnlineAccounts = { authenticated: new Set(), inWorld: new Set() };

function toIdSet(value: unknown): Set<number> {
  if (!Array.isArray(value)) return new Set();
  const out = new Set<number>();
  for (const item of value) {
    const id = typeof item === "number" ? item : Number(item);
    if (Number.isFinite(id) && id > 0) out.add(id);
  }
  return out;
}

/**
 * Récupère les comptes en ligne depuis le master. Ne lève jamais : en cas
 * d'échec (env absente, timeout, master KO, JSON invalide), renvoie des
 * ensembles vides. Timeout court (1,5 s) pour ne pas ralentir le rendu admin.
 */
export async function fetchOnlineAccounts(): Promise<OnlineAccounts> {
  const base = process.env.MASTER_STATUS_URL?.trim();
  if (!base) {
    // Variable optionnelle non définie : pas de pastille. On le signale une fois
    // par rendu pour qu'un admin surpris par l'absence de pastille comprenne.
    logWarn("serverStatus", "MASTER_STATUS_URL non défini : présence en ligne désactivée (aucune pastille).");
    return EMPTY;
  }

  const url = `${base.replace(/\/$/, "")}/online-accounts`;
  const controller = new AbortController();
  const timeout = setTimeout(() => controller.abort(), 1500);
  try {
    const response = await fetch(url, {
      cache: "no-store",
      signal: controller.signal,
    });
    if (!response.ok) {
      logWarn("serverStatus", "Master /online-accounts a répondu en erreur.", { url, status: response.status });
      return EMPTY;
    }
    const payload = (await response.json()) as { authenticated?: unknown; inWorld?: unknown };
    return {
      authenticated: toIdSet(payload.authenticated),
      inWorld: toIdSet(payload.inWorld),
    };
  } catch (err) {
    // Master injoignable / timeout / JSON invalide : on logge l'URL et l'erreur
    // pour distinguer un mauvais host (ex. 127.0.0.1 en conteneur) d'un master KO.
    logWarn("serverStatus", "Échec de la récupération de la présence en ligne.", { url, err });
    return EMPTY;
  } finally {
    clearTimeout(timeout);
  }
}

export type PlayerPresence = "in_world" | "authenticated" | "offline";

/** Statut de présence d'un compte à partir des ensembles renvoyés par le master. */
export function presenceFor(accountId: number, online: OnlineAccounts): PlayerPresence {
  if (online.inWorld.has(accountId)) return "in_world";
  if (online.authenticated.has(accountId)) return "authenticated";
  return "offline";
}
