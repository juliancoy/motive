# Auth Model (Motive Editor)

This document defines the authentication model currently implemented in `motive3d` for `motive_editor`, aligned to the same endpoint schema/connectivity pattern used in `../../QTSynth`.

## Scope

- Applies to `motive_editor` startup auth flow.
- Supports email/password login, registration, browser OAuth, stored-session reuse, and guest mode.
- Auth is optional at runtime when no auth API base URL is configured.

## API Base URL Resolution

Auth API base URL is resolved in this order:

1. `--auth-api <url>` CLI flag
2. `MOTIVE_AUTH_API_BASE_URL` environment variable
3. compile-time CMake definition `MOTIVE_AUTH_API_BASE_URL` (default: `https://jsynth.us/api`)

If the resolved URL is empty, editor starts in guest mode.

## Startup Decision Tree

At editor startup (`runMotiveEditorApp`):

1. If `--guest-mode` is set: run guest mode directly.
2. Else if auth API base URL is empty: run guest mode.
3. Else if no stored token exists: show login dialog.
4. Else: continue with stored credentials (no prompt).

Login dialog outcomes:

- `Accepted`: authenticated session.
- `Rejected` ("Continue as Guest"): guest mode session.

## Endpoint Contract

### 1) Email/password login

- `POST /auth/login`
- Request JSON:
  - `email: string`
  - `password: string`
- Success response JSON (expected):
  - `token: string`
  - `email: string`

### 2) Registration

- `POST /auth/register`
- Request JSON:
  - `email: string`
  - `password: string`
- Success response JSON (expected):
  - `token: string`
  - `email: string`

Client-side validation before register call:

- password confirmation must match
- password length >= 6

### 3) Current user lookup

- `GET /auth/whoami`
- Header: `Authorization: Bearer <token>`
- Used to recover `email` after OAuth callback when callback payload does not include email.

### 4) OAuth config discovery

- `GET /auth/supabase-config`
- Expected JSON fields:
  - `enabled: bool`
  - `supabase_url: string`
  - `desktop_redirect_base: string`

If enabled and fields are present, client builds OAuth URL via Supabase authorize endpoint.

### 5) OAuth fallback route

- `GET /oauth/{provider}?redirect=http://localhost:{port}/callback`
- Used when `/auth/supabase-config` does not return enabled config.

### 6) License lookup

- `GET /license`
- Header: `Authorization: Bearer <token>`
- Expected JSON (when active):
  - `active: bool`
  - `license_key: string`

If `active == true`, `license_key` is persisted.

## OAuth Desktop Flow

Providers currently wired in UI:

- `google`
- `github`

Flow:

1. Start local callback listener on `127.0.0.1:<ephemeral_port>`.
2. Open browser to computed OAuth URL.
3. Wait for callback (timeout: 180 seconds).
4. Parse token from callback URL query or fragment keys:
   - `token`, fallback `access_token`
5. Parse optional `email` from callback.
6. If email missing, call `/auth/whoami` with token.
7. Fetch `/license`.
8. Persist credentials.

Cancel and timeout both return non-authenticated state (user can retry or select guest).

## Persisted Credential Schema

Credentials are stored in `QStandardPaths::AppConfigLocation` as plain text single-line files:

- `auth_token.txt` -> bearer token
- `auth_email.txt` -> user email
- `license_key.txt` -> active license key (if any)

Helpers:

- `storedToken()`
- `storedEmail()`
- `storedLicenseKey()`
- `storeCredentials(token, email, licenseKey)`
- `clearCredentials()`

## Session Semantics

- Presence of a stored token is treated as signed-in at startup.
- Current implementation does not validate stored token freshness at startup.
- Guest mode is explicitly supported and does not require network availability.

## UI/CLI Surface

Login UI supports:

- Sign In
- Register
- Sign in with Google/GitHub
- Continue as Guest

CLI options:

- `--auth-api <url>`
- `--guest-mode`
- existing `--control-port <port>` remains unchanged

## Error Handling Model

For email/password requests:

- HTTP `401` -> "Invalid email or password."
- HTTP `409` -> "Email already registered. Try signing in."
- server `detail` field (if present) is surfaced
- otherwise generic connectivity error

OAuth failures surfaced as:

- local callback listener unavailable
- browser launch failure
- user canceled
- timeout
- missing token in callback

## Security Notes (Current State)

- Token and license are persisted unencrypted in app config files.
- Transport security depends on configured API URL (use HTTPS in production).
- No refresh-token flow is implemented in this client.

## Compatibility Target

This model intentionally mirrors QTSynth-side connectivity conventions:

- auth endpoints under `/auth/*`
- OAuth via `/auth/supabase-config` + `/oauth/{provider}` fallback
- `/license` lookup with bearer token
- local desktop callback browser flow
