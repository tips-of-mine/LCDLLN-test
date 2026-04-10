/**
 * Aligné sur engine/auth/Argon2Hash.h + MysqlAccountStore::CreateAccount :
 * client_hash = Argon2id(mot de passe, sel_dérivé_du_login), puis
 * password_hash en base = Argon2id(client_hash, sel_serveur aléatoire).
 */
import { createHash, randomBytes } from "node:crypto";
import argon2 from "argon2";

const CLIENT_SALT_PREFIX = Buffer.from(
  "LCDLLN\x1fclient_argon_salt\x1fv1\x1f",
  "utf8",
);

export function normalizeLoginForGameSalt(login: string): string {
  return login.trim();
}

export function deriveClientPasswordSalt(login: string): Buffer {
  const normalized = normalizeLoginForGameSalt(login);
  return createHash("sha256")
    .update(CLIENT_SALT_PREFIX)
    .update(Buffer.from(normalized, "utf8"))
    .digest()
    .subarray(0, 16);
}

const ARGON2_GAME_OPTS = {
  type: argon2.argon2id,
  timeCost: 2,
  memoryCost: 65536,
  parallelism: 1,
  hashLength: 32,
} as const;

/**
 * Valeur de la colonne accounts.password_hash pour le client jeu (double Argon2id).
 */
export async function hashPasswordForGameMaster(login: string, plainPassword: string): Promise<string> {
  const salt = deriveClientPasswordSalt(login);
  const clientHash = await argon2.hash(plainPassword, {
    ...ARGON2_GAME_OPTS,
    salt,
  });
  const serverSalt = randomBytes(16);
  return argon2.hash(clientHash, {
    ...ARGON2_GAME_OPTS,
    salt: serverSalt,
  });
}
