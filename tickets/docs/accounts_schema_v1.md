# Accounts schema v1 — Specification (source of truth)

**Version:** 1  
**Scope:** Account model only (no characters). Server is source of truth.  
**DB:** Implemented in M21; this document defines fields and validation.

---

## 1. Account fields

| Field            | Type     | Max length | Unique | Normalization   | Description |
|------------------|----------|------------|--------|-----------------|-------------|
| email            | string   | 256        | yes    | trim, lower-case| Contact email. |
| login            | string   | 64         | yes    | trim            | Login name (may equal email or be distinct). |
| account_status   | enum     | —          | no     | —               | active \| banned \| locked. |
| created_at       | datetime | —          | no     | —               | Creation timestamp (server). |
| updated_at       | datetime | —          | no     | —               | Last update timestamp (server). |

- **Password:** Never stored in plaintext. Store only a secure hash (e.g. Argon2id/bcrypt) and salt; not a field in the "account" row for schema doc purposes.
- **login** may be equal to email (single identifier) or distinct; both are unique and normalized.

---

## 2. Validation rules

### 2.1 Email

- **Format:** Valid email form (local@domain; domain with at least one dot allowed; no leading/trailing spaces after normalisation).
- **Max length:** 256 bytes (UTF-8) after normalisation.
- **Normalisation (server-side):** trim, then lower-case.
- **Reject:** empty after trim, invalid format, or length > 256.

### 2.2 Login

- **Charset:** Printable ASCII (0x20–0x7E) and/or letters/digits/underscore only (policy: alphanumeric + underscore).
- **Length:** 3–64 bytes (UTF-8) after trim.
- **Normalisation (server-side):** trim only.
- **Reject:** empty, length < 3 or > 64, or disallowed characters.

### 2.3 Password (policy v1)

- **Min length:** 8 characters.
- **Complexity (v1):** at least one digit, at least one letter (a–z or A–Z).
- **Max length:** 256 bytes (to bound hashing input).
- **Reject:** length < 8 or > 256, or complexity not met. Never store plaintext.

---

## 3. Max lengths (summary)

| Field   | Max length (bytes) |
|---------|---------------------|
| email   | 256                 |
| login   | 64                  |
| password (input) | 256          |

---

## 4. Examples (accepted / refused)

### Email

| Input                | After norm      | Result   | Reason |
|----------------------|-----------------|----------|--------|
| ` user@Example.COM ` | `user@example.com` | accepted | valid, norm ok |
| `user@domain`        | `user@domain`   | refused  | invalid format (no dot in domain) |
| ``                   | ``              | refused  | empty |
| `<256 chars>@x.co`   | —               | accepted | if format valid |
| `a@b.co` (257 chars) | —               | refused  | over max length |

### Login

| Input       | After norm | Result   | Reason |
|-------------|------------|----------|--------|
| `  Player1  ` | `Player1`  | accepted | alphanumeric + trim |
| `ab`        | `ab`       | refused  | length < 3 |
| `valid_login_42` | `valid_login_42` | accepted | allowed charset |
| `user<>`    | —          | refused  | disallowed chars |
| (65 chars)  | —          | refused  | over 64 |

### Password

| Input        | Result   | Reason |
|--------------|----------|--------|
| `short`      | refused  | length < 8 |
| `onlyletters`| refused  | no digit |
| `12345678`   | refused  | no letter |
| `Pass1234`   | accepted | min length + digit + letter |
| (257 chars)  | refused  | over max length |

---

## 5. Error codes (mapping, aligned with protocol v1)

| Code | Name             | When to use |
|------|------------------|-------------|
| 201  | REGISTRATION_INVALID | Generic invalid registration data (legacy). |
| 203  | INVALID_EMAIL    | Email format invalid or over max length. |
| 204  | WEAK_PASSWORD    | Password length or complexity policy not met. |
| 205  | INVALID_LOGIN    | Login charset/length invalid. |
| 202  | LOGIN_ALREADY_TAKEN | Login or email already registered. |

Use 203/204/205 for explicit client feedback; 201 remains for generic invalid.

---

## 6. Planned columns (for M21 DB)

Table **accounts** (or equivalent):

- `id` (PK, auto)
- `email` (VARCHAR(256), UNIQUE, NOT NULL)
- `login` (VARCHAR(64), UNIQUE, NOT NULL)
- `password_hash` (VARCHAR/type for hash, NOT NULL) — never plaintext
- `account_status` (ENUM or SMALLINT: active=0, banned=1, locked=2)
- `created_at` (TIMESTAMP/DATETIME)
- `updated_at` (TIMESTAMP/DATETIME)

Indexes: unique on `email`, unique on `login`.
