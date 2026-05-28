// POST /api/admin/cgu/[id]/publish
// Publishes a draft CGU edition
import { NextResponse } from 'next/server'
import { requireAdmin } from '@/lib/auth/admin'
import { query } from '@/lib/db/connection'
import type { RowDataPacket } from 'mysql2/promise'
import { logError } from '@/lib/log'

export async function POST(
  _request: Request,
  { params }: { params: { id: string } }
) {
  if (!(await requireAdmin())) return NextResponse.json({ ok: false }, { status: 403 })

  const id = parseInt(params.id, 10)
  if (isNaN(id)) return NextResponse.json({ ok: false }, { status: 400 })

  try {
    const rows = await query<Array<RowDataPacket & { id: number; status: string }>>(
      'SELECT id, status FROM terms_editions WHERE id = ? LIMIT 1',
      [id]
    )
    const edition = rows[0] ?? null
    if (!edition) return NextResponse.json({ ok: false, message: 'Ã‰dition introuvable' }, { status: 404 })
    if (edition.status !== 'draft') {
      return NextResponse.json(
        { ok: false, message: 'Seules les Ã©ditions en brouillon peuvent Ãªtre publiÃ©es' },
        { status: 409 }
      )
    }

    await query(
      `UPDATE terms_editions SET status = 'published', published_at = NOW() WHERE id = ?`,
      [id]
    )

    return NextResponse.json({ ok: true })
  } catch (err) {
    logError('POST /api/admin/cgu/[id]/publish', 'Publish CGU edition failed', { err })
    return NextResponse.json({ ok: false, message: 'Erreur serveur' }, { status: 500 })
  }
}
