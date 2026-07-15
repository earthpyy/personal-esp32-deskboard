import { execFileSync } from 'node:child_process'
import crypto from 'node:crypto'
import os from 'node:os'
import path from 'node:path'
import { type ClaudeAccountConfig, listClaudeAccounts } from './claudeAccounts.js'
import { getSettings } from './settings.js'
import { hhmm } from './time.js'

const USAGE_URL = 'https://api.anthropic.com/api/oauth/usage'
// Any `claude-code/<version>` UA works; without it the endpoint returns
// persistent 429s. The exact version is irrelevant.
const USER_AGENT = 'claude-code/2.0.0'

export type Severity = 'normal' | 'warning' | 'critical'

interface OAuthToken {
  accessToken: string
  expiresAt: number // epoch ms
}

function expandHome(p: string): string {
  const trimmed = p.trim()
  if (trimmed === '~') return os.homedir()
  if (trimmed.startsWith('~/')) return path.join(os.homedir(), trimmed.slice(2))
  return trimmed
}

// Claude Code stores each profile's Keychain item under a service name suffixed
// with the first 8 hex of sha256(CLAUDE_CONFIG_DIR); the account is the OS user.
// The default profile (no CLAUDE_CONFIG_DIR, i.e. empty configDir) uses the
// bare, unsuffixed service name.
function keychainService(configDir: string): string {
  const dir = expandHome(configDir)
  if (!dir) return 'Claude Code-credentials'
  const hash = crypto.createHash('sha256').update(dir).digest('hex').slice(0, 8)
  return `Claude Code-credentials-${hash}`
}

function parseToken(raw: string): OAuthToken | null {
  try {
    const o = JSON.parse(raw).claudeAiOauth
    if (!o?.accessToken) return null
    return { accessToken: o.accessToken, expiresAt: Number(o.expiresAt) || 0 }
  } catch {
    return null
  }
}

// Reads the live token from the login Keychain via the `security` CLI. Requires
// a one-time "Always Allow" grant per item; after that the read is silent. The
// timeout guards against a blocking GUI prompt if the grant is ever revoked.
function readToken(configDir: string): OAuthToken | null {
  try {
    const raw = execFileSync(
      '/usr/bin/security',
      ['find-generic-password', '-w', '-s', keychainService(configDir), '-a', os.userInfo().username],
      { encoding: 'utf8', timeout: 5000 },
    )
    return parseToken(raw)
  } catch {
    return null
  }
}

export interface UsageWindow {
  utilization: number
  // null while the window is idle — nothing has run in it, so no reset is scheduled
  resets_at: string | null
}

interface UsageLimit {
  kind: string
  severity: string
  percent: number
}

export interface UsageResponse {
  five_hour?: UsageWindow
  seven_day?: UsageWindow
  limits?: UsageLimit[]
}

export interface RawLimit {
  percent: number
  severity: Severity
  resetsAt?: number // epoch ms; absent while the window is idle
}

export interface RawAccount {
  id: string
  label: string
  available: boolean
  error?: string
  fiveHour?: RawLimit
  weekly?: RawLimit
}

function severityFromPercent(p: number): Severity {
  if (p >= 90) return 'critical'
  if (p >= 70) return 'warning'
  return 'normal'
}

// The top-level window carries the canonical utilization + reset; `limits[]`
// carries the server's severity for the same window (matched by kind).
export function mapWindow(
  data: UsageResponse,
  key: 'five_hour' | 'seven_day',
  kind: string,
): RawLimit | undefined {
  const window = data[key]
  if (!window) return undefined
  const percent = Math.round(window.utilization)
  const severity = (data.limits?.find((l) => l.kind === kind)?.severity as Severity)
    ?? severityFromPercent(percent)
  return { percent, severity, resetsAt: parseResetsAt(window.resets_at) }
}

function parseResetsAt(iso: string | null): number | undefined {
  if (!iso) return undefined
  const ms = Date.parse(iso)
  if (Number.isNaN(ms)) return undefined
  // resets land on HH:MM:59.9xx; round to the nearest minute so the label reads
  // "20:50", not "20:49"
  return Math.round(ms / 60_000) * 60_000
}

