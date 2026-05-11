// Roles centralisés pour le web-portal.
// La table `accounts.role` (migration sql/migrations/0043_phase_1c_account_roles.sql)
// porte une enum a 4 valeurs : 'player', 'moderator', 'game_master',
// 'administrator'. Avant cette migration, c'etait un binaire 'player'/'admin'.
// Les helpers ci-dessous permettent au portal de continuer a fonctionner avec
// la granularite hierarchique (Player < Moderator < GameMaster < Administrator)
// tout en preservant l'ancienne logique "admin OK / player KO" via isStaff().

export const ACCOUNT_ROLES = ['player', 'moderator', 'game_master', 'administrator'] as const

export type AccountRole = (typeof ACCOUNT_ROLES)[number]

/// Convertit une valeur DB (ou cookie) potentiellement inconnue en AccountRole.
/// Retourne 'player' par defaut pour toute valeur non reconnue ou nulle.
export function normalizeRole(raw: string | null | undefined): AccountRole {
  if (!raw) return 'player'
  if ((ACCOUNT_ROLES as readonly string[]).includes(raw)) {
    return raw as AccountRole
  }
  return 'player'
}

/// Retourne true si le role donne dispose des privileges staff (admin-portal).
/// Tout role autre que 'player' est considere comme staff.
export function isStaff(role: AccountRole | string | null | undefined): boolean {
  const r = typeof role === 'string' ? normalizeRole(role) : 'player'
  return r !== 'player'
}

/// Helper inverse pour clarte de lecture.
export function isPlayer(role: AccountRole | string | null | undefined): boolean {
  return !isStaff(role)
}

/// Hierarchie pour comparaisons "au moins" :
/// player(0) < moderator(1) < game_master(2) < administrator(3).
export function roleLevel(role: AccountRole | string | null | undefined): number {
  const r = typeof role === 'string' ? normalizeRole(role) : 'player'
  switch (r) {
    case 'player':
      return 0
    case 'moderator':
      return 1
    case 'game_master':
      return 2
    case 'administrator':
      return 3
  }
}

/// Retourne true si \p role >= \p minimum dans la hierarchie.
export function hasAtLeast(role: AccountRole | string | null | undefined, minimum: AccountRole): boolean {
  return roleLevel(role) >= roleLevel(minimum)
}
