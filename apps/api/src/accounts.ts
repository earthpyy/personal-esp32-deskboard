import fs from 'node:fs'
import path from 'node:path'
import { dataDir } from './settings.js'

export type AccountStatus = 'ok' | 'needs-reconnect'

export interface Account {
  email: string
  refreshToken: string
  status: AccountStatus
  // optional display name shown on the board in place of the email-named
  // primary calendar; absent/empty falls back to the email
  alias?: string
}

const accountsDir = path.join(dataDir, 'accounts')

function fileFor(email: string): string {
  return path.join(accountsDir, `${email.replace(/[^a-zA-Z0-9.@_-]/g, '_')}.json`)
}

export function listAccounts(): Account[] {
  try {
    return fs.readdirSync(accountsDir)
      .filter((f) => f.endsWith('.json'))
      .map((f) => JSON.parse(fs.readFileSync(path.join(accountsDir, f), 'utf8')) as Account)
  } catch {
    return []
  }
}

export function saveAccount(account: Account): void {
  fs.mkdirSync(accountsDir, { recursive: true })
  fs.writeFileSync(fileFor(account.email), JSON.stringify(account, null, 2))
}

export function deleteAccount(email: string): void {
  fs.rmSync(fileFor(email), { force: true })
}

export function markNeedsReconnect(email: string): void {
  const account = listAccounts().find((a) => a.email === email)
  if (account) saveAccount({ ...account, status: 'needs-reconnect' })
}
