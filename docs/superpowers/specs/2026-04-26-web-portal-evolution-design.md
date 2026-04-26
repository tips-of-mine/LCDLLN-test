# Web Portal — Évolution complète (Design Spec)

**Date :** 2026-04-26  
**Auteur :** hcornet  
**Approche :** Incrémentale par couches (A → B → C → D)  
**Impact lcdlln.exe :** Oui — colonnes `force_rename`, `parental_validated`, `deleted_at` lues par le serveur de jeu

---

## Contexte

Le web-portal est une application Next.js 14 connectée à la base de données MySQL partagée `lcdlln_master`. Il n'existe pas d'API REST entre le serveur de jeu (`lcdlln.exe`) et le web-portal — l'intégration est uniquement via la DB commune.

L'objectif de cette spec est de couvrir 6 évolutions majeures organisées en phases séquentielles :

- **Phase A** — Fix auth + Fondations (middleware, session, email)
- **Phase B** — Navigation conditionnelle (TAG-ID, déconnexion, liens)
- **Phase C** — Espace Joueur complet (5 sections)
- **Phase D** — Interface Admin complète (5 modules)
- **Phase E** — Roadmap dynamique + enrichissement contenu
- **Phase F** — Templates email intégrés

---

## Phase A — Fix Auth & Fondations

### A1. Fix argon2 (Docker/musl)

**Problème diagnostiqué :** Le package `argon2` utilise des bindings natifs C++ incompatibles avec musl libc (Alpine Linux). Erreur au runtime :
```
No native build was found for platform=linux arch=x64 runtime=node abi=115 uv=1 libc=musl
```

**Solution :** Remplacer `argon2` par `@node-rs/argon2` dans `package.json`. Ce package fournit des binaires précompilés pour toutes les plateformes (glibc et musl) sans compilation native. L'API est identique (`hash()`, `verify()`).

**Fichiers modifiés :**
- `package.json` — remplacement de la dépendance
- `lib/portalLogin.ts` — mise à jour de l'import
- `lib/gamePasswordHash.ts` — mise à jour de l'import

**Impact lcdlln.exe :** Aucun. Le hash Argon2id stocké en DB est inchangé.

---

### A2. Middleware de protection des routes

**Fichier à créer :** `web-portal/middleware.ts`

**Comportement :**
- Lit le cookie `lcdlln_portal_account` (accountId)
- Routes `/player/*` sans cookie → redirect `/login`
- Routes `/admin/*` sans cookie → redirect `/login`
- Routes `/admin/*` avec cookie mais `role != 'admin'` → redirect `/`
- Injecte les headers `x-account-id` et `x-account-role` dans la requête pour les Server Components

**Matcher :** `/player/:path*` et `/admin/:path*`

---

### A3. Module de session

**Fichier à créer :** `web-portal/lib/session.ts`

**Interface retournée :**
```typescript
type Session = {
  accountId: number
  role: 'player' | 'admin'
  tagId: string
  login: string
} | null
```

**Comportement :** Lit les headers `x-account-id` et `x-account-role` injectés par le middleware. Pour `tagId` et `login`, fait une requête DB légère sur `accounts` (mise en cache Next.js via `unstable_cache`).

---

### A4. Module email centralisé

**Fichier à créer :** `web-portal/lib/email.ts`

**Fonctions exposées :**
```typescript
sendVerificationEmail(to: string, token: string): Promise<void>
sendPasswordReset(to: string, token: string): Promise<void>
sendWelcome(to: string, login: string): Promise<void>
sendAccountConfirmed(to: string, login: string): Promise<void>
sendAccountDisabled(to: string, login: string, reason: string): Promise<void>
sendParentalValidation(to: string, playerLogin: string, token: string): Promise<void>
sendEmailChange(to: string, token: string): Promise<void>
```

**Mécanisme :** Chargement des templates HTML depuis `design/lune-noire-design-system/ui_kits/email/`, remplacement des variables `{{name}}`, `{{link}}`, `{{reason}}` par interpolation de string. Envoi via `nodemailer` avec la configuration SMTP existante.

