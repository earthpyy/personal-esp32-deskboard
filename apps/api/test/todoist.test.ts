import assert from 'node:assert/strict'
import { test } from 'node:test'
import { type CoreTask, buildBoard, formatDue } from '../src/todoist.js'

const TZ = 'Asia/Bangkok'
const NOW = Date.parse('2026-07-15T10:37:00+07:00')

function task(over: Partial<CoreTask> = {}): CoreTask {
  return {
    id: '1',
    content: 'A task',
    priority: 1,
    project: 'Inbox',
    labels: '',
    dueTimed: false,
    ...over,
  }
}

test('formatDue labels an undated task as blank, never overdue', () => {
  assert.deepEqual(formatDue(task(), TZ, NOW), { label: '', overdue: false })
})

test('formatDue: a date-only due today reads "Today" and is not overdue', () => {
  const t = task({ dueMs: Date.parse('2026-07-15T12:00:00Z'), dueTimed: false })
  assert.deepEqual(formatDue(t, TZ, NOW), { label: 'Today', overdue: false })
})

test('formatDue: a past date-only due is overdue', () => {
  const t = task({ dueMs: Date.parse('2026-07-14T12:00:00Z'), dueTimed: false })
  const { label, overdue } = formatDue(t, TZ, NOW)
  assert.equal(label, 'Yesterday')
  assert.equal(overdue, true)
})

test('formatDue: a timed due today shows just the time', () => {
  const t = task({ dueMs: Date.parse('2026-07-15T18:00:00+07:00'), dueTimed: true })
  assert.deepEqual(formatDue(t, TZ, NOW), { label: '18:00', overdue: false })
})

test('formatDue: a timed due earlier today is overdue', () => {
  const t = task({ dueMs: Date.parse('2026-07-15T09:00:00+07:00'), dueTimed: true })
  const { label, overdue } = formatDue(t, TZ, NOW)
  assert.equal(label, '09:00')
  assert.equal(overdue, true)
})

test('formatDue: a timed due tomorrow prefixes the day', () => {
  const t = task({ dueMs: Date.parse('2026-07-16T09:00:00+07:00'), dueTimed: true })
  assert.equal(formatDue(t, TZ, NOW).label, 'Tomorrow 09:00')
})

test('buildBoard sorts overdue/soonest first, undated last, priority breaks ties', () => {
  const tasks: CoreTask[] = [
    task({ id: 'undated', priority: 4 }),
    task({ id: 'tomorrow', dueMs: Date.parse('2026-07-16T09:00:00+07:00'), dueTimed: true }),
    task({ id: 'overdue', dueMs: Date.parse('2026-07-14T09:00:00+07:00'), dueTimed: true }),
    task({ id: 'today-p1', dueMs: Date.parse('2026-07-15T20:00:00+07:00'), dueTimed: true, priority: 4 }),
    task({ id: 'today-p4', dueMs: Date.parse('2026-07-15T20:00:00+07:00'), dueTimed: true, priority: 1 }),
  ]
  const board = buildBoard({ tasks, filterName: 'Work', error: '' }, TZ, NOW)
  assert.deepEqual(
    board.tasks.map((t) => t.id),
    ['overdue', 'today-p1', 'today-p4', 'tomorrow', 'undated'],
  )
})

test('buildBoard passes through now_label, filter name and error', () => {
  const board = buildBoard({ tasks: [], filterName: 'Errands', error: 'http 429' }, TZ, NOW)
  assert.equal(board.now_label, '10:37')
  assert.equal(board.filter_name, 'Errands')
  assert.equal(board.error, 'http 429')
  assert.equal(board.now, Math.floor(NOW / 1000))
})
