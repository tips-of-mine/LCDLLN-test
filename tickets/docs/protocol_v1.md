# Protocol v1 — Specification (source of truth)

**Version:** 1  
**Endianness:** little-endian  
**Max packet size:** 16 384 bytes (16 KB), hard limit.

This document is the single source of truth for the binary network protocol v1. Client and server implementations must follow it exactly. Do not rely on packed struct layout in memory (UB); encode/decode field by field.

---

## 1. Packet header (fixed-size, no padding)

All packets start with a **18-byte** header. Fields are strictly contiguous, little-endian.

| Offset | Type     | Field       | Description |
|--------|----------|-------------|-------------|
| 0      | uint16   | `size`      | Total packet size in bytes (header included). Must be ≥ 18 and ≤ 16384. |
| 2      | uint16   | `opcode`    | Message type (see opcode table). |
| 4      | uint16   | `flags`     | Reserved for v1; send 0, ignore on receive. |
| 6      | uint32   | `request_id` | Matching id for request/response. **0** = no response expected (e.g. server push). |
| 10     | uint64   | `session_id` | Session or connection identifier. 0 before auth. |

**Header size:** 2 + 2 + 2 + 4 + 8 = **18 bytes**.

---

## 2. Encoding rules

- **Byte order:** little-endian for all multi-byte integers.
- **Strings:** `uint16 length` (number of bytes) followed by **UTF-8** bytes. No null terminator. Max string length 8192 bytes (half of max packet for sanity).
- **Arrays:** `uint16 count` followed by `count` contiguous elements. Element layout is opcode-specific.
- **Compression:** none in v1.
- **Encryption:** TLS only; application payload is not encrypted separately.

---

## 3. Messaging model

- **Request/response:** client sends with non-zero `request_id`; server replies with same `request_id` and matching opcode (e.g. AUTH_RESPONSE for AUTH_REQUEST).
- **Server push:** server sends with `request_id == 0`; no response expected.
- **Matching:** receiver uses `request_id` to correlate responses to requests.

---

## 4. Official opcode table (v1)

| Opcode value | Name                | Direction       | Description |
|--------------|---------------------|-----------------|-------------|
| 1            | AUTH_REQUEST        | Client → Server | Login: username + password (payload). |
| 2            | AUTH_RESPONSE       | Server → Client | Login result: success flag + optional session_id / error. |
| 3            | REGISTER_REQUEST    | Client → Server | Account registration payload. |
| 4            | REGISTER_RESPONSE   | Server → Client | Registration result. |
| 5            | SERVER_LIST_REQUEST | Client → Server | Request list of realms/servers (payload may be empty). |
| 6            | SERVER_LIST_RESPONSE | Server → Client | List of servers (payload: array of server entries). |
| 7            | HEARTBEAT           | Both            | Keep-alive; payload empty or minimal (e.g. timestamp). |
| 8            | ERROR               | Server → Client | Error response: error code + optional message. |

**Payloads (binary layout after header):**

- **AUTH_REQUEST:** `uint16 username_len`, `uint8[username_len]` username_utf8, `uint16 password_len`, `uint8[password_len]` password_utf8.
- **AUTH_RESPONSE:** `uint8 success` (1=ok, 0=fail), if success: `uint64 session_id`; if fail: optional `uint32 error_code` (see error table).
- **REGISTER_REQUEST:** `uint16 login_len`, `uint8[login_len]` login_utf8, `uint16 password_len`, `uint8[password_len]` password_utf8, optional extra fields (e.g. email) as defined later.
- **REGISTER_RESPONSE:** `uint8 success`, optional `uint32 error_code`.
- **SERVER_LIST_REQUEST:** no payload (or reserved `uint16 0`).
- **SERVER_LIST_RESPONSE:** `uint16 count`, then for each: `uint16 name_len`, `uint8[name_len]` name_utf8, `uint32 addr_ip` (optional), `uint16 port`, etc. (exact layout can be extended; minimum: count + name per server).
- **HEARTBEAT:** no payload or `uint32 client_timestamp` (optional).
- **ERROR:** `uint32 error_code`, `uint16 message_len`, `uint8[message_len]` message_utf8 (message_len may be 0).

---

## 5. Official error codes (v1)

