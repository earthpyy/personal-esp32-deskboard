import { listAccounts } from './accounts.js'
import { type AccountFetchResult, type CalendarInfo, fetchAccountEvents } from './google.js'
import { buildSchedule, type Schedule } from './schedule.js'
import { getSettings } from './settings.js'
import { startOfDay } from './time.js'

export interface SyncResult {
  syncedAt: number
  accounts: { email: string; ok: boolean; error?: string }[]
}

let cached: { results: AccountFetchResult[]; fetchedAt: number; dayStart: number } | null = null
let lastSync: SyncResult | null = null

// Caches the fetched source events, but rebuilds the schedule on every request
// with the current time: the board anchors its clock to `now`, so it must
// never be stale. Refetches on TTL expiry or day rollover.
export async function getSchedule(force = false): Promise<Schedule> {
  const { timezone, cacheTtlMinutes, lunchEnabled, lunchStart, lunchEnd } = getSettings()
  const nowMs = Date.now()
  const dayStart = startOfDay(nowMs, timezone)
  const expired = !cached
    || nowMs - cached.fetchedAt > cacheTtlMinutes * 60_000
    || cached.dayStart !== dayStart
  if (force || expired) {
    const results = await Promise.all(
      listAccounts()
        .filter((a) => a.status === 'ok')
        .map((a) => fetchAccountEvents(a, dayStart, dayStart + 24 * 3600 * 1000)),
    )
    cached = { results, fetchedAt: nowMs, dayStart }
    lastSync = {
      syncedAt: nowMs,
      accounts: results.map((r) => ({ email: r.email, ok: !r.error, error: r.error })),
    }
  }
  return buildSchedule(
    cached!.results.map((r) => ({ events: r.events, failed: Boolean(r.error) })),
    timezone,
    nowMs,
    lunchEnabled ? { start: lunchStart, end: lunchEnd } : null,
  )
}

export function lastSyncResult(): SyncResult | null {
  return lastSync
}

export function accountCalendarCounts(): Map<
  string,
  { calendarCount: number; calendars: CalendarInfo[]; error?: string }
> {
  return new Map(
    (cached?.results ?? []).map((r) => [
      r.email,
      { calendarCount: r.calendarCount, calendars: r.calendars, error: r.error },
    ]),
  )
}
