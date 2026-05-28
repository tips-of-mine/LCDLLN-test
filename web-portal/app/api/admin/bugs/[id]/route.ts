// PATCH /api/admin/bugs/[id]
// Body: { adminStatus, adminComment?, awardExploit?: boolean }
// Auth: requires staff role (moderator / game_master / administrator)
//
// Exploit award logic (when awardExploit === true && adminStatus === 'resolved'):
//  1. Count the number of already-awarded (resolved) bug reports for the reporter.
//  2. Find all bug_reports exploits the reporter hasn't unlocked yet.
//  3. Unlock any exploit whose threshold is now reached.
//  4. Set exploit_awarded = 1 on the bug report.

import { NextResponse } from 'next/server'
import { isStaff } from '@/lib/auth/roles'
import { cookies } from 'next/headers'
import { query } from '@/lib/db/connection'
import type { RowDataPacket } from 'mysql2/promise'
import { logError } from '@/lib/log'

function isAdmin(): boolean {
  const jar = cookies()
  return isStaff(jar.get('lcdlln_portal_role')?.value)
}

const VALID_STATUSES = ['pending', 'confirmed', 'in_progress', 'resolved', 'not_a_bug'] as const
type AdminStatus = (typeof VALID_STATUSES)[number]

type BugRow = RowDataPacket & {
  account_id: number
  exploit_awarded: number
  admin_status: AdminStatus
}

type ExploitRow = RowDataPacket & {
  id: number
  threshold_value: number
}

type CountRow = RowDataPacket & {
  cnt: number
}

export async function PATCH(
  request: Request,
  { params }: { params: { id: string } }
) {
  if (!isAdmin()) {
    return NextResponse.json({ error: 'AccÃ¨s refusÃ©' }, { status: 403 })
  }

  const id = parseInt(params.id, 10)
  if (isNaN(id) || id <= 0) {
    return NextResponse.json({ error: 'ID invalide' }, { status: 400 })
  }

  let body: Record<string, unknown>
  try {
    body = await request.json() as Record<string, unknown>
  } catch (err) {
    logError('PATCH /api/admin/bugs/[id]', 'Invalid JSON body', { err })
    return NextResponse.json({ error: 'Corps JSON invalide' }, { status: 400 })
  }

  const adminStatus = body.adminStatus as string | undefined
  if (!adminStatus || !VALID_STATUSES.includes(adminStatus as AdminStatus)) {
    return NextResponse.json({ error: 'adminStatus invalide' }, { status: 400 })
  }

  const adminComment = typeof body.adminComment === 'string' ? body.adminComment.trim() || null : null
  const awardExploit = body.awardExploit === true

  try {
    // Fetch the bug report
    const rows = await query<BugRow[]>(
      'SELECT account_id, exploit_awarded, admin_status FROM bug_reports WHERE id = ?',
      [id]
    )
    if (!rows || rows.length === 0) {
      return NextResponse.json({ error: 'Signalement introuvable' }, { status: 404 })
    }
    const bug = rows[0]

    let newExploitAwarded = bug.exploit_awarded

    // Handle exploit award: only if resolved, requested, and not already awarded
    if (awardExploit && adminStatus === 'resolved' && !bug.exploit_awarded) {
      const accountId = bug.account_id

      // Count total awarded (resolved) bugs for this account (including this one)
      const countRows = await query<CountRow[]>(
        `SELECT COUNT(*) AS cnt
         FROM bug_reports
         WHERE account_id = ? AND exploit_awarded = 1`,
        [accountId]
      )
      // +1 for the current bug being marked as awarded now
      const resolvedCount = (countRows[0]?.cnt ?? 0) + 1

      // Find exploits with metric_source = 'bug_reports' that the account hasn't unlocked yet
      // and whose threshold is now met
      const eligibleExploits = await query<ExploitRow[]>(
        `SELECT e.id, e.threshold_value
         FROM exploits e
         WHERE e.metric_source = 'bug_reports'
           AND e.is_active = 1
           AND e.threshold_value <= ?
           AND NOT EXISTS (
             SELECT 1 FROM account_exploit_unlocks u
             WHERE u.account_id = ? AND u.exploit_id = e.id
           )
         ORDER BY e.threshold_value ASC`,
        [resolvedCount, accountId]
      )

      // Unlock each newly eligible exploit
      for (const exploit of eligibleExploits) {
        await query(
          `INSERT IGNORE INTO account_exploit_unlocks (account_id, exploit_id, source_detail)
           VALUES (?, ?, ?)`,
          [accountId, exploit.id, `bug_report_id:${id}`]
        )
      }

      newExploitAwarded = 1
    }

    // Update the bug report
    await query(
      `UPDATE bug_reports
       SET admin_status = ?, admin_comment = ?, exploit_awarded = ?
       WHERE id = ?`,
      [adminStatus, adminComment, newExploitAwarded, id]
    )

    return NextResponse.json({ ok: true })
  } catch (err) {
    logError('PATCH /api/admin/bugs/[id]', 'Bug update / exploit award failed', { err })
    return NextResponse.json({ error: 'Erreur serveur' }, { status: 500 })
  }
}
