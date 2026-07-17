import { TodoistApi } from '@doist/todoist-sdk'
import { getSettings } from './settings.js'
import { getTodoistConfig } from './todoistConfig.js'
import { hhmm, startOfDay } from './time.js'

// Hard cap so a huge filter can't blow up board memory; truncation is logged.
const MAX_TASKS = 40

// Todoist personal API token, from the environment (.env) like the Google
// secrets — not the admin UI. Empty/unset = the feature is unconfigured.
function todoistToken(): string {
  return process.env.TODOIST_TOKEN?.trim() ?? ''
}
const DAY_MS = 24 * 3600 * 1000

export interface TodoistFilter {
  id: string
  name: string
  query: string
}

// Normalized task as cached — due kept as an epoch anchor so the day-relative
// label ("Today", "Overdue") can be re-derived per request against a fresh now.
export interface CoreTask {
  id: string
  content: string
  priority: number // 1..4, 4 = highest (Todoist p1)
  project: string
  labels: string // "@a @b", '' when none
  dueMs?: number // epoch ms; undefined when the task has no due date
  dueTimed: boolean // true when the due carries a time-of-day
}

// A task as the board renders it verbatim.
export interface BoardTask {
  id: string
  content: string
  priority: number
  project: string
  labels: string
  due_label: string // pre-formatted in the configured tz, '' when undated
  overdue: boolean
}

export interface TodoistBoard {
  now: number
  now_label: string
  configured: boolean // token + a filter both set
  filter_name: string
  error: string // '' when ok; else a short reason ("no token", "http 401", …)
  tasks: BoardTask[]
}

export interface TodoistSyncResult {
  syncedAt: number
  ok: boolean
  error?: string
  filterName?: string
  taskCount?: number
}

interface CoreResult {
  tasks: CoreTask[]
  filterName: string
  error: string
}

// ── formatting / shaping (pure, unit-tested) ───────────────────────────────

function weekdayShort(epochMs: number, timezone: string): string {
  return new Intl.DateTimeFormat('en-US', { timeZone: timezone, weekday: 'short' }).format(epochMs)
}

function dayMonth(epochMs: number, timezone: string): string {
  return new Intl.DateTimeFormat('en-US', { timeZone: timezone, month: 'short', day: 'numeric' })
    .format(epochMs)
}

// Day-relative label for a due date. Adjacent days read as words; within a week
// as a weekday; further out as "Mon 21". Overdue is signalled separately (color).
function dayLabel(dueMs: number, timezone: string, nowMs: number): string {
  const diff = Math.round((startOfDay(dueMs, timezone) - startOfDay(nowMs, timezone)) / DAY_MS)
  if (diff === 0) return 'Today'
  if (diff === 1) return 'Tomorrow'
  if (diff === -1) return 'Yesterday'
  if (Math.abs(diff) < 7) return weekdayShort(dueMs, timezone)
  return dayMonth(dueMs, timezone)
}

export function formatDue(
  task: CoreTask,
  timezone: string,
  nowMs: number,
): { label: string; overdue: boolean } {
  if (task.dueMs === undefined) return { label: '', overdue: false }
  const day = dayLabel(task.dueMs, timezone, nowMs)
  if (!task.dueTimed) {
    // date-only: overdue once the whole day has passed
    const overdue = startOfDay(task.dueMs, timezone) < startOfDay(nowMs, timezone)
    return { label: day, overdue }
  }
  const time = hhmm(task.dueMs, timezone)
  const label = day === 'Today' ? time : `${day} ${time}`
  return { label, overdue: task.dueMs < nowMs }
}

// overdue/soonest-due first, undated last; ties broken by priority (desc).
function compareTasks(a: CoreTask, b: CoreTask): number {
  if (a.dueMs === undefined && b.dueMs === undefined) return b.priority - a.priority
  if (a.dueMs === undefined) return 1
  if (b.dueMs === undefined) return -1
  if (a.dueMs !== b.dueMs) return a.dueMs - b.dueMs
  return b.priority - a.priority
}

export function buildBoard(result: CoreResult, timezone: string, nowMs: number): TodoistBoard {
  const { activeFilterId } = getTodoistConfig()
  const sorted = [...result.tasks].sort(compareTasks).slice(0, MAX_TASKS)
  return {
    now: Math.floor(nowMs / 1000),
    now_label: hhmm(nowMs, timezone),
    configured: Boolean(todoistToken() && activeFilterId),
    filter_name: result.filterName,
    error: result.error,
    tasks: sorted.map((t) => {
      const { label, overdue } = formatDue(t, timezone, nowMs)
      return {
        id: t.id,
        content: t.content,
        priority: t.priority,
        project: t.project,
        labels: t.labels,
        due_label: label,
        overdue,
      }
    }),
  }
}

// ── Todoist fetching (SDK + one raw filters call) ──────────────────────────

interface RawDue {
  date: string
  datetime?: string | null
}

