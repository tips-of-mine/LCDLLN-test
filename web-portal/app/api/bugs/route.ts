// POST /api/bugs
// Body: { title, body, category }
import { NextResponse } from 'next/server'
import { cookies } from 'next/headers'
import { query } from '@/lib/db/connection'
import { logError } from '@/lib/log'
import type { ResultSetHeader } from 'mysql2/promise'

const VALID_CATEGORIES = ['gameplay', 'graphique', 'reseau', 'interface', 'autre'] as const

export async function POST(request: Request) {
  const jar = cookies()
  const raw = jar.get('lcdlln_portal_account')?.value
  if (!raw) return NextResponse.json({ ok: false }, { status: 401 })
  const accountId = parseInt(raw, 10)
  if (isNaN(accountId)) return NextResponse.json({ ok: false }, { status: 401 })

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
    logError("POST /api/bugs", "Insert bug report failed", { err })
    const msg = err instanceof Error ? err.message : 'Erreur serveur'
    return NextResponse.json({ ok: false, message: msg }, { status: 500 })
  }
}
