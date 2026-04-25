# Spec — Web Portal : Auth & Navigation (Sous-projet A)

**Date :** 2026-04-25
**Statut :** Approuvé
**Périmètre :** Finalisation du système d'authentification du portail web et navigation conditionnelle selon l'état de session.

---

## Contexte

Le portail web (`web-portal/`) utilise Next.js App Router. Un cookie httpOnly `lcdlln_portal_account` était écrit au login, mais jamais lu côté serveur. La navigation (`SiteHeader`) était entièrement statique : bouton "Connexion" toujours visible, "Espace joueur" et "Admin" accessibles à tous sans protection. L'authentification n'avait jamais été validée de bout en bout.

Ce sous-projet est le prérequis bloquant pour les sous-projets B (Espace joueur), C (Email), D (Admin) et E (Roadmap dynamique).

---

## Ce qui ne change pas côté `lcdlln.exe`

- La table `accounts` est partagée entre le client jeu et le portail. La migration 0020 ajoute uniquement la colonne `role` — colonne non lue par le client jeu ni par le serveur master. Aucune modification des handlers C++ ni du protocole réseau.

---

## 1. Migration DB — 0020

**Fichier :** `db/migrations/0020_accounts_role.sql`

Ajout idempotent de la colonne `role` sur `accounts` :

```sql
role ENUM('player','admin','moderator') NOT NULL DEFAULT 'player'
```

- Valeur par défaut `'player'` → aucun compte existant n'est impacté.
- La promotion en `admin` ou `moderator` se fait manuellement en SQL (ou via le futur panel admin, sous-projet D).
- Pattern idempotent identique aux migrations 0006–0019 (vérification `information_schema.columns` avant `ALTER TABLE`).

---

## 2. Module de session — `lib/session.ts`

**Cookie :** `lcdlln_session` (remplace `lcdlln_portal_account`)

**Payload JSON :**
```typescript
type SessionPayload = {
  v: 1;
  accountId: number;
  tagId: string;
  login: string;
  role: 'player' | 'admin' | 'moderator';
};
```

**Signature :** HMAC-SHA256 sur `payload_b64` avec `process.env.SESSION_HMAC_SECRET`.
**Format cookie :** `<payload_base64url>.<signature_hex>`
**Options cookie :** httpOnly, SameSite=lax, Secure en production, maxAge 7 jours, path=/

**API exposée :**
- `signSession(payload: SessionPayload): string` → produit la valeur du cookie
- `verifySession(cookieValue: string): SessionPayload | null` → vérifie HMAC, parse, retourne null si invalide
- `readSession(cookieStore: ReadonlyRequestCookies): SessionPayload | null` → lit le cookie `lcdlln_session` et appelle `verifySession`

Si `SESSION_HMAC_SECRET` n'est pas défini en environnement, `signSession` lève une erreur et `verifySession` retourne `null` (pas de crash silencieux).

---

## 3. Mise à jour de `lib/portalLogin.ts`

La requête SQL est étendue :
```sql
SELECT id, login, password_hash, tag_id, role
FROM accounts
WHERE LOWER(email) = ? OR login = ?
LIMIT 1
```

Type de retour mis à jour :
```typescript
type PortalLoginResult =
  | { ok: true; accountId: number; login: string; tagId: string; role: string }
  | { ok: false; code: 'missing' | 'invalid' | 'db' };
```

La logique de vérification du hash (double Argon2id + fallback scrypt) reste inchangée.

---

## 4. Route `app/api/auth/login/route.ts`

Après vérification réussie :
1. Supprimer le cookie `lcdlln_portal_account` (compatibilité avec les éventuelles sessions anciennes).
2. Écrire le cookie `lcdlln_session` via `signSession()`.
3. Déterminer la valeur de `redirect` :
   - Lire le paramètre `next` du body JSON.
   - Valide si `next` commence par `/` et ne contient pas `//` (protection open redirect).
   - Sinon, redirect par défaut vers `/player`.
4. Retourner `{ ok: true, redirect }`.

---

## 5. Route `app/api/auth/logout/route.ts` *(nouveau)*

