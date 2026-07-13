import crypto from 'node:crypto'
import fs from 'node:fs'
import path from 'node:path'
import { dataDir } from './settings.js'

export interface ClaudeAccountConfig {
  id: string
  alias: string
  // CLAUDE_CONFIG_DIR of the profile; '' targets the default (~/.claude)
  // profile, a '~'-prefixed or absolute path targets a specific one
  configDir: string
}

const filePath = path.join(dataDir, 'claude-accounts.json')

export function listClaudeAccounts(): ClaudeAccountConfig[] {
  try {
    return JSON.parse(fs.readFileSync(filePath, 'utf8')) as ClaudeAccountConfig[]
  } catch {
    return []
  }
}

function writeAll(accounts: ClaudeAccountConfig[]): void {
  fs.mkdirSync(dataDir, { recursive: true })
  fs.writeFileSync(filePath, JSON.stringify(accounts, null, 2))
}

export function addClaudeAccount(input: { alias: string; configDir: string }): ClaudeAccountConfig {
  const account: ClaudeAccountConfig = { id: crypto.randomUUID(), ...input }
  writeAll([...listClaudeAccounts(), account])
  return account
}

export function updateClaudeAccount(
  id: string,
  patch: { alias?: string; configDir?: string },
): ClaudeAccountConfig | null {
  const accounts = listClaudeAccounts()
  const idx = accounts.findIndex((a) => a.id === id)
  if (idx < 0) return null
  accounts[idx] = { ...accounts[idx], ...patch }
  writeAll(accounts)
  return accounts[idx]
}

export function deleteClaudeAccount(id: string): void {
  writeAll(listClaudeAccounts().filter((a) => a.id !== id))
}
