// POST /api/player/cgu/accept
// Body: { editionId: number }
import { NextResponse } from 'next/server'
import { cookies } from 'next/headers'
import { query } from '@/lib/db'
import type { RowDataPacket } from 'mysql2/promise'

export async function POST(request: Request) {
  const jar = cookies()
  const raw = jar.get('lcdlln_portal_account')?.value
  if (!raw) return NextResponse.json({ ok: false }, { status: 401 })
  const accountId = parseInt(raw, 10)
  if (isNaN(accountId)) return NextResponse.json({ ok: false }, { status: 401 })

  try {
    const body = await request.json() as { editionId?: number }
    const editionId = Number(body.editionId)
    if (!editionId || isNaN(editionId)) {
      return NextResponse.json({ ok: false, message: 'Edition invalide' }, { status: 400 })
    }

    // Verify edition exists and is published
    const editions = await query<Array<RowDataPacket & { id: number }>>(
      'SELECT id FROM terms_editions WHERE id = ? AND status = ? LIMIT 1',
      [editionId, 'published']
    )
    if (!editions[0]) return NextResponse.json({ ok: false, message: 'CGU introuvable' }, { status: 404 })

    const ip = (request.headers.get('x-forwarded-for') ?? '').split(',')[0].trim() || null
    const ua = request.headers.get('user-agent') ?? null

    await query(
      `INSERT INTO account_terms_acceptances (account_id, edition_id, accepted_at, ip_address, user_agent)
       VALUES (?, ?, NOW(), ?, ?)
       ON DUPLICATE KEY UPDATE accepted_at = NOW(), ip_address = VALUES(ip_address), user_agent = VALUES(user_agent)`,
      [accountId, editionId, ip, ua]
    )

    return NextResponse.json({ ok: true })
  } catch {
    return NextResponse.json({ ok: false, message: 'Erreur serveur' }, { status: 500 })
  }
}
