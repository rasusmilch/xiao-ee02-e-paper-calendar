'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const test = require('node:test');
const vm = require('node:vm');

// Control host-local date operations without changing production timezone logic.
process.env.TZ = 'UTC';
const NOW = '2026-10-11T12:34:56.000Z';
class FixedDate extends Date {
  constructor(...args) {
    super(...(args.length ? args : [NOW]));
  }
}

const script = new vm.Script(fs.readFileSync(
    path.join(__dirname, '..', 'google-calendar-api.gs'), 'utf8'),
    { filename: 'google-calendar-api.gs' });

function timed(title, start) {
  const seconds = start.length === 5 ? start + ':00' : start;
  const startDate = new Date('2026-10-11T' + seconds + 'Z');
  return { title, start: startDate,
    end: new Date(startDate.getTime() + 30 * 60 * 1000), allDay: false };
}

function allDay(title) {
  return { title, allDay: true };
}

function calendar(name, events) {
  return { name, events };
}

function run(calendars, key = 'xxx') {
  const visits = [];
  const examined = [];
  let calendarLookups = 0;
  const context = vm.createContext({
    Date: FixedDate,
    CalendarApp: {
      getAllCalendars() {
        calendarLookups++;
        return calendars.map(cal => ({
          getName: () => cal.name,
          getEvents(today, tomorrow) {
            assert.equal(today.toISOString(), '2026-10-11T00:00:00.000Z');
            assert.equal(tomorrow.toISOString(), '2026-10-12T00:00:00.000Z');
            visits.push(cal.name);
            return cal.events.map(event => ({
              isAllDayEvent: () => event.allDay,
              getTitle() {
                examined.push(event.title);
                return event.title;
              },
              getStartTime() {
                assert.equal(event.allDay, false,
                    'all-day formatting must not use timed getters');
                return event.start;
              },
              getEndTime() {
                assert.equal(event.allDay, false);
                return event.end;
              }
            }));
          }
        }));
      }
    },
    ContentService: {
      MimeType: { JSON: 'application/json' },
      createTextOutput(text) {
        return {
          setMimeType(mime) { return { text, mime }; }
        };
      }
    }
  });
  script.runInContext(context);
  const output = context.doGet({ parameter: { key } });
  assert.equal(output.mime, 'application/json');
  const body = JSON.parse(output.text);
  if (key === 'xxx') {
    assert.equal(calendarLookups, 1);
    assert.deepEqual(visits, calendars.map(cal => cal.name),
        'every accessible calendar must be queried with the same daily interval');
    assert.deepEqual(examined, calendars.flatMap(cal =>
      cal.events.map(event => event.title)), 'every returned event must be considered');
    assert.deepEqual(Object.keys(body), ['events']);
    assert.ok(body.events.length <= 8);
    for (const event of body.events) {
      assert.deepEqual(Object.keys(event).sort(), ['calendar', 'end', 'start', 'title']);
      for (const value of Object.values(event)) assert.equal(typeof value, 'string');
    }
  } else {
    assert.equal(calendarLookups, 0);
    assert.deepEqual(visits, []);
    assert.deepEqual(examined, []);
  }
  return body;
}

function expectTitles(calendars, expected) {
  const events = run(calendars).events;
  assert.deepEqual(events.map(event => event.title), expected);
  return events;
}

test('A: no events, including empty accessible calendars', () => {
  expectTitles([], []);
  expectTitles([calendar('empty-own', []), calendar('empty-shared', [])], []);
});

test('B: one calendar below the limit is sorted', () => {
  expectTitles([calendar('own', [timed('late', '13:00'), timed('early', '09:00'),
    timed('middle', '11:00')])], ['early', 'middle', 'late']);
});

test('C: multiple calendars below the total limit', () => {
  expectTitles([calendar('own', [timed('three', '03:00'), timed('one', '01:00')]),
    calendar('shared', [timed('four', '04:00'), timed('two', '02:00')])],
  ['one', 'two', 'three', 'four']);
});

test('D: exactly eight events', () => {
  expectTitles([calendar('own', [timed('eight', '08:00'), timed('two', '02:00'),
    timed('six', '06:00'), timed('four', '04:00'), timed('one', '01:00'),
    timed('seven', '07:00'), timed('five', '05:00'), timed('three', '03:00')])],
  ['one', 'two', 'three', 'four', 'five', 'six', 'seven', 'eight']);
});