**Templates à créer** (dans `design/lune-noire-design-system/ui_kits/email/`) :
- `account-disabled.html` — variables : `{{login}}`, `{{reason}}`, `{{contact_url}}`
- `parental-validation.html` — variables : `{{player_login}}`, `{{validation_link}}`
- `email-change.html` — variables : `{{login}}`, `{{new_email}}`, `{{confirmation_link}}`

Style identique aux templates existants : dark theme, Cinzel/EB Garamond, CTA doré `--ln-accent-gold`.

---

## Phase B — Navigation conditionnelle

### B1. Refactoring SiteHeader

`SiteHeader.tsx` devient un Server Component. Il appelle `getSession()` et passe les données de session à un Client Component enfant `HeaderActions.tsx` pour les interactions (déconnexion).

### B2. Affichage conditionnel

| Élément | Condition |
|---|---|
| Bouton "Connexion" | Non connecté uniquement |
| TAG-ID (texte, lecture seule) | Connecté — style Cinzel, couleur `--ln-accent-gold` |
| Lien "Déconnexion" | Connecté — style discret, hover accent |
| Lien "Espace joueur" | Connecté uniquement |
| Lien "ADMIN" | Connecté **et** `role = 'admin'` uniquement |
| Liens publics (Accueil, Roadmap, Support, Bugs) | Toujours visibles |

### B3. Route de déconnexion

**Fichier à créer :** `web-portal/app/api/auth/logout/route.ts`

`POST /api/auth/logout` — supprime le cookie `lcdlln_portal_account` (maxAge=0) et retourne redirect vers `/`.

---

## Phase C — Espace Joueur

Toutes les routes `/player/*` sont protégées par le middleware (Phase A).

### C1. Détail du compte — `/player/account`

**Champs affichés :**

| Champ | Modifiable | Comportement |
|---|---|---|
| Nom (`last_name`) | Oui | PATCH `/api/player/account` — sauvegarde directe |
| Prénom (`first_name`) | Oui | PATCH `/api/player/account` — sauvegarde directe |
| Adresse mail | Oui | Déclenche envoi `email-change.html` à la nouvelle adresse. `email_pending` + `email_pending_token` + `email_pending_expires_at` (48h) stockés en DB. L'ancienne adresse reste active jusqu'à confirmation. |
| TAG-ID | Non | Affiché en lecture seule |
| Adresse (rue, ville, code postal, pays) | Oui | PATCH `/api/player/account` — sauvegarde directe |

**Route de confirmation email :** `GET /api/player/account/confirm-email?token=xxx` — valide le token, met à jour `email` en DB, vide les champs `email_pending*`.

**Migration :** `0023_accounts_profile.sql`
```sql
ALTER TABLE accounts ADD COLUMN first_name VARCHAR(100) NULL;
ALTER TABLE accounts ADD COLUMN last_name VARCHAR(100) NULL;
ALTER TABLE accounts ADD COLUMN address_street VARCHAR(255) NULL;
ALTER TABLE accounts ADD COLUMN address_city VARCHAR(100) NULL;
ALTER TABLE accounts ADD COLUMN address_zip VARCHAR(20) NULL;
ALTER TABLE accounts ADD COLUMN address_country VARCHAR(100) NULL;
ALTER TABLE accounts ADD COLUMN email_pending VARCHAR(255) NULL;
ALTER TABLE accounts ADD COLUMN email_pending_token VARCHAR(128) NULL;
ALTER TABLE accounts ADD COLUMN email_pending_expires_at DATETIME NULL;
ALTER TABLE accounts ADD COLUMN disabled_reason TEXT NULL;
ALTER TABLE accounts ADD COLUMN parental_email VARCHAR(255) NULL;
ALTER TABLE accounts ADD COLUMN parental_validated TINYINT(1) NOT NULL DEFAULT 0;
ALTER TABLE accounts ADD COLUMN parental_token VARCHAR(128) NULL;
ALTER TABLE accounts ADD COLUMN parental_token_expires_at DATETIME NULL;
```

---

### C2. Mes Chroniques — `/player/chronicles`

Trois sous-sections sur la même page :

