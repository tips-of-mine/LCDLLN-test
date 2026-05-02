// POST /api/admin/cgu/[id]/retire
// Body: { reason: string } — required, non-empty
// Retires a published CGU edition
import { NextResponse } from 'next/server'
import { isAuthenticatedAdmin } from '@/lib/apiAuth'
import { query } from '@/lib/db'
import type { RowDataPacket } from 'mysql2/promise'

export async function POST(
  request: Request,
  { params }: { params: { id: string } }
) {
  if (!(await isAuthenticatedAdmin())) {
    return NextResponse.json({ ok: false }, { status: 403 })
  }

  const id = parseInt(params.id, 10)
  if (isNaN(id)) return NextResponse.json({ ok: false }, { status: 400 })

  try {
    const body = await request.json() as { reason?: string }
    const reason = typeof body.reason === 'string' ? body.reason.trim() : ''
    if (!reason) {
      return NextResponse.json(
        { ok: false, message: 'Le motif de retrait est requis' },
        { status: 400 }
      )
    }

    const rows = await query<Array<RowDataPacket & { id: number; status: string }>>(
      'SELECT id, status FROM terms_editions WHERE id = ? LIMIT 1',
      [id]
    )
    const edition = rows[0] ?? null
    if (!edition) return NextResponse.json({ ok: false, message: 'Édition introuvable' }, { status: 404 })
    if (edition.status !== 'published') {
      return NextResponse.json(
        { ok: false, message: 'Seules les éditions publiées peuvent être retirées' },
        { status: 409 }
      )
    }

    await query(
      `UPDATE terms_editions SET status = 'retired', retired_reason = ? WHERE id = ?`,
      [reason, id]
    )

    return NextResponse.json({ ok: true })
  } catch {
    return NextResponse.json({ ok: false, message: 'Erreur serveur' }, { status: 500 })
  }
}
