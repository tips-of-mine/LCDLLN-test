// GET /api/admin/faq — returns all faq items ordered by display_order
// POST /api/admin/faq — creates a new faq item
import { NextResponse } from 'next/server'
import { cookies } from 'next/headers'
import { query } from '@/lib/db/connection'
import type { RowDataPacket } from 'mysql2/promise'

function isAdmin(): boolean {
  const jar = cookies()
  return jar.get('lcdlln_portal_role')?.value === 'admin'
}

export async function GET() {
  if (!isAdmin()) {
    return NextResponse.json({ error: 'Accès refusé' }, { status: 403 })
  }
  try {
    const items = await query<RowDataPacket[]>(
      'SELECT id, question, answer, category, display_order, published FROM faq_items ORDER BY display_order ASC, id ASC'
    )
    return NextResponse.json(items)
  } catch {
    return NextResponse.json({ error: 'Erreur serveur' }, { status: 500 })
  }
}

export async function POST(request: Request) {
  if (!isAdmin()) {
    return NextResponse.json({ error: 'Accès refusé' }, { status: 403 })
  }
  try {
    const body = await request.json() as {
      question?: string
      answer?: string
      category?: string
      displayOrder?: number
      published?: boolean
    }
    const { question, answer, category, displayOrder, published } = body
    if (!question || typeof question !== 'string' || question.trim() === '') {
      return NextResponse.json({ error: 'La question est requise' }, { status: 400 })
    }
    if (!answer || typeof answer !== 'string' || answer.trim() === '') {
      return NextResponse.json({ error: 'La réponse est requise' }, { status: 400 })
    }
    // Default display_order to max + 1 if not provided
    let order = displayOrder
    if (order === undefined || order === null) {
      const rows = await query<RowDataPacket[]>(
        'SELECT COALESCE(MAX(display_order), 0) + 1 AS next_order FROM faq_items'
      )
      order = (rows[0] as { next_order: number }).next_order
    }
    await query(
      'INSERT INTO faq_items (question, answer, category, display_order, published) VALUES (?, ?, ?, ?, ?)',
      [question.trim(), answer.trim(), category ?? null, order, published ? 1 : 0]
    )
    return NextResponse.json({ ok: true }, { status: 201 })
  } catch {
    return NextResponse.json({ error: 'Erreur serveur' }, { status: 500 })
  }
}
