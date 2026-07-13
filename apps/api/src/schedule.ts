import { dateLabel, hhmm, isoDate, startOfDay } from './time.js'

export interface SourceEvent {
  title: string
  start: { dateTime?: string; date?: string }
  end: { dateTime?: string; date?: string }
  declined: boolean
  color: string
  calendar: string
}

export interface ScheduleAllDay {
  title: string
  calendar: string
  color: string
}

export interface ScheduleEvent extends ScheduleAllDay {
  time: string
  start: number // epoch seconds, clamped to today
  end: number
}

export interface LunchConfig {
  start: string // "HH:MM" local time
  end: string // "HH:MM" local time
}

export interface Schedule {
  now: number
  now_label: string
  date: string
  warnings: number
  all_day: ScheduleAllDay[]
  events: ScheduleEvent[]
  // a subtle "lunch break" divider the board draws at `start` (epoch seconds),
  // shown only when the configured window is free of timed events
  lunch: { show: boolean; start: number }
}

export const MAX_TIMED = 30
export const MAX_ALL_DAY = 10

const DAY_MS = 24 * 3600 * 1000

// minutes past local midnight for a "HH:MM" string, or null if malformed
function parseHhmm(s: string): number | null {
  const m = /^(\d{1,2}):(\d{2})$/.exec(s?.trim() ?? '')
  if (!m) return null
  const h = Number(m[1])
  const min = Number(m[2])
  if (h > 23 || min > 59) return null
  return h * 60 + min
}

export function buildSchedule(
  sources: { events: SourceEvent[]; failed?: boolean }[],
  timezone: string,
  nowMs: number,
  lunch?: LunchConfig | null,
): Schedule {
  const dayStart = startOfDay(nowMs, timezone)
  const dayEnd = dayStart + DAY_MS
  const today = isoDate(nowMs, timezone)

  const allDay: ScheduleAllDay[] = []
  const timed: ScheduleEvent[] = []

  for (const source of sources) {
    for (const ev of source.events) {
      if (ev.declined) continue
      if (ev.start.date && ev.end.date) {
        // all-day: start date inclusive, end date exclusive
        if (ev.start.date <= today && today < ev.end.date)
          allDay.push({ title: ev.title, calendar: ev.calendar, color: ev.color })
        continue
      }
      if (!ev.start.dateTime || !ev.end.dateTime) continue
      const start = Date.parse(ev.start.dateTime)
      const end = Date.parse(ev.end.dateTime)
      if (end <= dayStart || start >= dayEnd) continue
      const clampedStart = Math.max(start, dayStart)
      const clampedEnd = Math.min(end, dayEnd)
      timed.push({
        title: ev.title,
        calendar: ev.calendar,
        color: ev.color,
        time: `${hhmm(clampedStart, timezone)} - ${hhmm(clampedEnd, timezone)}`,
        start: Math.floor(clampedStart / 1000),
        end: Math.floor(clampedEnd / 1000),
      })
    }
  }

  timed.sort((a, b) => a.start - b.start || a.end - b.end)

  const shown = timed.slice(0, MAX_TIMED)

  // lunch divider: only when enabled, the window is valid, at least one timed
  // event exists, and nothing overlaps [start, end) — i.e. lunch is actually free
  let lunchShow = false
  let lunchStartSec = 0
  const lunchStartMin = lunch ? parseHhmm(lunch.start) : null
  const lunchEndMin = lunch ? parseHhmm(lunch.end) : null
  if (lunchStartMin != null && lunchEndMin != null && lunchEndMin > lunchStartMin) {
    const lunchStartMs = dayStart + lunchStartMin * 60_000
    const lunchEndMs = dayStart + lunchEndMin * 60_000
    const occupied = shown.some((e) => e.start * 1000 < lunchEndMs && e.end * 1000 > lunchStartMs)
    lunchShow = shown.length > 0 && !occupied
    lunchStartSec = Math.floor(lunchStartMs / 1000)
  }

  return {
    now: Math.floor(nowMs / 1000),
    now_label: hhmm(nowMs, timezone),
    date: dateLabel(nowMs, timezone),
    warnings: sources.filter((s) => s.failed).length,
    all_day: allDay.slice(0, MAX_ALL_DAY),
    events: shown,
    lunch: { show: lunchShow, start: lunchStartSec },
  }
}