// Anchor a due date to an epoch. Timed dues parse directly; date-only dues are
// pinned to noon UTC so startOfDay() resolves them to the right local calendar
// day for any timezone within ±12h.
function dueToMs(due: RawDue | null): { dueMs?: number; dueTimed: boolean } {
  if (!due) return { dueMs: undefined, dueTimed: false }
  if (due.datetime) {
    const ms = Date.parse(due.datetime)
    if (!Number.isNaN(ms)) return { dueMs: ms, dueTimed: true }
  }
  const ms = Date.parse(`${due.date}T12:00:00Z`)
  return { dueMs: Number.isNaN(ms) ? undefined : ms, dueTimed: false }
}

export async function listFilters(token: string): Promise<TodoistFilter[]> {
  // Saved filters aren't in the SDK's task/project surface — they're a Sync API
  // resource, which the SDK exposes via sync(). (There is no REST /filters; it
  // 404s.) Full sync of just the filters resource; drop deleted, keep UI order.
  const res = await new TodoistApi(token).sync({ resourceTypes: ['filters'], syncToken: '*' })
  return (res.filters ?? [])
    .filter((f) => !f.isDeleted)
    .sort((a, b) => a.itemOrder - b.itemOrder)
    .map((f) => ({ id: f.id, name: f.name, query: f.query }))
}

async function projectNames(api: TodoistApi): Promise<Map<string, string>> {
  const names = new Map<string, string>()
  let cursor: string | null | undefined
  do {
    const res = await api.getProjects({ cursor, limit: 200 })
    for (const p of res.results) names.set(p.id, p.name)
    cursor = res.nextCursor
  } while (cursor)
  return names
}

async function tasksForFilter(api: TodoistApi, query: string): Promise<CoreTask[]> {
  const names = await projectNames(api)
  const out: CoreTask[] = []
  let cursor: string | null | undefined
  do {
    const res = await api.getTasksByFilter({ query, cursor, limit: 50 })
    for (const t of res.results) {
      out.push({
        id: t.id,
        content: t.content,
        priority: t.priority,
        project: names.get(t.projectId) ?? '',
        labels: t.labels.map((l) => `@${l}`).join(' '),
        ...dueToMs(t.due),
      })
    }
    cursor = res.nextCursor
  } while (cursor && out.length < MAX_TASKS)
  if (out.length > MAX_TASKS)
    console.warn(`todoist: filter returned ${out.length} tasks, showing first ${MAX_TASKS}`)
  return out
}

async function fetchCore(): Promise<CoreResult> {
  const token = todoistToken()
  const { activeFilterId } = getTodoistConfig()
  if (!token) return { tasks: [], filterName: '', error: 'no token' }
  if (!activeFilterId) return { tasks: [], filterName: '', error: 'no filter' }
  try {
    const filter = (await listFilters(token)).find((f) => f.id === activeFilterId)
    if (!filter) return { tasks: [], filterName: '', error: 'filter not found' }
    const api = new TodoistApi(token)
    return { tasks: await tasksForFilter(api, filter.query), filterName: filter.name, error: '' }
  } catch (err) {
    return { tasks: [], filterName: '', error: err instanceof Error ? err.message : String(err) }
  }
}

// ── cache + public surface (mirrors claude.ts) ─────────────────────────────

let cached: (CoreResult & { fetchedAt: number }) | null = null
let lastSync: TodoistSyncResult | null = null

export function todoistSyncResult(): TodoistSyncResult | null {
  return lastSync
}

// Hard failures (bad config, auth) replace the cache; transient ones (network,
// rate limit) keep the last-good task list so the board doesn't flap to empty.
function isHard(error: string): boolean {
  return error === 'no token' || error === 'no filter'
    || error === 'filter not found' || error === 'http 401'
}

export async function getTodoistBoard(force = false): Promise<TodoistBoard> {
  const { timezone, todoistCacheTtlMinutes } = getSettings()
  const nowMs = Date.now()
  const expired = !cached || nowMs - cached.fetchedAt > todoistCacheTtlMinutes * 60_000
  if (force || expired) {
    const fresh = await fetchCore()
    if (fresh.error === '' || isHard(fresh.error) || !cached)
      cached = { ...fresh, fetchedAt: nowMs }
    else cached = { ...cached, fetchedAt: nowMs } // transient: keep last-good tasks
    lastSync = {
      syncedAt: nowMs,
      ok: fresh.error === '',
      error: fresh.error || undefined,
      filterName: fresh.filterName || cached.filterName,
      taskCount: fresh.tasks.length,
    }
  }
  return buildBoard(cached!, timezone, nowMs)
}

export async function completeTodoistTask(id: string): Promise<boolean> {
  const token = todoistToken()
  if (!token) throw new Error('no token')
  const ok = await new TodoistApi(token).closeTask(id)
  await getTodoistBoard(true) // bust the cache so the next board poll drops it
  return ok
}
