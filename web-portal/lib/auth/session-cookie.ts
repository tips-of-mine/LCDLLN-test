// Constantes du cookie de session — extraites de session.ts pour pouvoir être
// importées depuis le middleware sans tirer les dépendances Node.js
// (mysql2, node:crypto) qui ne sont pas disponibles en Edge Runtime.
//
// Le middleware Next.js tourne sur l'Edge Runtime (V8 isolate, pas Node.js)
// — il a accès aux Web APIs mais PAS aux modules `node:*` ni à `mysql2`.
// Si on importe `SESSION_COOKIE_NAME` depuis `session.ts`, webpack bundle
// tout `session.ts` (incluant ses imports transitifs) pour Edge → fail
// "Unhandled scheme 'node:'". En isolant les constantes dans ce module
// pur, le middleware reste léger et bundle-friendly.

export const SESSION_COOKIE_NAME = 'lcdlln_portal_session'

// Durée de vie maximale d'une session (login → expiration absolue). Le
// rolling refresh de last_seen_at ne repousse PAS expires_at — c'est
// volontaire pour limiter les sessions éternellement actives sur une
// machine partagée.
export const SESSION_MAX_AGE_SEC = 60 * 60 * 24 * 7 // 7 jours