async function fetchAccountUsage(account: ClaudeAccountConfig): Promise<RawAccount> {
  const base = { id: account.id, label: account.alias }
  const token = readToken(account.configDir)
  if (!token) return { ...base, available: false, error: 'no token' }
  if (token.expiresAt && Date.now() > token.expiresAt)
    return { ...base, available: false, error: 'expired' }
  try {
    const res = await fetch(USAGE_URL, {
      headers: {
        Authorization: `Bearer ${token.accessToken}`,
        'anthropic-beta': 'oauth-2025-04-20',
        'anthropic-version': '2023-06-01',
        'Content-Type': 'application/json',
        'User-Agent': USER_AGENT,
      },
    })
    if (!res.ok)
      return { ...base, available: false, error: res.status === 401 ? 'expired' : `http ${res.status}` }
    const data = (await res.json()) as UsageResponse
    return {
      ...base,
      available: true,
      fiveHour: mapWindow(data, 'five_hour', 'session'),
      weekly: mapWindow(data, 'seven_day', 'weekly_all'),
    }
  } catch (err) {
    return { ...base, available: false, error: err instanceof Error ? err.message : String(err) }
  }
}

export interface ClaudeLimit {
  percent: number
  severity: Severity
  resets?: number // epoch seconds; absent while the window is idle
  resets_label: string // empty while the window is idle; the board renders it blank
}

export interface ClaudeAccount {
  label: string
  available: boolean
  error?: string
  five_hour?: ClaudeLimit
  weekly?: ClaudeLimit
}

export interface ClaudeUsage {
  now: number
  now_label: string
  accounts: ClaudeAccount[]
}

// Cached fetch result + the last available snapshot per account, so a transient
// upstream failure (429 / network) keeps showing the last good numbers instead
// of flapping to "unavailable". Only a hard auth failure clears them.
export interface ClaudeSyncResult {
  syncedAt: number
  accounts: { label: string; ok: boolean; error?: string }[]
}

let cached: { accounts: RawAccount[]; fetchedAt: number } | null = null
let lastSync: ClaudeSyncResult | null = null
const lastGood = new Map<string, RawAccount>()

export function claudeSyncResult(): ClaudeSyncResult | null {
  return lastSync
}

function resolve(fresh: RawAccount): RawAccount {
  if (fresh.available) {
    lastGood.set(fresh.id, fresh)
    return fresh
  }
  const hard = fresh.error === 'expired' || fresh.error === 'no token'
  const prev = lastGood.get(fresh.id)
  return !hard && prev ? prev : fresh
}

function weekdayTime(epochMs: number, timezone: string): string {
  const weekday = new Intl.DateTimeFormat('en-US', { timeZone: timezone, weekday: 'short' })
    .format(epochMs)
  return `${weekday} ${hhmm(epochMs, timezone)}`
}

function toLimit(limit: RawLimit, timezone: string, weekly: boolean): ClaudeLimit {
  const { resetsAt } = limit
  if (resetsAt === undefined)
    return { percent: limit.percent, severity: limit.severity, resets_label: '' }
  return {
    percent: limit.percent,
    severity: limit.severity,
    resets: Math.floor(resetsAt / 1000),
    resets_label: weekly ? weekdayTime(resetsAt, timezone) : hhmm(resetsAt, timezone),
  }
}

export function buildUsage(accounts: RawAccount[], timezone: string, nowMs: number): ClaudeUsage {
  return {
    now: Math.floor(nowMs / 1000),
    now_label: hhmm(nowMs, timezone),
    accounts: accounts.map((a) => ({
      label: a.label,
      available: a.available,
      error: a.error,
      five_hour: a.fiveHour && toLimit(a.fiveHour, timezone, false),
      weekly: a.weekly && toLimit(a.weekly, timezone, true),
    })),
  }
}

// Caches the upstream usage per TTL (the endpoint rate-limits aggressively), but
// re-derives `now`/reset labels per request so the board's clock anchor stays
// current — same split as the schedule builder.
export async function getClaudeUsage(force = false): Promise<ClaudeUsage> {
  const { timezone, claudeCacheTtlMinutes } = getSettings()
  const nowMs = Date.now()
  const expired = !cached || nowMs - cached.fetchedAt > claudeCacheTtlMinutes * 60_000
  if (force || expired) {
    const fresh = await Promise.all(listClaudeAccounts().map(fetchAccountUsage))
    cached = { accounts: fresh.map(resolve), fetchedAt: nowMs }
    // record the raw fetch outcome (not the last-good fallback) so the admin
    // sees whether this fetch actually reached the endpoint per account
    lastSync = {
      syncedAt: nowMs,
      accounts: fresh.map((a) => ({ label: a.label, ok: a.available, error: a.error })),
    }
  }
  return buildUsage(cached!.accounts, timezone, nowMs)
}
