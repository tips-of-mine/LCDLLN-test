// web-portal/lib/playerCharacters.ts
import type { RowDataPacket, ResultSetHeader } from "mysql2/promise";
import { query } from "@/lib/db";

type CharacterRow = RowDataPacket & {
  id: number;
  name: string;
  slot: number;
  server_name: string | null;
  server_id: number | null;
  total_play_seconds: number | null;
  last_seen: string | null;
  created_at: string;
};

export type CharacterWithStats = {
  id: number;
  name: string;
  slot: number;
  serverName: string | null;
  serverId: number | null;
  totalPlaySeconds: number;
  lastSeen: string | null;
  createdAt: string;
};

export async function getCharactersWithStats(accountId: number): Promise<CharacterWithStats[]> {
  const rows = await query<CharacterRow[]>(
    `SELECT
       c.id,
       c.name,
       c.slot,
       gs.name        AS server_name,
       gs.server_id   AS server_id,
       cs.total_play_seconds,
       cs.last_seen,
       c.created_at
     FROM characters c
     LEFT JOIN character_stats cs ON cs.character_id = c.id
     LEFT JOIN game_servers gs    ON gs.server_id = cs.server_id
     WHERE c.account_id = ?
     ORDER BY c.slot ASC, cs.total_play_seconds DESC`,
    [accountId],
  );

  return rows.map((r) => ({
    id: r.id,
    name: r.name,
    slot: r.slot,
    serverName: r.server_name ?? null,
    serverId: r.server_id ?? null,
    totalPlaySeconds: r.total_play_seconds ?? 0,
    lastSeen: r.last_seen ?? null,
    createdAt: r.created_at,
  }));
}

export type DeleteResult = { ok: true } | { ok: false; message: string };

export async function deleteCharacter(
  characterId: number,
  accountId: number,
): Promise<DeleteResult> {
  const res = await query<ResultSetHeader>(
    "DELETE FROM characters WHERE id = ? AND account_id = ?",
    [characterId, accountId],
  );
  if (res.affectedRows === 0) {
    return { ok: false, message: "Personnage introuvable ou accès refusé." };
  }
  return { ok: true };
}
