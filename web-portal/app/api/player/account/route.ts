// PATCH /api/player/account
// Body: { firstName?, lastName?, addressStreet?, addressCity?, addressZip?, addressCountry? }
import { NextResponse } from 'next/server'
import { getSession } from '@/lib/auth/session'
import { query } from '@/lib/db/connection'
import { logError } from '@/lib/log'

export async function PATCH(request: Request) {
  const session = await getSession()
  if (!session) return NextResponse.json({ ok: false, message: 'Non authentifié' }, { status: 401 })
  const accountId = session.accountId

  try {
    const body = await request.json() as Record<string, string>
    // GARDE anti-triche (spec 2026-07-18, récompenses d'anniversaire) :
    // birth_date et created_at sont IMMUABLES — ils conditionnent des
    // récompenses annuelles côté serveur. Ne JAMAIS les ajouter à ce
    // fieldMap ni créer un autre chemin d'édition.
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
  } catch (err) {
    logError('PATCH /api/player/account', 'Account update failed', { err })
    return NextResponse.json({ ok: false, message: 'Erreur serveur' }, { status: 500 })
  }
}
