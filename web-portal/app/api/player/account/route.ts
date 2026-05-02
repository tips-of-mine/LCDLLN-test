// PATCH /api/player/account
// Body: { firstName?, lastName?, addressStreet?, addressCity?, addressZip?, addressCountry? }
import { NextResponse } from 'next/server'
import { query } from '@/lib/db'
import { getAuthenticatedAccountId } from '@/lib/apiAuth'

export async function PATCH(request: Request) {
  const accountId = await getAuthenticatedAccountId()
  if (!accountId) return NextResponse.json({ ok: false, message: 'Non authentifié' }, { status: 401 })

  try {
    const body = await request.json() as Record<string, string>
    const fieldMap: Record<string, string> = {
      firstName: 'first_name',
      lastName: 'last_name',
      addressStreet: 'address_street',
      addressCity: 'address_city',
      addressZip: 'address_zip',
      addressCountry: 'address_country',
    }
    const updates: string[] = []
    const values: unknown[] = []
    for (const [key, col] of Object.entries(fieldMap)) {
      if (typeof body[key] === 'string') {
        updates.push(`${col} = ?`)
        values.push(body[key].trim())
      }
    }
    if (updates.length === 0) {
      return NextResponse.json({ ok: false, message: 'Aucun champ à mettre à jour' }, { status: 400 })
    }
    values.push(accountId)
    await query(`UPDATE accounts SET ${updates.join(', ')} WHERE id = ?`, values)
    return NextResponse.json({ ok: true })
  } catch {
    return NextResponse.json({ ok: false, message: 'Erreur serveur' }, { status: 500 })
  }
}
