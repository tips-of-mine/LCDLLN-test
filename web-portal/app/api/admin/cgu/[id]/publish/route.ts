// POST /api/admin/cgu/[id]/publish
// Publishes a draft CGU edition
import { NextResponse } from 'next/server'
import { isAuthenticatedAdmin } from '@/lib/apiAuth'
import { query } from '@/lib/db'
import type { RowDataPacket } from 'mysql2/promise'

export async function POST(
  _request: Request,
  { params }: { params: { id: string } }
) {
  if (!(await isAuthenticatedAdmin())) {
    return NextResponse.json({ ok: false }, { status: 403 })
  }

  const id = parseInt(params.id, 10)
  if (isNaN(id)) return NextResponse.json({ ok: false }, { status: 400 })

  try {
    const rows = await query<Array<RowDataPacket & { id: number; status: string }>>(
      'SELECT id, status FROM terms_editions WHERE id = ? LIMIT 1',
      [id]
    )
    const edition = rows[0] ?? null
    if (!edition) return NextResponse.json({ ok: false, message: 'Édition introuvable' }, { status: 404 })
    if (edition.status !== 'draft') {
      return NextResponse.json(
        { ok: false, message: 'Seules les éditions en brouillon peuvent être publiées' },
        { status: 409 }
      )
    }

    await query(
      `UPDATE terms_editions SET status = 'published', published_at = NOW() WHERE id = ?`,
      [id]
    )

    return NextResponse.json({ ok: true })
  } catch {
    return NextResponse.json({ ok: false, message: 'Erreur serveur' }, { status: 500 })
  }
}
