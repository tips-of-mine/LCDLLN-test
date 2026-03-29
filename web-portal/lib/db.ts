import mysql from "mysql2/promise";

let pool: mysql.Pool | null = null;

export function getPool(): mysql.Pool | null {
  const url = process.env.DATABASE_URL;
  if (!url) return null;
  if (!pool) {
    pool = mysql.createPool(url);
  }
  return pool;
}

export async function query<T>(sql: string, params?: unknown[]): Promise<T> {
  const p = getPool();
  if (!p) throw new Error("DATABASE_URL non configuré");
  const [rows] = await p.execute(sql, params);
  return rows as T;
}
