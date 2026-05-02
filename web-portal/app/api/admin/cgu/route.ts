// POST /api/admin/cgu
// Body: { versionLabel, titleFr, contentFr, titleEn?, contentEn? }
// Creates a new terms_edition (status='draft') + terms_localizations entries
import { NextResponse } from 'next/server'
import { isAuthenticatedAdmin } from '@/lib/apiAuth'
import { query } from '@/lib/db'
import type { ResultSetHeader } from 'mysql2/promise'

export async function POST(request: Request) {
  if (!(await isAuthenticatedAdmin())) return NextResponse.json({ ok: false }, { status: 403 })

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

    // Create edition (draft: published_at stays NULL)
    const result = await query<ResultSetHeader>(
      `INSERT INTO terms_editions (version_label, status) VALUES (?, 'draft')`,
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
  } catch (err) {
    console.error('[POST /api/admin/cgu]', err)
    const msg = err instanceof Error ? err.message : 'Erreur serveur'
    return NextResponse.json({ ok: false, message: msg }, { status: 500 })
  }
}
