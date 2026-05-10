// PATCH /api/admin/cgu/[id] — update draft edition
// DELETE /api/admin/cgu/[id] — delete draft edition
import { NextResponse } from 'next/server'
import { cookies } from 'next/headers'
import { query } from '@/lib/db/connection'
import { isStaff } from '@/lib/auth/roles'
import type { RowDataPacket } from 'mysql2/promise'

function isAdmin(): boolean {
  const jar = cookies()
  return isStaff(jar.get('lcdlln_portal_role')?.value)
}

async function getEdition(id: number) {
  const rows = await query<Array<RowDataPacket & { id: number; status: string }>>(
    'SELECT id, status FROM terms_editions WHERE id = ? LIMIT 1',
    [id]
  )
  return rows[0] ?? null
}

export async function PATCH(
  request: Request,
  { params }: { params: { id: string } }
) {
  if (!isAdmin()) return NextResponse.json({ ok: false }, { status: 403 })

  const id = parseInt(params.id, 10)
  if (isNaN(id)) return NextResponse.json({ ok: false }, { status: 400 })

  try {
    const edition = await getEdition(id)
    if (!edition) return NextResponse.json({ ok: false, message: 'Édition introuvable' }, { status: 404 })
    if (edition.status !== 'draft') {
      return NextResponse.json(
        { ok: false, message: 'Seules les éditions en brouillon peuvent être modifiées' },
        { status: 409 }
      )
    }

    const body = await request.json() as {
      versionLabel?: string
      titleFr?: string
      contentFr?: string
      titleEn?: string
      contentEn?: string
    }

    if (typeof body.versionLabel === 'string' && body.versionLabel.trim()) {
      await query(
        'UPDATE terms_editions SET version_label = ? WHERE id = ?',
        [body.versionLabel.trim(), id]
      )
    }

    // Update French localization
    if (typeof body.titleFr === 'string' || typeof body.contentFr === 'string') {
      const titleFr = typeof body.titleFr === 'string' ? body.titleFr.trim() : null
      const contentFr = typeof body.contentFr === 'string' ? body.contentFr.trim() : null

      await query(
        `INSERT INTO terms_localizations (edition_id, locale, title, content)
         VALUES (?, 'fr', ?, ?)
         ON DUPLICATE KEY UPDATE
           title = COALESCE(NULLIF(?, ''), title),
           content = COALESCE(NULLIF(?, ''), content)`,
        [id, titleFr ?? '', contentFr ?? '', titleFr ?? '', contentFr ?? '']
      )
    }

    // Update English localization
    if (typeof body.titleEn === 'string' || typeof body.contentEn === 'string') {
      const titleEn = typeof body.titleEn === 'string' ? body.titleEn.trim() : null
      const contentEn = typeof body.contentEn === 'string' ? body.contentEn.trim() : null

      if (titleEn && contentEn) {
        await query(
          `INSERT INTO terms_localizations (edition_id, locale, title, content)
           VALUES (?, 'en', ?, ?)
           ON DUPLICATE KEY UPDATE
             title = COALESCE(NULLIF(?, ''), title),
             content = COALESCE(NULLIF(?, ''), content)`,
          [id, titleEn, contentEn, titleEn, contentEn]
        )
      }
    }

    return NextResponse.json({ ok: true })
  } catch {
    return NextResponse.json({ ok: false, message: 'Erreur serveur' }, { status: 500 })
  }
}

export async function DELETE(
  _request: Request,
  { params }: { params: { id: string } }
) {
  if (!isAdmin()) return NextResponse.json({ ok: false }, { status: 403 })

  const id = parseInt(params.id, 10)
  if (isNaN(id)) return NextResponse.json({ ok: false }, { status: 400 })

  try {
    const edition = await getEdition(id)
    if (!edition) return NextResponse.json({ ok: false, message: 'Édition introuvable' }, { status: 404 })
    if (edition.status !== 'draft') {
      return NextResponse.json(
        { ok: false, message: 'Seules les éditions en brouillon peuvent être supprimées' },
        { status: 409 }
      )
    }

    // Delete localizations first (FK)
    await query('DELETE FROM terms_localizations WHERE edition_id = ?', [id])
    await query('DELETE FROM terms_editions WHERE id = ?', [id])

    return NextResponse.json({ ok: true })
  } catch {
    return NextResponse.json({ ok: false, message: 'Erreur serveur' }, { status: 500 })
  }
}
