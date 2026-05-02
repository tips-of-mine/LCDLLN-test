import { NextResponse } from 'next/server'
import { isAuthenticatedAdmin } from '@/lib/apiAuth'
import { query } from '@/lib/db'

export async function PATCH(_req: Request, { params }: { params: { id: string } }) {
  if (!await isAuthenticatedAdmin()) return NextResponse.json({ ok: false }, { status: 403 })
  const id = parseInt(params.id, 10)
  if (isNaN(id)) return NextResponse.json({ ok: false }, { status: 400 })
  try {
    await query(
      "UPDATE accounts SET account_status = 'active', disabled_reason = NULL WHERE id = ?",
      [id]
    )
    return NextResponse.json({ ok: true })
  } catch {
    return NextResponse.json({ ok: false, message: 'Erreur serveur' }, { status: 500 })
  }
}
