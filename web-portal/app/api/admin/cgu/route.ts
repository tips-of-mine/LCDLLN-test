// POST /api/admin/cgu
// Body: { versionLabel, titleFr, contentFr, titleEn?, contentEn? }
// Creates a new terms_edition (status='draft') + terms_localizations entries
import { NextResponse } from 'next/server'
import { cookies } from 'next/headers'
import { query } from '@/lib/db'
import type { ResultSetHeader } from 'mysql2/promise'

function isAdmin(): boolean {
  const jar = cookies()
  return jar.get('lcdlln_portal_role')?.value === 'admin'
}

export async function POST(request: Request) {
  if (!isAdmin()) return NextResponse.json({ ok: false }, { status: 403 })

  try {
    const body = await request.json() as {
      versionLabel?: string
      titleFr?: string
      contentFr?: string
      titleEn?: string
      contentEn?: string
    }

    const versionLabel = typeof body.versionLabel === 'string' ? body.versionLabel.trim() : ''
    const titleFr = typeof body.titleFr === 'string' ? body.titleFr.trim() : ''
    const contentFr = typeof body.contentFr === 'string' ? body.contentFr.trim() : ''

    if (!versionLabel || !titleFr || !contentFr) {
      return NextResponse.json(
        { ok: false, message: 'versionLabel, titleFr et contentFr sont requis' },
        { status: 400 }
      )
    }

    // Create edition
    const result = await query<ResultSetHeader>(
      `INSERT INTO terms_editions (version_label, status, published_at, retired_reason)
       VALUES (?, 'draft', NULL, NULL)`,
      [versionLabel]
    )
    const editionId = result.insertId

    // French localization (required)
    await query(
      `INSERT INTO terms_localizations (edition_id, locale, title, content)
       VALUES (?, 'fr', ?, ?)`,
      [editionId, titleFr, contentFr]
    )

    // English localization (optional)
    const titleEn = typeof body.titleEn === 'string' ? body.titleEn.trim() : ''
    const contentEn = typeof body.contentEn === 'string' ? body.contentEn.trim() : ''
    if (titleEn && contentEn) {
      await query(
        `INSERT INTO terms_localizations (edition_id, locale, title, content)
         VALUES (?, 'en', ?, ?)`,
        [editionId, titleEn, contentEn]
      )
    }

    return NextResponse.json({ ok: true, id: editionId }, { status: 201 })
  } catch {
    return NextResponse.json({ ok: false, message: 'Erreur serveur' }, { status: 500 })
  }
}
