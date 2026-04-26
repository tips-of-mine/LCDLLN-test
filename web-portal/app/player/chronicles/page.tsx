import { redirect } from 'next/navigation'
import { getSession } from '@/lib/session'
import { query } from '@/lib/db'
import { ExploitsProfile } from '@/components/ExploitsProfile'
import { CharacterDeleteButton } from '@/components/CharacterDeleteButton'
import { getPlayerExploitsData } from '@/lib/exploitsData'
import type { RowDataPacket } from 'mysql2/promise'

export const dynamic = 'force-dynamic'

type ShardPlaytime = RowDataPacket & {
  shard_name: string
  total_playtime: number
  character_count: number
}

type CharacterRow = RowDataPacket & {
  id: number
  name: string
  slot: number
  total_playtime: number
}

function formatPlaytime(seconds: number): string {
  const h = Math.floor(seconds / 3600)
  const m = Math.floor((seconds % 3600) / 60)
  if (h === 0 && m === 0) return '0m'
  if (h === 0) return `${m}m`
  if (m === 0) return `${h}h`
  return `${h}h ${m}m`
}

export default async function ChroniquesPage() {
  const session = await getSession()
  if (!session) redirect('/login')

  const accountId = session.accountId

  // Section 1: Temps de jeu par serveur
  const shardRows = await query<ShardPlaytime[]>(
    `SELECT s.name as shard_name,
            COALESCE(SUM(cs.playtime_seconds), 0) as total_playtime,
            COUNT(c.id) as character_count
     FROM shards s
     LEFT JOIN characters c ON c.account_id = ? AND c.deleted_at IS NULL
     LEFT JOIN character_stats cs ON cs.character_id = c.id AND cs.shard_id = s.id
     GROUP BY s.id, s.name
     ORDER BY total_playtime DESC`,
    [accountId]
  )

  // Section 2: Exploits
  const exploitsData = await getPlayerExploitsData(accountId)

  // Section 3: Personnages
  const characterRows = await query<CharacterRow[]>(
    `SELECT c.id, c.name, c.slot,
            COALESCE(SUM(cs.playtime_seconds), 0) as total_playtime
     FROM characters c
     LEFT JOIN character_stats cs ON cs.character_id = c.id
     WHERE c.account_id = ? AND c.deleted_at IS NULL
     GROUP BY c.id, c.name, c.slot
     ORDER BY c.slot ASC`,
    [accountId]
  )

  return (
    <div className="wp-main">
      <div className="wp-page-header">
        <h1>Mes Chroniques</h1>
        <p>Temps de jeu, exploits et gestion de vos personnages.</p>
      </div>

      {/* Section 1: Temps de jeu par serveur */}
      <div className="wp-section-title">Temps de jeu par serveur</div>
      <div className="wp-section-sub">
        Durée totale jouée sur chaque serveur, tous personnages confondus.
      </div>

      {shardRows.length === 0 ? (
        <div className="wp-card" style={{ textAlign: 'center', padding: '2rem', color: 'var(--ln-muted)' }}>
          Aucune donnée de serveur disponible.
        </div>
      ) : (
        <div className="wp-card" style={{ padding: 0, overflow: 'hidden' }}>
          <table style={{ width: '100%', borderCollapse: 'collapse', fontSize: 14 }}>
            <thead>
              <tr style={{ borderBottom: '1px solid var(--ln-border)' }}>
                <th style={{ padding: '12px 16px', textAlign: 'left', fontFamily: 'var(--font-ui)', fontSize: 11, letterSpacing: '.15em', textTransform: 'uppercase', color: 'var(--ln-muted)', fontWeight: 500 }}>
                  Serveur
                </th>
                <th style={{ padding: '12px 16px', textAlign: 'right', fontFamily: 'var(--font-ui)', fontSize: 11, letterSpacing: '.15em', textTransform: 'uppercase', color: 'var(--ln-muted)', fontWeight: 500 }}>
                  Temps joué
                </th>
                <th style={{ padding: '12px 16px', textAlign: 'right', fontFamily: 'var(--font-ui)', fontSize: 11, letterSpacing: '.15em', textTransform: 'uppercase', color: 'var(--ln-muted)', fontWeight: 500 }}>
                  Personnages
                </th>
              </tr>
            </thead>
            <tbody>
              {shardRows.map((row, i) => (
                <tr
                  key={row.shard_name}
                  style={{
                    borderBottom: i < shardRows.length - 1 ? '1px solid var(--ln-border)' : 'none',
                  }}
                >
                  <td style={{ padding: '12px 16px', color: 'var(--ln-text)', fontWeight: 500 }}>
                    {row.shard_name}
                  </td>
                  <td style={{ padding: '12px 16px', textAlign: 'right', color: 'var(--ln-accent)', fontFamily: 'var(--font-mono)', fontSize: 13 }}>
                    {formatPlaytime(Number(row.total_playtime))}
                  </td>
                  <td style={{ padding: '12px 16px', textAlign: 'right', color: 'var(--ln-muted)' }}>
                    {Number(row.character_count)}
                  </td>
                </tr>
              ))}
            </tbody>
          </table>
        </div>
      )}

      {/* Section 2: Mes Exploits */}
      <div className="wp-section-title" style={{ marginTop: '2rem' }}>Mes Exploits</div>
      <ExploitsProfile data={exploitsData} />

      {/* Section 3: Mes Personnages */}
      <div className="wp-section-title" style={{ marginTop: '2rem' }}>Mes Personnages</div>
      <div className="wp-section-sub">
        Liste de vos personnages actifs. La suppression est irréversible.
      </div>

      {characterRows.length === 0 ? (
        <div className="wp-card" style={{ textAlign: 'center', padding: '2rem', color: 'var(--ln-muted)' }}>
          Aucun personnage actif sur ce compte.
        </div>
      ) : (
        <div style={{ display: 'grid', gap: '0.5rem' }}>
          {characterRows.map((char) => (
            <div key={char.id} className="wp-card" style={{ display: 'flex', alignItems: 'center', justifyContent: 'space-between', flexWrap: 'wrap', gap: 12 }}>
              <div>
                <div style={{ display: 'flex', alignItems: 'center', gap: 10 }}>
                  <strong style={{ color: 'var(--ln-text)', fontSize: 15 }}>{char.name}</strong>
                  <span className="wp-badge planned" style={{ fontSize: 11 }}>
                    Slot {char.slot}
                  </span>
                </div>
                <div style={{ fontSize: 12, color: 'var(--ln-muted)', marginTop: 4, fontFamily: 'var(--font-mono)' }}>
                  {formatPlaytime(Number(char.total_playtime))} joués
                </div>
              </div>
              <CharacterDeleteButton characterId={char.id} characterName={char.name} />
            </div>
          ))}
        </div>
      )}
    </div>
  )
}
