// Logger structuré minimal pour le web-portal.
//
// Trois niveaux : info / warn / error.
// Sortie : JSON one-line en production (parseable par stack ELK/Loki/Grafana),
// texte préfixé [context] en développement (lisible humainement).
// Sous-jacent : console.log / console.warn / console.error — préserve
// l'output Docker logs, journald, PM2, etc.
//
// Usage :
//   logError("POST /api/bugs", "Insert failed", { err });
//   logWarn("password-recovery", "SMTP non configuré, lien généré localement", { to });
//   logInfo("admin/players", "Account disabled by admin", { accountId, by });
//
// La meta `err` est traitée spécialement : si c'est un Error, on extrait
// message + stack + name pour que la sérialisation JSON soit utile.

type LogMeta = Record<string, unknown>;

function isProduction(): boolean {
  return process.env.NODE_ENV === "production";
}

function serializeError(err: unknown): unknown {
  if (err instanceof Error) {
    return {
      name: err.name,
      message: err.message,
      stack: err.stack,
    };
  }
  return err;
}

function normalizeMeta(meta: LogMeta | undefined): LogMeta | undefined {
  if (!meta) return undefined;
  const normalized: LogMeta = {};
  for (const [key, value] of Object.entries(meta)) {
    normalized[key] = key === "err" ? serializeError(value) : value;
  }
  return normalized;
}

function emit(
  level: "info" | "warn" | "error",
  context: string,
  message: string,
  meta: LogMeta | undefined
): void {
  const normalized = normalizeMeta(meta);
  if (isProduction()) {
    const payload = {
      ts: new Date().toISOString(),
      level,
      context,
      message,
      ...(normalized ?? {}),
    };
    const line = JSON.stringify(payload);
    if (level === "error") console.error(line);
    else if (level === "warn") console.warn(line);
    else console.log(line);
  } else {
    const prefix = `[${context}]`;
    if (normalized) {
      if (level === "error") console.error(prefix, message, normalized);
      else if (level === "warn") console.warn(prefix, message, normalized);
      else console.log(prefix, message, normalized);
    } else {
      if (level === "error") console.error(prefix, message);
      else if (level === "warn") console.warn(prefix, message);
      else console.log(prefix, message);
    }
  }
}

export function logInfo(context: string, message: string, meta?: LogMeta): void {
  emit("info", context, message, meta);
}

export function logWarn(context: string, message: string, meta?: LogMeta): void {
  emit("warn", context, message, meta);
}

export function logError(context: string, message: string, meta?: LogMeta): void {
  emit("error", context, message, meta);
}
