import { cookies } from 'next/headers'
import { query } from '@/lib/db'
import type { RowDataPacket } from 'mysql2/promise'

export type Session = {
  accountId: number
  role: 'player' | 'admin'
  tagId: string | null
  login: string
} | null

export async function getSession(): Promise<Session> {
  const jar = cookies()
  const raw = jar.get('lcdlln_portal_account')?.value
  if (!raw) return null
  const accountId = parseInt(raw, 10)
  if (isNaN(accountId) || accountId <= 0) return null
  try {
    const rows = await query<Array<RowDataPacket & {
      id: number; login: string; role: string; tag_id: string | null
    }>>(
      'SELECT id, login, role, tag_id FROM accounts WHERE id = ? LIMIT 1',
      [accountId]
    )
    const row = rows[0]
    if (!row) return null
    return {
      accountId: row.id,
      role: (row.role === 'admin' ? 'admin' : 'player') as 'player' | 'admin',
      tagId: row.tag_id ?? null,
      login: row.login,
    }
  } catch {
    return null
  }
}
