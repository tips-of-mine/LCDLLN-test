import { getSession, type Session } from '@/lib/auth/session'
import { hasAtLeast, isStaff, type AccountRole } from '@/lib/auth/roles'

// Helper unifié pour les 14 routes /api/admin/*. Avant cette refactor,
// chaque route déclarait localement un `checkAdmin()` qui faisait
// confiance au cookie `lcdlln_portal_role` (cookie en clair, modifiable
// par DevTools/curl/proxy). N'importe quel joueur connecté pouvait poser
// `lcdlln_portal_role=SUPERADMIN` et bypasser l'autorisation.
//
// Maintenant `getSession()` re-lit le rôle depuis la table accounts via
// JOIN avec portal_sessions. Le cookie ne porte plus aucune autorité —
// modifier le cookie ne fait rien tant que la session DB ne donne pas
// le rôle staff.
//
// Retourne :
//   - null si non authentifié OU si rôle non-staff. Le caller doit
//     alors retourner NextResponse.json({...}, { status: 403 }).
//   - la Session complète sinon. Le caller a accès à session.accountId
//     pour audit log, etc.
export async function requireAdmin(): Promise<Session> {
  const session = await getSession()
  if (!session) return null
  if (!isStaff(session.role)) return null
  return session
}

// Variante granulaire : exige un rôle >= `minimum` dans la hiérarchie
// player(0) < moderator(1) < game_master(2) < administrator(3).
// Le rôle est re-lu en base par getSession() (le cookie ne porte aucune autorité).
// Durcir une route avec un `minimum` plus élevé ne peut que RESTREINDRE l'accès :
// les comptes staff hérités sont 'administrator' (migration 0043), donc aucun
// verrouillage des admins existants ; la granularité ne s'active que pour des
// comptes explicitement créés en 'moderator'/'game_master'.
//
// Retourne null si non authentifié OU rôle insuffisant (le caller renvoie alors
// un 403), la Session complète sinon.
export async function requireRole(minimum: AccountRole): Promise<Session> {
  const session = await getSession()
  if (!session) return null
  if (!hasAtLeast(session.role, minimum)) return null
  return session
}