**Temps joué & personnages par serveur**
- Tableau listant chaque shard : nom du serveur, temps joué (formaté HH:MM depuis `character_stats.playtime_seconds`), nombre de personnages actifs
- Données : JOIN `shards` + `character_stats` + `characters` filtrés par `account_id` et `deleted_at IS NULL`

**Exploits débloqués**
- Réutilise le composant `ExploitsProfile.tsx` existant
- Données depuis `exploit_records` + `account_exploit_stats`

**Mes Personnages**
- Liste de tous les personnages (`deleted_at IS NULL`) avec : nom, shard, slot
- Bouton "Voir le détail" : expand inline affichant les stats du personnage depuis `character_stats`
- Bouton "Supprimer" : 
  1. Premier clic → dialog de confirmation "Es-tu sûr de vouloir supprimer ce personnage ?"
  2. Deuxième clic → dialog final "Cette action est irréversible. Confirmer la suppression ?"
  3. PATCH `/api/player/characters/[id]/delete` — set `deleted_at = NOW()` (soft delete)
- **Impact lcdlln.exe :** Le serveur de jeu doit ignorer les personnages où `deleted_at IS NOT NULL`. À vérifier/adapter côté engine.

**Migration :** `0024_characters_soft_delete.sql`
```sql
ALTER TABLE characters ADD COLUMN deleted_at DATETIME NULL DEFAULT NULL;
ALTER TABLE characters ADD COLUMN force_rename TINYINT(1) NOT NULL DEFAULT 0;
```

---

### C3. Contrôle Parental — `/player/parental`

Visible uniquement si `DATEDIFF(NOW(), accounts.birth_date) / 365.25 < 18`.

**État initial (non validé) :**
- Formulaire : saisie email du tuteur légal
- Bouton "Envoyer la demande de validation"
- POST `/api/player/parental/request` → enregistre `parental_email` en DB, génère `parental_token`, envoie `parental-validation.html`

**Route de validation parentale :** `GET /api/player/parental/validate?token=xxx` — set `parental_validated = 1`, vide le token. Le joueur peut désormais jouer.

**État en attente :** Message "Demande envoyée à [email]. En attente de validation." + bouton "Renvoyer"

**État validé :** Message de confirmation, date de validation, email du tuteur.

**Impact lcdlln.exe :** Le serveur de jeu lit `parental_validated`. Si `0` et joueur mineur, connexion au jeu bloquée.

---

### C4. Sécurité du compte — `/player/security`

**Changement de mot de passe :**
- Formulaire : ancien mot de passe (vérifié via `@node-rs/argon2`), nouveau mot de passe, confirmation
- PATCH `/api/player/password` — hash + stockage si ancien mot de passe valide

**MFA :**
- Section affichée avec titre "Authentification multi-facteurs"
- Badge "Bientôt disponible" — aucune logique fonctionnelle
- Design cohérent avec le reste de la page (section grisée/désactivée visuellement)

---

### C5. Vie Privée — `/player/privacy`

**CGU :**
- Tableau complet des éditions `terms_editions` (status = 'published')
- Colonnes : Version, Date de publication, Date d'acceptation (ou badge "Non accepté")
- Bouton "Accepter" pour les CGU non encore acceptées → POST `/api/player/cgu/accept`
- Réutilise la logique de `player/cgu/page.tsx` existante (à fusionner/refactorer)

**Visibilité du profil :**
- Sélecteur : Public / Amis uniquement / Personne
- PATCH `/api/player/privacy` — sauvegarde dans `account_privacy_settings`

**Migration :** `0025_privacy_settings.sql`
```sql
CREATE TABLE account_privacy_settings (
  account_id INT UNSIGNED NOT NULL PRIMARY KEY,
  profile_visibility ENUM('public', 'friends', 'none') NOT NULL DEFAULT 'public',
  updated_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  FOREIGN KEY (account_id) REFERENCES accounts(id) ON DELETE CASCADE
);
```

---

## Phase D — Interface Admin

Toutes les routes `/admin/*` sont protégées par le middleware (`role = 'admin'`).

### D1. Gestion de la Roadmap — `/admin/roadmap`

