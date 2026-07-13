import fs from 'node:fs'
import path from 'node:path'
import { node } from '@elysiajs/node'
import { staticPlugin } from '@elysiajs/static'
import { Elysia, file, redirect, t } from 'elysia'
import { deleteAccount, listAccounts, saveAccount } from './accounts.js'
import { accountCalendarCounts, getSchedule, lastSyncResult } from './cache.js'
import { OAUTH_SCOPES, emailForCode, oauthClient } from './google.js'
import { getSettings, saveSettings } from './settings.js'

// optional .env next to package.json (see .env.example); real env vars still win
const envPath = path.resolve(import.meta.dirname, '../.env')
if (fs.existsSync(envPath)) process.loadEnvFile(envPath)

const adminDist = path.resolve(import.meta.dirname, '../../admin/dist')
const adminIndex = path.join(adminDist, 'index.html')

const boardStatus: { lastPollAt: number | null; lastPollIp: string | null } = {
  lastPollAt: null,
  lastPollIp: null,
}

function redirectUriFrom(request: Request): string {
  return `${new URL(request.url).origin}/api/oauth/callback`
}

const app = new Elysia({ adapter: node() })
  .get('/health', () => ({ status: 'ok' }))
  .get('/schedule', ({ request }) => {
    boardStatus.lastPollAt = Date.now()
    // the board reports its own IP; the node adapter can't see the socket address
    boardStatus.lastPollIp = request.headers.get('x-board-ip')
      ?? request.headers.get('x-forwarded-for')
      ?? 'unknown'
    return getSchedule()
  })
  .get('/api/accounts', () => {
    const counts = accountCalendarCounts()
    return listAccounts().map(({ email, status }) => ({
      email,
      status,
      calendars: counts.get(email)?.calendarCount ?? null,
      error: counts.get(email)?.error ?? null,
    }))
  })
  .delete('/api/accounts/:email', ({ params }) => {
    deleteAccount(decodeURIComponent(params.email))
    return { ok: true }
  })
  .get('/api/oauth/start', ({ request }) =>
    redirect(oauthClient(redirectUriFrom(request)).generateAuthUrl({
      access_type: 'offline',
      prompt: 'consent',
      scope: OAUTH_SCOPES,
    })))
  .get('/api/oauth/callback', async ({ request, query }) => {
    if (!query.code) return new Response('Missing ?code', { status: 400 })
    const { email, refreshToken } = await emailForCode(
      oauthClient(redirectUriFrom(request)), query.code,
    )
    saveAccount({ email, refreshToken, status: 'ok' })
    await getSchedule(true) // warm the cache so the new account shows up immediately
    return redirect('/admin/')
  })
  .post('/api/sync', async () => {
    await getSchedule(true)
    return lastSyncResult()
  })
  .get('/api/settings', () => getSettings())
  .put('/api/settings', ({ body }) => saveSettings(body), {
    body: t.Partial(t.Object({
      timezone: t.String(),
      cacheTtlMinutes: t.Number({ minimum: 1 }),
    })),
  })
  .get('/api/board-status', () => ({ ...boardStatus, lastSync: lastSyncResult() }))
  .get('/admin', () =>
    fs.existsSync(adminIndex)
      ? file(adminIndex)
      : new Response('Admin UI not built. Run: pnpm --filter admin build', { status: 503 }))

if (fs.existsSync(adminDist)) {
  app.use(staticPlugin({ assets: adminDist, prefix: '/admin' }))
}

app.listen(3000, ({ hostname, port }) => {
  console.log(`API running at http://${hostname}:${port}`)
})