test('E: nine events discard the latest, not the last encountered', () => {
  expectTitles([calendar('own', [timed('nine', '09:00'), timed('eight', '08:00'),
    timed('seven', '07:00'), timed('six', '06:00'), timed('five', '05:00'),
    timed('four', '04:00'), timed('three', '03:00'), timed('two', '02:00'),
    timed('one', '01:00')])],
  ['one', 'two', 'three', 'four', 'five', 'six', 'seven', 'eight']);
});

test('F: more than eight in the first calendar cannot hide a later calendar', () => {
  expectTitles([calendar('first', [timed('ten', '10:00'), timed('eleven', '11:00'),
    timed('twelve', '12:00'), timed('thirteen', '13:00'), timed('fourteen', '14:00'),
    timed('fifteen', '15:00'), timed('sixteen', '16:00'), timed('seventeen', '17:00'),
    timed('eighteen', '18:00'), timed('nineteen', '19:00')]),
  calendar('later', [timed('early', '08:00')])],
  ['early', 'ten', 'eleven', 'twelve', 'thirteen', 'fourteen', 'fifteen', 'sixteen']);
});

const interleaved = [
  calendar('A', [timed('six', '06:00'), timed('nine', '09:00'), timed('twelve', '12:00')]),
  calendar('B', [timed('seven', '07:00'), timed('ten', '10:00'), timed('thirteen', '13:00')]),
  calendar('C', [timed('eight', '08:00'), timed('eleven', '11:00'), timed('fourteen', '14:00')])
];
const interleavedExpected = ['six', 'seven', 'eight', 'nine', 'ten', 'eleven',
  'twelve', 'thirteen'];

test('G: interleaved calendars yield globally chronological events', () => {
  expectTitles(interleaved, interleavedExpected);
});

test('H: reversed calendar enumeration preserves distinct-time selection', () => {
  const forward = expectTitles(interleaved, interleavedExpected);
  const reversed = expectTitles([...interleaved].reverse(), interleavedExpected);
  assert.deepEqual(reversed, forward);
});

test('I: equal times within one calendar retain encounter order', () => {
  expectTitles([calendar('own', [timed('tie-first', '09:00'), timed('tie-second', '09:00'),
    timed('early', '08:00'), timed('tie-third', '09:00')])],
  ['early', 'tie-first', 'tie-second', 'tie-third']);
});

test('J: equal times across calendars preserve encounter order at the cutoff', () => {
  expectTitles([calendar('A', [timed('one', '01:00'), timed('two', '02:00'),
    timed('three', '03:00'), timed('tie-A', '07:00')]),
  calendar('B', [timed('four', '04:00'), timed('five', '05:00'), timed('six', '06:00'),
    timed('tie-B-first', '07:00'), timed('tie-B-second', '07:00')])],
  ['one', 'two', 'three', 'four', 'five', 'six', 'tie-A', 'tie-B-first']);
});

test('K: mixed timed and all-day events use displayed start times without extra priority', () => {
  expectTitles([calendar('A', [timed('midnight', '00:00'), timed('eight', '08:00'),
    allDay('all-A')]), calendar('B', [allDay('all-B'), timed('seven', '07:00')])],
  ['midnight', 'all-A', 'all-B', 'seven', 'eight']);
});

test('L: all-day records retain 00:00 through 24:00', () => {
  assert.deepEqual(run([calendar('own', [allDay('day')])]), { events: [
    { start: '00:00', end: '24:00', title: 'day', calendar: 'own' }
  ] });
});

test('M: response fields and HH:MM formatting remain compatible', () => {
  assert.deepEqual(run([calendar('Synthetic shared calendar', [timed('Synthetic appointment', '09:05')])]),
    { events: [{ start: '09:05', end: '09:35', title: 'Synthetic appointment',
      calendar: 'Synthetic shared calendar' }] });
});

test('N: invalid API key retains rejection and does not query calendars', () => {
  assert.deepEqual(run([calendar('own', [timed('unread', '09:00')])], 'invalid'),
    { error: 'unauthorized' });
});

test('O: first six records are the earliest six available to unchanged firmware', () => {
  const events = expectTitles(interleaved, interleavedExpected);
  assert.deepEqual(events.slice(0, 6).map(event => event.title),
    ['six', 'seven', 'eight', 'nine', 'ten', 'eleven']);
});

test('P: displayed HH:MM, not hidden seconds, defines equal-time order', () => {
  const events = expectTitles([calendar('own', [timed('first-seen', '09:15:50'),
    timed('second-seen', '09:15:10')])], ['first-seen', 'second-seen']);
  assert.deepEqual(events.map(event => event.start), ['09:15', '09:15']);
});
