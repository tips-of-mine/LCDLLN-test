import { NextResponse } from 'next/server'
import { cookies } from 'next/headers'

export async function POST() {
  const jar = cookies()
  jar.set('lcdlln_portal_account', '', { maxAge: 0, path: '/' })
  jar.set('lcdlln_portal_role', '', { maxAge: 0, path: '/' })
  return NextResponse.json({ ok: true })
}
