// POST /api/bugs
// Body: { title, body, category }
import { NextResponse } from 'next/server'
import { query } from '@/lib/db'
import { getAuthenticatedAccountId } from '@/lib/apiAuth'
import type { ResultSetHeader } from 'mysql2/promise'

const VALID_CATEGORIES = ['gameplay', 'graphique', 'reseau', 'interface', 'autre'] as const

export async function POST(request: Request) {
  const accountId = await getAuthenticatedAccountId()
  if (!accountId) return NextResponse.json({ ok: false }, { status: 401 })

  try {
    const body = await request.json() as { title?: string; body?: string; category?: string }
    const title = typeof body.title === 'string' ? body.title.trim() : ''
    const content = typeof body.body === 'string' ? body.body.trim() : ''
    const category = VALID_CATEGORIES.includes(body.category as typeof VALID_CATEGORIES[number])
      ? body.category!
      : 'autre'

    if (!title || !content) {
      return NextResponse.json({ ok: false, message: 'Titre et description requis' }, { status: 400 })
    }

    const result = await query<ResultSetHeader>(
      `INSERT INTO bug_reports (account_id, title, body, category) VALUES (?, ?, ?, ?)`,
      [accountId, title, content, category]
    )

    return NextResponse.json({ ok: true, id: result.insertId }, { status: 201 })
  } catch (err) {
    console.error('[POST /api/bugs]', err)
    return NextResponse.json({ ok: false, message: 'Erreur serveur' }, { status: 500 })
  }
}
