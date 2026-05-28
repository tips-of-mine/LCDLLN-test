import { cookies } from 'next/headers'
import { randomBytes } from 'node:crypto'
import type { ResultSetHeader, RowDataPacket } from 'mysql2/promise'
import { query } from '@/lib/db/connection'
import { normalizeRole, type AccountRole } from '@/lib/auth/roles'

// Nom du seul cookie d'authentification. Contient un session_token random
// (64 chars hex). Aucune information identifiante ou autoritative côté
// client — toute la matérialisation de l'identité passe par une lecture
// de la table portal_sessions à chaque appel de getSession().
export const SESSION_COOKIE_NAME = 'lcdlln_portal_session'

// Durée de vie maximale d'une session (login → expiration absolue). Le
// rolling refresh (last_seen_at via ON UPDATE CURRENT_TIMESTAMP) ne
// repousse PAS expires_at — c'est volontaire pour limiter les sessions
// éternellement actives sur une machine partagée.
export const SESSION_MAX_AGE_SEC = 60 * 60 * 24 * 7 // 7 jours

export type Session = {
  accountId: number
  role: AccountRole
  tagId: string | null
  login: string
} | null

// Génère un session_token de 256 bits (32 octets → 64 chars hex). C'est la
// SEULE source d'autorité côté client après cette refactor.
export function generateSessionToken(): string {
  return randomBytes(32).toString('hex')
}

// Insère une nouvelle session pour `accountId` et retourne le token à
// poser en cookie. Appelée par /api/auth/login après vérification des
// credentials.
export async function createSession(
  accountId: number,
  metadata?: { userAgent?: string | null; ip?: string | null }
): Promise<string> {
  const token = generateSessionToken()
  const expiresAt = new Date(Date.now() + SESSION_MAX_AGE_SEC * 1000)
  await query<ResultSetHeader>(
    `INSERT INTO portal_sessions (session_token, account_id, expires_at, user_agent, ip)
     VALUES (?, ?, ?, ?, ?)`,
    [token, accountId, expiresAt, metadata?.userAgent ?? null, metadata?.ip ?? null]
  )
  return token
}

// Supprime la session associée à `token`. Appelée par /api/auth/logout.
// No-op silencieux si le token n'existe pas (ex. déjà expiré ou clic
// double).
export async function deleteSession(token: string): Promise<void> {
  await query<ResultSetHeader>(
    'DELETE FROM portal_sessions WHERE session_token = ?',
    [token]
  )
}

// Lecture-écriture : matérialise la session côté serveur. Lit le cookie,
// fait un JOIN portal_sessions + accounts pour récupérer l'identité réelle
// (account_id, role) depuis la DB. Met à jour last_seen_at par effet de
// bord (ON UPDATE CURRENT_TIMESTAMP sur la colonne).
//
// Retourne null dans tous les cas non-authentifiés :
//   - cookie absent
//   - token de format invalide (non-hex / longueur != 64)
//   - token non trouvé en DB (déconnecté, supprimé, ou jamais émis)
//   - session expirée (expires_at < now)
//   - compte associé supprimé (FK CASCADE retire la session, mais filet
//     de sécurité au cas où)
//
// Quiconque modifie le cookie côté client tombe dans la branche "token
// non trouvé" — il est cryptographiquement impossible de fabriquer un
// session_token valide sans passer par createSession().
export async function getSession(): Promise<Session> {
  const jar = cookies()
  const token = jar.get(SESSION_COOKIE_NAME)?.value
  if (!token) return null
  // Format-check rapide : on filtre les tokens qui ne respectent pas la
  // forme hex 64-chars avant de toucher à la DB. Évite de polluer le
  // pool MySQL avec des requêtes garanties non matching et bloque les
  // tentatives d'injection ou de probing de format.
  if (!/^[0-9a-f]{64}$/.test(token)) return null
  try {
    const rows = await query<Array<RowDataPacket & {
      id: number
      login: string
      role: string
      tag_id: string | null
      expires_at: string
    }>>(
      `SELECT a.id, a.login, a.role, a.tag_id, s.expires_at
       FROM portal_sessions s
       INNER JOIN accounts a ON a.id = s.account_id
       WHERE s.session_token = ?
       LIMIT 1`,
      [token]
    )
    const row = rows[0]
    if (!row) return null
    if (new Date(row.expires_at).getTime() <= Date.now()) {
      // Session expirée : nettoyage opportuniste (best-effort, on
      // n'attend pas le résultat pour ne pas bloquer la réponse).
      void deleteSession(token).catch(() => undefined)
      return null
    }
    // Rolling touch : met à jour last_seen_at sans recalculer
    // expires_at. Un UPDATE séparé serait redondant avec ON UPDATE
    // CURRENT_TIMESTAMP qui se déclenche sur tout UPDATE — on lance
    // un UPDATE explicite minimal pour matérialiser l'activité.
    void query<ResultSetHeader>(
      'UPDATE portal_sessions SET last_seen_at = CURRENT_TIMESTAMP WHERE session_token = ?',
      [token]
    ).catch(() => undefined)
    return {
      accountId: row.id,
      role: normalizeRole(row.role),
      tagId: row.tag_id ?? null,
      login: row.login,
    }
  } catch {
    return null
  }
}

// Helper rare : retourne le token brut du cookie (utile pour le logout
// qui doit DELETE la bonne ligne). Pas exposé aux routes consommatrices.
export function getRawSessionToken(): string | null {
  const jar = cookies()
  return jar.get(SESSION_COOKIE_NAME)?.value ?? null
}
