// GET /api/admin/roadmap — returns all roadmap items ordered by display_order
// POST /api/admin/roadmap — creates a new roadmap item
import { NextResponse } from 'next/server'
import { isAuthenticatedAdmin } from '@/lib/apiAuth'
import { query } from '@/lib/db'
import type { RowDataPacket } from 'mysql2/promise'

export async function GET() {
  if (!(await isAuthenticatedAdmin())) {
    return NextResponse.json({ error: 'Accès refusé' }, { status: 403 })
  }
  try {
    const items = await query<RowDataPacket[]>(
      'SELECT id, title, description, status, category, display_order FROM roadmap_items ORDER BY display_order ASC'
    )
    return NextResponse.json(items)
  } catch {
    return NextResponse.json({ error: 'Erreur serveur' }, { status: 500 })
  }
}

export async function POST(request: Request) {
  if (!(await isAuthenticatedAdmin())) {
    return NextResponse.json({ error: 'Accès refusé' }, { status: 403 })
  }
  try {
    const body = await request.json() as {
      title?: string
      description?: string
      status?: string
      category?: string
      display_order?: number
    }
    const { title, description, status, category, display_order } = body
    if (!title || typeof title !== 'string' || title.trim() === '') {
      return NextResponse.json({ error: 'Le titre est requis' }, { status: 400 })
    }
    const validStatuses = ['completed', 'in_progress', 'planned']
    if (!status || !validStatuses.includes(status)) {
      return NextResponse.json({ error: 'Statut invalide' }, { status: 400 })
    }
    // Default display_order to max + 1 if not provided
    let order = display_order
    if (order === undefined || order === null) {
      const rows = await query<RowDataPacket[]>(
        'SELECT COALESCE(MAX(display_order), 0) + 1 AS next_order FROM roadmap_items'
      )
      order = (rows[0] as { next_order: number }).next_order
    }
    await query(
      'INSERT INTO roadmap_items (title, description, status, category, display_order) VALUES (?, ?, ?, ?, ?)',
      [title.trim(), description ?? null, status, category ?? null, order]
    )
    return NextResponse.json({ ok: true }, { status: 201 })
  } catch {
    return NextResponse.json({ error: 'Erreur serveur' }, { status: 500 })
  }
}
