import { NextResponse } from 'next/server'
import { cookies } from 'next/headers'
import { query } from '@/lib/db/connection'
import { isStaff } from '@/lib/auth/roles'
import { sendAccountDisabled } from '@/lib/email/sender'
import type { RowDataPacket } from 'mysql2/promise'
import { logError, logWarn } from '@/lib/log'

async function checkAdmin() {
  const role = cookies().get('lcdlln_portal_role')?.value
  return isStaff(role)
}

export async function PATCH(req: Request, { params }: { params: { id: string } }) {
  if (!await checkAdmin()) return NextResponse.json({ ok: false }, { status: 403 })
  const id = parseInt(params.id, 10)
  if (isNaN(id)) return NextResponse.json({ ok: false }, { status: 400 })

  let body: { reason?: string }
  try {
    body = await req.json()
  } catch (err) {
    logError('PATCH /api/admin/players/[id]/disable', 'Invalid JSON body', { err })
    return NextResponse.json({ ok: false, message: 'Corps invalide' }, { status: 400 })
  }

  const reason = body.reason?.trim() ?? ''
  if (!reason) return NextResponse.json({ ok: false, message: 'Le motif est requis' }, { status: 400 })

  try {
    const rows = await query<Array<RowDataPacket & { email: string; login: string }>>(
      'SELECT email, login FROM accounts WHERE id = ? LIMIT 1',
      [id]
    )
    if (!rows[0]) return NextResponse.json({ ok: false, message: 'Compte introuvable' }, { status: 404 })

    await query(
      "UPDATE accounts SET account_status = 'disabled', disabled_reason = ? WHERE id = ?",
      [reason, id]
    )

    try {
      await sendAccountDisabled(rows[0].email, rows[0].login, reason)
    } catch (err) {
      // Email failure is non-fatal — account is already disabled
      logWarn('PATCH /api/admin/players/[id]/disable', 'Account-disabled email send failed', { err, accountId: id })
    }

    return NextResponse.json({ ok: true })
  } catch (err) {
    logError('PATCH /api/admin/players/[id]/disable', 'Disable account failed', { err })
    return NextResponse.json({ ok: false, message: 'Erreur serveur' }, { status: 500 })
  }
}
