import fs from 'node:fs'
import path from 'node:path'
import { node } from '@elysiajs/node'
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
    return listAccounts().map(({ email, status, alias }) => ({
      email,
      alias: alias ?? null,
      status,
      calendars: counts.get(email)?.calendarCount ?? null,
      calendarList: counts.get(email)?.calendars ?? null,
      error: counts.get(email)?.error ?? null,
    }))
  })
  .patch('/api/accounts/:email', async ({ params, body }) => {
    const email = decodeURIComponent(params.email)
    const account = listAccounts().find((a) => a.email === email)
    if (!account) return new Response('Account not found', { status: 404 })
    const alias = body.alias.trim()
    saveAccount({ ...account, alias: alias || undefined })
    await getSchedule(true) // bust the cache so the board relabels on next poll
    return { ok: true }
  }, {
    body: t.Object({ alias: t.String() }),
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
    // Response.redirect (behind Elysia's redirect()) rejects relative URLs on Node
    return new Response(null, { status: 302, headers: { location: '/admin/' } })
  })
  .get('/api/sync', () => lastSyncResult())
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
  .get('/api/board-status', () => boardStatus)
  .get('/admin', () =>
    fs.existsSync(adminIndex)
      ? file(adminIndex)
      : new Response('Admin UI not built. Run: pnpm --filter admin build', { status: 503 }))
  // serve built assets via file(), which sets Content-Type; @elysiajs/static
  // returns an empty MIME on the node adapter and browsers reject the module
  .get('/admin/*', ({ params }) => {
    const target = path.resolve(adminDist, params['*'])
    if (!target.startsWith(adminDist + path.sep) || !fs.existsSync(target))
      return new Response('Not found', { status: 404 })
    return file(target)
  })

app.listen(3000, ({ hostname, port }) => {
  console.log(`API running at http://${hostname}:${port}`)
})
