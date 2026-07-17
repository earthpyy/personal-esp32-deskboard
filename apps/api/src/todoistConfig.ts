import fs from 'node:fs'
import path from 'node:path'
import { dataDir } from './settings.js'

export interface TodoistConfig {
  // id of the saved filter to display; '' = none picked yet. The name + query
  // are resolved live from Todoist per fetch so renames/edits propagate.
  // The API token lives in TODOIST_TOKEN (env / .env), like the Google secrets.
  activeFilterId: string
}

const DEFAULTS: TodoistConfig = { activeFilterId: '' }

const filePath = path.join(dataDir, 'todoist.json')

export function getTodoistConfig(): TodoistConfig {
  try {
    const parsed = JSON.parse(fs.readFileSync(filePath, 'utf8'))
    return { activeFilterId: String(parsed.activeFilterId ?? '') }
  } catch {
    return { ...DEFAULTS }
  }
}

export function saveTodoistConfig(patch: Partial<TodoistConfig>): TodoistConfig {
  const next = { ...getTodoistConfig(), ...patch }
  fs.mkdirSync(dataDir, { recursive: true })
  fs.writeFileSync(filePath, JSON.stringify(next, null, 2))
  return next
}
