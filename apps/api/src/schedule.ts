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

export interface Schedule {
  now: number
  now_label: string
  date: string
  warnings: number
  all_day: ScheduleAllDay[]
  events: ScheduleEvent[]
}

export const MAX_TIMED = 30
export const MAX_ALL_DAY = 10

const DAY_MS = 24 * 3600 * 1000

export function buildSchedule(
  sources: { events: SourceEvent[]; failed?: boolean }[],
  timezone: string,
  nowMs: number,
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

  return {
    now: Math.floor(nowMs / 1000),
    now_label: hhmm(nowMs, timezone),
    date: dateLabel(nowMs, timezone),
    warnings: sources.filter((s) => s.failed).length,
    all_day: allDay.slice(0, MAX_ALL_DAY),
    events: timed.slice(0, MAX_TIMED),
  }
}
