import assert from 'node:assert/strict'
import { test } from 'node:test'
import { buildUsage, mapWindow, type RawAccount, type UsageResponse } from '../src/claude.js'

const TZ = 'Asia/Bangkok'
const NOW = Date.parse('2026-07-15T10:37:00+07:00')

// Shape returned by GET /api/oauth/usage. An idle window (nothing run in it)
// reports `resets_at: null` — there is no reset scheduled yet.
function response(fiveHourResetsAt: string | null): UsageResponse {
  return {
    five_hour: { utilization: 0, resets_at: fiveHourResetsAt },
    seven_day: { utilization: 5, resets_at: '2026-07-21T00:59:59.840643+00:00' },
    limits: [
      { kind: 'session', severity: 'normal', percent: 0 },
      { kind: 'weekly_all', severity: 'normal', percent: 5 },
    ],
  }
}

function account(data: UsageResponse): RawAccount {
  return {
    id: 'cdm',
    label: 'CDM',
    available: true,
    fiveHour: mapWindow(data, 'five_hour', 'session'),
    weekly: mapWindow(data, 'seven_day', 'weekly_all'),
  }
}

test('mapWindow keeps the percent when the window has no reset time', () => {
  const limit = mapWindow(response(null), 'five_hour', 'session')
  assert.equal(limit?.percent, 0)
  assert.equal(limit?.severity, 'normal')
  assert.equal(limit?.resetsAt, undefined)
})

test('mapWindow rounds a present reset to the nearest minute', () => {
  const limit = mapWindow(response('2026-07-15T08:29:59.372916+00:00'), 'five_hour', 'session')
  assert.equal(limit?.resetsAt, Date.parse('2026-07-15T08:30:00+00:00'))
})

test('buildUsage renders a null reset as a blank label instead of throwing', () => {
  const usage = buildUsage([account(response(null))], TZ, NOW)
  const five = usage.accounts[0]?.five_hour
  assert.equal(five?.percent, 0)
  assert.equal(five?.severity, 'normal')
  assert.equal(five?.resets_label, '')
  assert.equal(five?.resets, undefined)
})

test('buildUsage still labels windows that do have a reset', () => {
  const usage = buildUsage([account(response(null))], TZ, NOW)
  const weekly = usage.accounts[0]?.weekly
  assert.equal(weekly?.percent, 5)
  // seven_day resets 2026-07-21T00:59:59Z -> rounds to 01:00Z -> 08:00 Bangkok, a Tuesday
  assert.equal(weekly?.resets_label, 'Tue 08:00')
  assert.equal(weekly?.resets, Date.parse('2026-07-21T01:00:00+00:00') / 1000)
})

test('buildUsage anchors the board clock to now', () => {
  const usage = buildUsage([account(response(null))], TZ, NOW)
  assert.equal(usage.now, Math.floor(NOW / 1000))
  assert.equal(usage.now_label, '10:37')
})
