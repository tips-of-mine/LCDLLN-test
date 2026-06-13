// PATCH /api/admin/faq/[id] — update faq item fields
// DELETE /api/admin/faq/[id] — delete faq item
import { NextResponse } from 'next/server'
import { requireRole } from '@/lib/auth/admin'
import { query } from '@/lib/db/connection'
import { logError } from '@/lib/log'

export async function PATCH(
  request: Request,
  { params }: { params: { id: string } }
) {
  if (!(await requireRole('game_master'))) {
    return NextResponse.json({ error: 'Accès refusé' }, { status: 403 })
  }
  const id = parseInt(params.id, 10)
  if (isNaN(id) || id <= 0) {
    return NextResponse.json({ error: 'ID invalide' }, { status: 400 })
  }
  try {
    const body = await request.json() as Record<string, unknown>
    const fieldMap: Record<string, string> = {
      question: 'question',
      answer: 'answer',
      category: 'category',
      display_order: 'display_order',
      published: 'published',
    }
    const updates: string[] = []
    const values: unknown[] = []
    for (const [key, col] of Object.entries(fieldMap)) {
      if (key in body) {
        let val = body[key]
        if (key === 'published') {
          val = val ? 1 : 0
        }
        updates.push(`${col} = ?`)
        values.push(val)
      }
    }
    if (updates.length === 0) {
      return NextResponse.json({ error: 'Aucun champ à mettre à jour' }, { status: 400 })
    }
    values.push(id)
    await query(
      `UPDATE faq_items SET ${updates.join(', ')} WHERE id = ?`,
      values
    )
    return NextResponse.json({ ok: true })
  } catch (err) {
    logError('PATCH /api/admin/faq/[id]', 'Update FAQ item failed', { err })
    return NextResponse.json({ error: 'Erreur serveur' }, { status: 500 })
  }
}

export async function DELETE(
  _request: Request,
  { params }: { params: { id: string } }
) {
  if (!(await requireRole('game_master'))) {
    return NextResponse.json({ error: 'Accès refusé' }, { status: 403 })
  }
  const id = parseInt(params.id, 10)
  if (isNaN(id) || id <= 0) {
    return NextResponse.json({ error: 'ID invalide' }, { status: 400 })
  }
  try {
    await query('DELETE FROM faq_items WHERE id = ?', [id])
    return NextResponse.json({ ok: true })
  } catch (err) {
    logError('DELETE /api/admin/faq/[id]', 'Delete FAQ item failed', { err })
    return NextResponse.json({ error: 'Erreur serveur' }, { status: 500 })
  }
}
