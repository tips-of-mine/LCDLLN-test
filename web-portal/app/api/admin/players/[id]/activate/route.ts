import { NextResponse } from 'next/server'
import { cookies } from 'next/headers'
import { query } from '@/lib/db/connection'

async function checkAdmin() {
  const role = cookies().get('lcdlln_portal_role')?.value
  return role === 'admin'
}

export async function PATCH(_req: Request, { params }: { params: { id: string } }) {
  if (!await checkAdmin()) return NextResponse.json({ ok: false }, { status: 403 })
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
