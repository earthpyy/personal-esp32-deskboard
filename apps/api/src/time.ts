const fmtCache = new Map<string, Intl.DateTimeFormat>()

function formatter(timeZone: string): Intl.DateTimeFormat {
  let f = fmtCache.get(timeZone)
  if (!f) {
    f = new Intl.DateTimeFormat('en-US', {
      timeZone,
      year: 'numeric',
      month: 'numeric',
      day: 'numeric',
      hour: 'numeric',
      minute: 'numeric',
      second: 'numeric',
      hour12: false,
    })
    fmtCache.set(timeZone, f)
  }
  return f
}

interface ZonedParts {
  y: number
  m: number
  d: number
  h: number
  min: number
  s: number
}

function zonedParts(epochMs: number, timeZone: string): ZonedParts {
  const parts = Object.fromEntries(
    formatter(timeZone).formatToParts(epochMs).map((p) => [p.type, p.value]),
  )
  return {
    y: Number(parts.year),
    m: Number(parts.month),
    d: Number(parts.day),
    h: Number(parts.hour) % 24,
    min: Number(parts.minute),
    s: Number(parts.second),
  }
}

/** Epoch ms of local midnight of the day containing epochMs, in timeZone. */
export function startOfDay(epochMs: number, timeZone: string): number {
  const { y, m, d } = zonedParts(epochMs, timeZone)
  let ts = Date.UTC(y, m - 1, d)
  // correct the UTC guess by the offset Intl reports; two passes settle DST edges
  for (let i = 0; i < 2; i++) {
    const p = zonedParts(ts, timeZone)
    ts += Date.UTC(y, m - 1, d) - Date.UTC(p.y, p.m - 1, p.d, p.h, p.min, p.s)
  }
  return ts
}

export function hhmm(epochMs: number, timeZone: string): string {
  const { h, min } = zonedParts(epochMs, timeZone)
  return `${String(h).padStart(2, '0')}:${String(min).padStart(2, '0')}`
}

export function dateLabel(epochMs: number, timeZone: string): string {
  return new Intl.DateTimeFormat('en-US', {
    timeZone,
    weekday: 'short',
    month: 'short',
    day: 'numeric',
  }).format(epochMs)
}

export function isoDate(epochMs: number, timeZone: string): string {
  const { y, m, d } = zonedParts(epochMs, timeZone)
  return `${y}-${String(m).padStart(2, '0')}-${String(d).padStart(2, '0')}`
}