**Méthode :** POST

Actions :
1. Effacer le cookie `lcdlln_session` (maxAge=0).
2. Retourner `{ ok: true }`.

Appelé via un `<form method="POST">` depuis le `SiteHeader` — fonctionne sans JavaScript.

---

## 6. Middleware — `middleware.ts`

**Fichier :** `middleware.ts` à la racine du projet Next.js.

**Routes protégées :**
- `/player` et `/player/:path*` → requiert session valide (tout rôle)
- `/admin` et `/admin/:path*` → requiert session valide ET `role` dans `['admin', 'moderator']`

**Comportement :**
- Session absente ou invalide sur `/player/*` → redirect vers `/login?next=<url_encodée>`
- Session valide mais rôle insuffisant sur `/admin/*` → redirect vers `/` (pas de 403 exposé)
- Les routes `/api/*` sont exclues du middleware (config `matcher`)

**Config matcher :**
```typescript
export const config = {
  matcher: ['/player/:path*', '/admin/:path*'],
};
```

---

## 7. Layout `app/layout.tsx`

Devient un **Server Component async**. Lit la session via `readSession(cookies())` et passe `session: SessionPayload | null` en prop à `SiteHeader`.

Aucune autre modification du layout.

---

## 8. Composant `components/SiteHeader.tsx`

Splitté en deux parties pour compatibilité Server Component + interactivité mobile :

### `SiteHeader` (Server Component)
Reçoit `session: SessionPayload | null`. Rend le shell statique de la topbar.

**Liens conditionnels :**

| Lien | Condition d'affichage |
|---|---|
| Roadmap | Toujours |
| Support | Toujours |
| Signaler un bug | Toujours |
| Espace joueur | `session !== null` uniquement |
| Admin | `session?.role === 'admin' \|\| session?.role === 'moderator'` |
| TAG-ID joueur | `session !== null` — affiche `session.tagId`, style distinctif (non-cliquable) |
| Connexion (CTA) | `session === null` uniquement |
| Déconnexion | `session !== null` — `<form action="/api/auth/logout" method="POST"><button>` |

**Style TAG-ID :** police `var(--font-display)`, couleur `var(--ln-accent)`, lettrage espacé — visuellement différencié du reste de la nav.

**Style Déconnexion :** classe `btn btn-ghost`, dans le thème Lune Noire.

### `NavToggle` (Client Component)
Gère uniquement le `useState(open)` pour le menu hamburger mobile. Reçoit les liens nav en `children` (pattern shell/slot). Aucune logique métier.

---

## 9. Page `app/login/page.tsx`

- Lit `searchParams.next` (passé en query string par le middleware).
- Inclut `next` dans le body du `fetch` vers `/api/auth/login`.
- Après succès, `router.push(data.redirect)`.

---

## 10. Variables d'environnement requises

| Variable | Description |
|---|---|
| `SESSION_HMAC_SECRET` | Secret HMAC-SHA256, min 32 caractères aléatoires |
| `DATABASE_URL` | Déjà existante |

À documenter dans `.env.example`.

---

## Fichiers créés ou modifiés

| Fichier | Action |
|---|---|
| `db/migrations/0020_accounts_role.sql` | Nouveau |
| `web-portal/lib/session.ts` | Nouveau |
| `web-portal/lib/portalLogin.ts` | Modifié |
| `web-portal/app/api/auth/login/route.ts` | Modifié |
| `web-portal/app/api/auth/logout/route.ts` | Nouveau |
| `web-portal/middleware.ts` | Nouveau |
| `web-portal/app/layout.tsx` | Modifié |
| `web-portal/components/SiteHeader.tsx` | Modifié (split server/client) |
| `web-portal/app/login/page.tsx` | Modifié |
| `web-portal/.env.example` | Modifié |

---

## Hors périmètre de ce sous-projet

- Espace joueur complet → Sous-projet B
- Intégration des templates email → Sous-projet C
- Panel admin complet → Sous-projet D
- Roadmap dynamique → Sous-projet E
- MFA → prévu en Sous-projet B (placeholder uniquement)
- Contrôle parental → Sous-projet B
