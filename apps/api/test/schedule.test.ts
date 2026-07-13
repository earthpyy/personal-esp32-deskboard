import assert from 'node:assert/strict'
import { test } from 'node:test'
import { buildSchedule, type SourceEvent } from '../src/schedule.js'

const TZ = 'Asia/Bangkok'
const NOW = Date.parse('2026-07-13T12:00:00+07:00')

function timed(title: string, startIso: string, endIso: string, extra: Partial<SourceEvent> = {}): SourceEvent {
  return {
    title,
    start: { dateTime: startIso },
    end: { dateTime: endIso },
    declined: false,
    color: '#039BE5',
    calendar: 'Work',
    ...extra,
  }
}

function allDay(title: string, startDate: string, endDate: string): SourceEvent {
  return {
    title,
    start: { date: startDate },
    end: { date: endDate },
    declined: false,
    color: '#D50000',
    calendar: 'Family',
  }
}

test('merges accounts and sorts by start time', () => {
  const s = buildSchedule([
    { events: [timed('B', '2026-07-13T14:00:00+07:00', '2026-07-13T15:00:00+07:00')] },
    { events: [timed('A', '2026-07-13T09:30:00+07:00', '2026-07-13T10:00:00+07:00')] },
  ], TZ, NOW)
  assert.deepEqual(s.events.map((e) => e.title), ['A', 'B'])
  assert.equal(s.events[0].time, '09:30 - 10:00')
  assert.equal(s.events[0].start, Date.parse('2026-07-13T09:30:00+07:00') / 1000)
})

test('drops declined events', () => {
  const s = buildSchedule([{
    events: [timed('X', '2026-07-13T09:00:00+07:00', '2026-07-13T10:00:00+07:00', { declined: true })],
  }], TZ, NOW)
  assert.equal(s.events.length, 0)
})

test('all-day events use inclusive-start exclusive-end date range', () => {
  const s = buildSchedule([{
    events: [
      allDay('today', '2026-07-13', '2026-07-14'),
      allDay('multi spanning', '2026-07-10', '2026-07-15'),
      allDay('ended yesterday', '2026-07-12', '2026-07-13'),
    ],
  }], TZ, NOW)
  assert.deepEqual(s.all_day.map((e) => e.title), ['today', 'multi spanning'])
})

test('clamps multi-day timed events to today and drops non-overlapping ones', () => {
  const s = buildSchedule([{
    events: [
      timed('ends today', '2026-07-12T20:00:00+07:00', '2026-07-13T14:00:00+07:00'),
      timed('tomorrow', '2026-07-14T09:00:00+07:00', '2026-07-14T10:00:00+07:00'),
    ],
  }], TZ, NOW)
  assert.equal(s.events.length, 1)
  assert.equal(s.events[0].time, '00:00 - 14:00')
  assert.equal(s.events[0].start, Date.parse('2026-07-13T00:00:00+07:00') / 1000)
})

const LUNCH = { start: '12:00', end: '13:00' }

test('lunch divider shows when the window is free', () => {
  const s = buildSchedule([{
    events: [
      timed('morning', '2026-07-13T09:00:00+07:00', '2026-07-13T10:00:00+07:00'),
      timed('afternoon', '2026-07-13T14:00:00+07:00', '2026-07-13T15:00:00+07:00'),
    ],
  }], TZ, NOW, LUNCH)
  assert.equal(s.lunch.show, true)
  assert.equal(s.lunch.start, Date.parse('2026-07-13T12:00:00+07:00') / 1000)
})

test('lunch divider hides when an event overlaps the window', () => {
  const s = buildSchedule([{
    events: [timed('lunch mtg', '2026-07-13T12:30:00+07:00', '2026-07-13T13:30:00+07:00')],
  }], TZ, NOW, LUNCH)
  assert.equal(s.lunch.show, false)
})

test('lunch divider hides with no timed events or when disabled', () => {
  const empty = buildSchedule([{ events: [] }], TZ, NOW, LUNCH)
  assert.equal(empty.lunch.show, false)
  const disabled = buildSchedule([{
    events: [timed('m', '2026-07-13T09:00:00+07:00', '2026-07-13T10:00:00+07:00')],
  }], TZ, NOW, null)
  assert.equal(disabled.lunch.show, false)
})

test('events touching the lunch boundary do not block it', () => {
  const s = buildSchedule([{
    events: [
      timed('ends at noon', '2026-07-13T11:00:00+07:00', '2026-07-13T12:00:00+07:00'),
      timed('starts at 1', '2026-07-13T13:00:00+07:00', '2026-07-13T14:00:00+07:00'),
    ],
  }], TZ, NOW, LUNCH)
  assert.equal(s.lunch.show, true)
})

test('caps output and counts failed accounts in warnings', () => {
  const many = Array.from({ length: 40 }, (_, i) =>
    timed(`e${i}`, `2026-07-13T0${i % 8}:0${i % 6}:00+07:00`, '2026-07-13T09:00:00+07:00'))
  const s = buildSchedule([{ events: many }, { events: [], failed: true }], TZ, NOW)
  assert.equal(s.events.length, 30)
  assert.equal(s.warnings, 1)
  assert.equal(s.now, Math.floor(NOW / 1000))
  assert.equal(s.now_label, '12:00')
  assert.equal(s.date, 'Mon, Jul 13')
})