**Migration :** `0026_roadmap_items.sql`
```sql
CREATE TABLE roadmap_items (
  id INT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
  title VARCHAR(255) NOT NULL,
  description TEXT NULL,
  status ENUM('completed', 'in_progress', 'planned') NOT NULL DEFAULT 'planned',
  category VARCHAR(100) NULL,
  display_order INT NOT NULL DEFAULT 0,
  created_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
  updated_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP
);
-- Insertion des 12 items existants de roadmap/page.tsx
INSERT INTO roadmap_items (title, description, status, display_order) VALUES
  ('Infrastructure serveur de base', 'Mise en place de l''infrastructure serveur fondamentale', 'completed', 1),
  ('Système d''exploits', 'Implémentation du système de récompenses et d''exploits', 'completed', 2),
  ('Réinitialisation de mot de passe', 'Système de récupération de compte par email', 'completed', 3),
  ('Web Portal v1', 'Première version du portail web joueur', 'completed', 4),
  ('Formulaire de signalement de bugs', 'Interface de rapport de bugs avec paliers exploits', 'completed', 5),
  ('Authentification & Espace Joueur', 'Fix auth, middleware, espace joueur complet avec profil, chroniques, parental, sécurité et vie privée', 'in_progress', 6),
  ('Refonte UI/UX', 'Amélioration de l''interface et de l''expérience utilisateur', 'in_progress', 7),
  ('Interface Admin complète', 'Panel administration : joueurs, CGU, roadmap, FAQ, suivi bugs', 'in_progress', 8),
  ('Contrôle Parental', 'Validation parentale pour les joueurs mineurs', 'in_progress', 9),
  ('Visibilité de profil & Vie Privée', 'Paramètres de confidentialité du profil joueur', 'completed', 10),
  ('Monitoring serveur', 'Tableaux de bord de supervision des serveurs de jeu', 'planned', 11),
  ('Système de tickets support', 'Interface de support en ligne avec suivi de tickets', 'planned', 12),
  ('Notifications en jeu', 'Système de notifications push pour les événements du jeu', 'planned', 13),
  ('Système MFA', 'Authentification multi-facteurs (TOTP/email)', 'planned', 14);
```

**Interface admin :** Tableau des items avec colonnes Titre / Statut / Ordre. Actions : Ajouter (modal), Modifier (modal), Supprimer (confirmation). Drag & drop ou champs d'ordre numérique pour réordonner.

**Page publique `/roadmap` :** Lire depuis `roadmap_items` ORDER BY `display_order` au lieu du code statique.

---

### D2. Gestion des Joueurs — `/admin/players`

**Liste paginée** (25 par page) avec filtres : statut du compte, email vérifié, CGU complètes, présence de mineurs.

**Colonnes tableau :** TAG-ID / Login / Email / Statut / Email vérifié / CGU / Inscrit le / Actions

**Actions par joueur (boutons inline ou menu contextuel) :**

| Action | Implémentation |
|---|---|
| Valider l'email | PATCH `/api/admin/players/[id]/verify-email` → `email_verified = 1` |
| Réactiver le compte | PATCH `/api/admin/players/[id]/activate` → `account_status = 'active'` |
| Désactiver le compte | Modal avec champ "Motif" obligatoire → PATCH `/api/admin/players/[id]/disable` → `account_status = 'disabled'`, `disabled_reason = motif`, envoi `account-disabled.html` |
| Voir les CGU | Panel expand : liste CGU acceptées/non pour ce joueur |
| Voir les personnages | Panel expand : liste des personnages avec nom et shard. Bouton "Forcer renommage" → PATCH `/api/admin/characters/[id]/force-rename` → `force_rename = 1` |

---

### D3. Gestion des CGU — `/admin/cgu` (enrichissement)

**Règles métier :**
- CGU `draft` : modifiable, publiable, supprimable
- CGU `published` : **non modifiable**, supprimable avec motif obligatoire
- CGU `retired` : lecture seule

**Suppression avec motif :**
- Modal "Motif de suppression" (champ texte obligatoire)
- PATCH `/api/admin/cgu/[id]/retire` → `status = 'retired'`, `retired_reason = motif`

