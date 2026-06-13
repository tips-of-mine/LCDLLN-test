// PATCH /api/admin/roadmap/[id] — update roadmap item fields
// DELETE /api/admin/roadmap/[id] — delete roadmap item
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
      title: 'title',
      description: 'description',
      status: 'status',
      category: 'category',
      display_order: 'display_order',
    }
    const validStatuses = ['completed', 'in_progress', 'planned']
    const updates: string[] = []
    const values: unknown[] = []
    for (const [key, col] of Object.entries(fieldMap)) {
      if (key in body) {
        if (key === 'status' && !validStatuses.includes(body[key] as string)) {
          return NextResponse.json({ error: 'Statut invalide' }, { status: 400 })
        }
        updates.push(`${col} = ?`)
        values.push(body[key])
      }
    }
    if (updates.length === 0) {
      return NextResponse.json({ error: 'Aucun champ à mettre à jour' }, { status: 400 })
    }
    values.push(id)
    await query(
      `UPDATE roadmap_items SET ${updates.join(', ')} WHERE id = ?`,
      values
    )
    return NextResponse.json({ ok: true })
  } catch (err) {
    logError('PATCH /api/admin/roadmap/[id]', 'Update roadmap item failed', { err })
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
    await query('DELETE FROM roadmap_items WHERE id = ?', [id])
    return NextResponse.json({ ok: true })
  } catch (err) {
    logError('DELETE /api/admin/roadmap/[id]', 'Delete roadmap item failed', { err })
    return NextResponse.json({ error: 'Erreur serveur' }, { status: 500 })
  }
}
