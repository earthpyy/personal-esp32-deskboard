import type { OAuth2Client } from 'google-auth-library'
import { google } from 'googleapis'
import { type Account, markNeedsReconnect } from './accounts.js'
import type { SourceEvent } from './schedule.js'

// non-meeting event types Google surfaces (working-location pills, OOO/focus
// blocks); the board only wants actual events, so drop them at fetch time
const HIDDEN_EVENT_TYPES = new Set(['workingLocation', 'outOfOffice', 'focusTime'])

export const OAUTH_SCOPES = [
  'https://www.googleapis.com/auth/calendar.readonly',
  'https://www.googleapis.com/auth/userinfo.email',
]

export function oauthClient(redirectUri?: string): OAuth2Client {
  return new google.auth.OAuth2(
    process.env.GOOGLE_CLIENT_ID,
    process.env.GOOGLE_CLIENT_SECRET,
    redirectUri,
  )
}

export async function emailForCode(
  client: OAuth2Client,
  code: string,
): Promise<{ email: string; refreshToken: string }> {
  const { tokens } = await client.getToken(code)
  client.setCredentials(tokens)
  const { data } = await google.oauth2({ version: 'v2', auth: client }).userinfo.get()
  if (!data.email || !tokens.refresh_token)
    throw new Error('Google did not return an email and refresh token')
  return { email: data.email, refreshToken: tokens.refresh_token }
}

export interface AccountFetchResult {
  email: string
  calendarCount: number
  events: SourceEvent[]
  error?: string
}

export async function fetchAccountEvents(
  account: Account,
  timeMinMs: number,
  timeMaxMs: number,
): Promise<AccountFetchResult> {
  const auth = oauthClient()
  auth.setCredentials({ refresh_token: account.refreshToken })
  const calendar = google.calendar({ version: 'v3', auth })
  try {
    const [{ data: list }, { data: palette }] = await Promise.all([
      calendar.calendarList.list({ minAccessRole: 'reader' }),
      calendar.colors.get({}),
    ])
    // mirror the "ticked" calendars in the Google Calendar UI
    const selected = (list.items ?? []).filter((c) => c.selected)
    // the primary calendar is named after the email; show the alias instead
    const calendarLabel = (cal: (typeof selected)[number]) =>
      cal.primary && account.alias
        ? account.alias
        : (cal.summaryOverride ?? cal.summary ?? 'Calendar')
    const events: SourceEvent[] = []
    await Promise.all(selected.map(async (cal) => {
      const { data } = await calendar.events.list({
        calendarId: cal.id!,
        timeMin: new Date(timeMinMs).toISOString(),
        timeMax: new Date(timeMaxMs).toISOString(),
        singleEvents: true,
        orderBy: 'startTime',
        maxResults: 50,
      })
      const calendarName = calendarLabel(cal)
      for (const ev of data.items ?? []) {
        if (ev.status === 'cancelled') continue
        if (ev.eventType && HIDDEN_EVENT_TYPES.has(ev.eventType)) continue
        const self = ev.attendees?.find((a) => a.self)
        events.push({
          title: ev.summary ?? '(untitled)',
          start: { dateTime: ev.start?.dateTime ?? undefined, date: ev.start?.date ?? undefined },
          end: { dateTime: ev.end?.dateTime ?? undefined, date: ev.end?.date ?? undefined },
          declined: self?.responseStatus === 'declined',
          color: (ev.colorId && palette.event?.[ev.colorId]?.background) || cal.backgroundColor || '#888888',
          calendar: calendarName,
        })
      }
    }))
    return { email: account.email, calendarCount: selected.length, events }
  } catch (err) {
    const message = err instanceof Error ? err.message : String(err)
    if (message.includes('invalid_grant')) markNeedsReconnect(account.email)
    return { email: account.email, calendarCount: 0, events: [], error: message }
  }
}