**Migration :** `0029_terms_retired_reason.sql`
```sql
ALTER TABLE terms_editions ADD COLUMN retired_reason TEXT NULL;
```

---

### D4. Support & FAQ — `/admin/faq`

**Migration :** `0027_faq_items.sql`
```sql
CREATE TABLE faq_items (
  id INT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
  question VARCHAR(500) NOT NULL,
  answer TEXT NOT NULL,
  category VARCHAR(100) NULL,
  display_order INT NOT NULL DEFAULT 0,
  published TINYINT(1) NOT NULL DEFAULT 0,
  created_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
  updated_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP
);
```

**Interface admin :** CRUD complet — Ajouter / Modifier (éditeur texte) / Archiver (`published = 0`). La page publique `/support` lit depuis `faq_items WHERE published = 1 ORDER BY display_order`.

---

### D5. Suivi des Bugs — `/admin/bugs`

**Migration :** `0028_bug_reports_admin.sql`
```sql
ALTER TABLE bug_reports ADD COLUMN admin_status ENUM('pending', 'confirmed', 'in_progress', 'resolved', 'not_a_bug') NOT NULL DEFAULT 'pending';
ALTER TABLE bug_reports ADD COLUMN admin_comment TEXT NULL;
ALTER TABLE bug_reports ADD COLUMN exploit_awarded TINYINT(1) NOT NULL DEFAULT 0;
```

**Interface admin :** Tableau des bugs avec colonnes : ID / Joueur / Titre / Statut admin / Date / Actions.

**Workflow :**
1. Sélectionner un bug → changer son statut
2. À la résolution (`resolved`) : case à cocher "Attribuer les points exploit au joueur rapporteur"
   - Si cochée : incrémenter le compteur dans `account_exploit_stats` pour le rapporteur
3. Si `not_a_bug` : champ commentaire visible du joueur dans son espace

---

## Phase E — Roadmap dynamique (page publique)

La page `/roadmap` est réécrite pour lire depuis `roadmap_items` au lieu du code statique. Le rendu visuel (timeline avec statuts ✅ / 🔄 / 📋) est conservé à l'identique — seule la source de données change.

---

## Phase F — Templates Email

### Templates à créer

**`account-disabled.html`** — Variables : `{{login}}`, `{{reason}}`, `{{contact_url}}`
- Objet : "Votre compte Lune Noire a été suspendu"
- Corps : notification de suspension avec motif affiché, lien vers le support

**`parental-validation.html`** — Variables : `{{player_login}}`, `{{validation_link}}`
- Objet : "Validation parentale requise — Lune Noire"
- Corps : explication du système de contrôle parental, bouton CTA "Valider l'accès"

**`email-change.html`** — Variables : `{{login}}`, `{{new_email}}`, `{{confirmation_link}}`
- Objet : "Confirmez votre nouvelle adresse email — Lune Noire"
- Corps : confirmation de la demande, bouton CTA "Confirmer mon adresse", note sur l'expiration 48h

Tous les templates suivent le design system Lune Noire : fond sombre `#0e0c0a`, typographie Cinzel/EB Garamond, CTA couleur `--ln-accent-gold`.

---

## Récapitulatif des migrations DB

| Migration | Tables / Colonnes |
|---|---|
| `0023_accounts_profile.sql` | `accounts` : first_name, last_name, address_*, email_pending*, disabled_reason, parental_* |
| `0024_characters_soft_delete.sql` | `characters` : deleted_at, force_rename |
| `0025_privacy_settings.sql` | Nouvelle table `account_privacy_settings` |
| `0026_roadmap_items.sql` | Nouvelle table `roadmap_items` + 14 items initiaux |
| `0027_faq_items.sql` | Nouvelle table `faq_items` |
| `0028_bug_reports_admin.sql` | `bug_reports` : admin_status, admin_comment, exploit_awarded |
| `0029_terms_retired_reason.sql` | `terms_editions` : retired_reason |

---

## Fichiers créés / modifiés

