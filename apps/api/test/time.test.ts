import assert from 'node:assert/strict'
import { test } from 'node:test'
import { dateLabel, hhmm, isoDate, startOfDay } from '../src/time.js'

const TZ = 'Asia/Bangkok'
const NOON = Date.parse('2026-07-13T12:00:00+07:00')

test('startOfDay returns local midnight in the configured timezone', () => {
  assert.equal(startOfDay(NOON, TZ), Date.parse('2026-07-13T00:00:00+07:00'))
})

test('startOfDay just after midnight stays on the same day', () => {
  const t = Date.parse('2026-07-13T00:05:00+07:00')
  assert.equal(startOfDay(t, TZ), Date.parse('2026-07-13T00:00:00+07:00'))
})

test('hhmm formats zero-padded local time', () => {
  assert.equal(hhmm(Date.parse('2026-07-13T09:05:00+07:00'), TZ), '09:05')
  assert.equal(hhmm(Date.parse('2026-07-13T00:00:00+07:00'), TZ), '00:00')
})

test('dateLabel and isoDate', () => {
  assert.equal(dateLabel(NOON, TZ), 'Mon, Jul 13')
  assert.equal(isoDate(NOON, TZ), '2026-07-13')
})