| Code | Name                  | Description / reaction |
|------|------------------------|------------------------|
| 0    | OK                    | No error (e.g. used in AUTH_RESPONSE success). |
| 97   | PACKET_OVERSIZE       | Packet size > 16 KB; server sends ERROR then closes. |
| 98   | UNKNOWN_OPCODE        | Opcode not recognized; server may send ERROR (policy). |
| 99   | INVALID_PACKET        | Malformed header or invalid packet; server sends ERROR then closes. |
| 100  | BAD_REQUEST           | Malformed packet or invalid field; close connection only if repeated. |
| 101  | INVALID_CREDENTIALS   | Auth failed; client may retry (re-auth). |
| 102  | ACCOUNT_LOCKED       | Account disabled; client should show message, no retry. |
| 103  | ACCOUNT_NOT_FOUND    | Login not found (auth or register). |
| 104  | ALREADY_LOGGED_IN    | Session conflict; client may disconnect and reconnect. |
| 200  | REGISTRATION_DISABLED | Register not allowed. |
| 201  | REGISTRATION_INVALID  | Invalid login/password/email format. |
| 202  | LOGIN_ALREADY_TAKEN   | Duplicate registration. |
| 203  | INVALID_EMAIL         | Email format invalid or over max length. |
| 204  | WEAK_PASSWORD         | Password length or complexity not met. |
| 205  | INVALID_LOGIN         | Login charset or length invalid. |
| 300  | SERVER_LIST_UNAVAILABLE | Server list temporarily unavailable; retry later. |
| 500  | INTERNAL_ERROR       | Server error; client may retry or disconnect. |
| 501  | TIMEOUT              | Request timed out; client may retry. |

**Reaction rules:**

- **ERROR packet:** client must handle according to `error_code` (show message, retry, or disconnect). No mandatory disconnect for all errors; only severe/repeated BAD_REQUEST or INTERNAL_ERROR may require disconnect.
- **Connection close:** on TLS close or transport failure; not implied by ERROR alone unless documented per code.

---

## 6. Limits (v1)

| Item            | Limit |
|-----------------|-------|
| Max packet size | 16 384 bytes (16 KB) |
| Header size     | 18 bytes |
| Max payload     | 16 366 bytes |
| Max string length | 8 192 bytes (recommended) |
| Opcode          | uint16 (0–65535); 1–255 reserved for v1. |

---

## 7. Example packets (hex dump + field-by-field)

### Example 1: AUTH_REQUEST

- **Intent:** client sends login "user" / password "secret", request_id=1, session_id=0.
- **Payload:** username length 4, "user", password length 6, "secret".
- **Total size:** 18 + 2+4+2+6 = 32 bytes.

**Hex dump (bytes in order, space-separated):**

```
20 00 01 00 00 00 01 00 00 00 00 00 00 00 00 00 04 00 75 73 65 72 06 00 73 65 63 72 65 74
```

**Field-by-field:**

| Bytes (hex)    | Field        | Value (decimal / meaning) |
|----------------|--------------|----------------------------|
| 20 00          | size         | 32 (total packet) |
| 01 00          | opcode       | 1 (AUTH_REQUEST) |
| 00 00          | flags        | 0 |
| 01 00 00 00    | request_id   | 1 |
| 00 00 00 00 00 00 00 00 | session_id | 0 |
| 04 00          | username_len | 4 |
| 75 73 65 72    | username_utf8| "user" |
| 06 00          | password_len| 6 |
| 73 65 63 72 65 74 | password_utf8 | "secret" |

---

### Example 2: ERROR (invalid credentials)

- **Intent:** server responds to the AUTH_REQUEST with error 101 (INVALID_CREDENTIALS), request_id=1 to match, message "Invalid login".
- **Payload:** error_code 101, message length 13, "Invalid login".
- **Total size:** 18 + 4 + 2 + 13 = 37 bytes.

**Hex dump:**

```
25 00 08 00 00 00 01 00 00 00 00 00 00 00 00 00 65 00 00 00 0D 00 49 6E 76 61 6C 69 64 20 6C 6F 67 69 6E
```

**Field-by-field:**

| Bytes (hex)    | Field        | Value (decimal / meaning) |
|----------------|--------------|----------------------------|
| 25 00          | size         | 37 (0x25) |
| 08 00          | opcode       | 8 (ERROR) |
| 00 00          | flags        | 0 |
| 01 00 00 00    | request_id   | 1 (matches AUTH_REQUEST) |
| 00 00 00 00 00 00 00 00 | session_id | 0 |
| 65 00 00 00    | error_code   | 101 (INVALID_CREDENTIALS) |
| 0D 00          | message_len  | 13 |
| 49 6E 76 61 6C 69 64 20 6C 6F 67 69 6E | message_utf8 | "Invalid login" |

---

## 8. Implementation note

- **No struct packing:** do not assume C/C++ struct layout matches wire format. Serialise and parse each field explicitly (read/write uint16, uint32, uint64, then length-prefixed bytes) to avoid undefined behaviour and ABI differences.
