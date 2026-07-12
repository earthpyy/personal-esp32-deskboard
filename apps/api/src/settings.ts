import fs from 'node:fs'
import path from 'node:path'

export interface Settings {
  timezone: string
  cacheTtlMinutes: number
}

const DEFAULTS: Settings = { timezone: 'Asia/Bangkok', cacheTtlMinutes: 5 }

export const dataDir = path.resolve(import.meta.dirname, '../data')
const settingsPath = path.join(dataDir, 'settings.json')

// Read on every access so admin-UI changes apply without restart.
export function getSettings(): Settings {
  try {
    return { ...DEFAULTS, ...JSON.parse(fs.readFileSync(settingsPath, 'utf8')) }
  } catch {
    return { ...DEFAULTS }
  }
}

export function saveSettings(patch: Partial<Settings>): Settings {
  const next = { ...getSettings(), ...patch }
  fs.mkdirSync(dataDir, { recursive: true })
  fs.writeFileSync(settingsPath, JSON.stringify(next, null, 2))
  return next
}