### Nouveaux fichiers
- `web-portal/middleware.ts`
- `web-portal/lib/session.ts`
- `web-portal/lib/email.ts`
- `web-portal/components/HeaderActions.tsx`
- `web-portal/app/api/auth/logout/route.ts`
- `web-portal/app/api/player/account/route.ts`
- `web-portal/app/api/player/account/confirm-email/route.ts`
- `web-portal/app/api/player/characters/[id]/delete/route.ts`
- `web-portal/app/api/player/parental/request/route.ts`
- `web-portal/app/api/player/parental/validate/route.ts`
- `web-portal/app/api/player/password/route.ts`
- `web-portal/app/api/player/privacy/route.ts`
- `web-portal/app/api/player/cgu/accept/route.ts`
- `web-portal/app/api/admin/players/[id]/verify-email/route.ts`
- `web-portal/app/api/admin/players/[id]/activate/route.ts`
- `web-portal/app/api/admin/players/[id]/disable/route.ts`
- `web-portal/app/api/admin/characters/[id]/force-rename/route.ts`
- `web-portal/app/api/admin/roadmap/route.ts`
- `web-portal/app/api/admin/roadmap/[id]/route.ts`
- `web-portal/app/api/admin/faq/route.ts`
- `web-portal/app/api/admin/faq/[id]/route.ts`
- `web-portal/app/api/admin/bugs/[id]/route.ts`
- `web-portal/app/api/admin/cgu/[id]/retire/route.ts`
- `web-portal/app/player/account/page.tsx`
- `web-portal/app/player/chronicles/page.tsx`
- `web-portal/app/player/parental/page.tsx`
- `web-portal/app/player/security/page.tsx`
- `web-portal/app/player/privacy/page.tsx`
- `web-portal/app/admin/roadmap/page.tsx`
- `web-portal/app/admin/players/page.tsx`
- `web-portal/app/admin/faq/page.tsx`
- `web-portal/app/admin/bugs/page.tsx`
- `design/lune-noire-design-system/ui_kits/email/account-disabled.html`
- `design/lune-noire-design-system/ui_kits/email/parental-validation.html`
- `design/lune-noire-design-system/ui_kits/email/email-change.html`
- `db/migrations/0023_accounts_profile.sql`
- `db/migrations/0024_characters_soft_delete.sql`
- `db/migrations/0025_privacy_settings.sql`
- `db/migrations/0026_roadmap_items.sql`
- `db/migrations/0027_faq_items.sql`
- `db/migrations/0028_bug_reports_admin.sql`
- `db/migrations/0029_terms_retired_reason.sql`

### Fichiers modifiés
- `package.json` — remplacement `argon2` → `@node-rs/argon2`
- `web-portal/lib/portalLogin.ts` — mise à jour import argon2
- `web-portal/lib/gamePasswordHash.ts` — mise à jour import argon2
- `web-portal/components/SiteHeader.tsx` — refactoring Server Component + HeaderActions
- `web-portal/app/roadmap/page.tsx` — lecture depuis DB
- `web-portal/app/support/page.tsx` — lecture depuis DB
- `web-portal/app/player/page.tsx` — hub mis à jour avec nouveaux liens
- `web-portal/app/admin/page.tsx` — hub mis à jour avec nouveaux modules
- `web-portal/app/admin/cgu/page.tsx` — ajout règles published/retired + motif suppression

---

## Contraintes & points d'attention

1. **Impact lcdlln.exe :** Les colonnes `force_rename`, `parental_validated`, `deleted_at` sont lues par le serveur de jeu C++. Toute modification du schéma doit être coordonnée avec l'équipe engine.
2. **Soft delete personnages :** Le serveur de jeu doit filtrer `WHERE deleted_at IS NULL` lors du chargement des personnages. À valider côté engine.
3. **Ordre des migrations :** Les migrations doivent être appliquées séquentiellement (0023 → 0029). Le `MigrationRunner.cpp` côté engine gère l'ordre via `schema_version`.
4. **Sécurité des tokens :** Tous les tokens (email_pending_token, parental_token) doivent être générés avec `crypto.randomBytes(64).toString('hex')` et avoir une expiration.
5. **Autorisation admin :** Le middleware vérifie `role = 'admin'` en DB à chaque requête sur `/admin/*`. Pas de JWT — vérification directe MySQL pour éviter les tokens obsolètes.
