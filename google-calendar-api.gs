// ============================================================
// Google Apps Script — Calendar Events API
// ============================================================
// Deployment:
//   1. Go to https://script.google.com
//   2. Create a new project, paste this code
//   3. Click "Deploy" → "New deployment"
//   4. Choose "Web app", set "Execute as" = "Me", "Who has access" = "Anyone"
//   5. Copy the generated URL into credentials.h as GOOGLE_CALENDAR_URL
//
// Purpose:
//   Returns today's calendar events as JSON for the ESP32 e-paper display.
//   Includes events from all calendars (primary + shared by others).
//   Returns the earliest 8 globally by displayed HH:MM start time; the ESP32
//   displays at most 6. Equal start times retain calendar/event encounter order.
//   All-day events keep the existing "00:00" through "24:00" representation.
// ============================================================

const MAX_EVENTS = 8;
const API_KEY = "xxx";

function doGet(e) {
  // Validate API key
  if (e.parameter.key !== API_KEY) {
    return ContentService
      .createTextOutput(JSON.stringify({ error: "unauthorized" }))
      .setMimeType(ContentService.MimeType.JSON);
  }

  const today = new Date();
  today.setHours(0, 0, 0, 0);
  const tomorrow = new Date(today);
  tomorrow.setDate(tomorrow.getDate() + 1);

  const result = [];

  // Fetch events from ALL calendars (own + shared by others)
  const allCals = CalendarApp.getAllCalendars();
  for (let c = 0; c < allCals.length; c++) {
    const events = allCals[c].getEvents(today, tomorrow);

    for (let i = 0; i < events.length; i++) {
      const ev = events[i];

      const calName = allCals[c].getName();

      if (ev.isAllDayEvent()) {
        retainEarliestEvent(result, {
          start: "00:00",
          end: "24:00",
          title: ev.getTitle(),
          calendar: calName
        });
        continue;
      }

      const startTime = ev.getStartTime();
      const endTime = ev.getEndTime();

      retainEarliestEvent(result, {
        start: formatTime(startTime),
        end: formatTime(endTime),
        title: ev.getTitle(),
        calendar: calName
      });
    }
  }

  return ContentService
    .createTextOutput(JSON.stringify({ events: result }))
    .setMimeType(ContentService.MimeType.JSON);
}

// Keep only the earliest MAX_EVENTS records seen so far, sorted by the same
// HH:MM representation consumed by the firmware. Insert after existing ties so
// later encounters cannot displace an equal-time event at the cutoff. With a
// fixed limit of 8, insertion avoids retaining/sorting all calendars' records.
function retainEarliestEvent(result, event) {
  let position = 0;
  while (position < result.length && result[position].start <= event.start) {
    position++;
  }
  if (position < MAX_EVENTS) {
    if (result.length === MAX_EVENTS) {
      result.pop();
    }
    result.splice(position, 0, event);
  }
}

function formatTime(date) {
  const hh = String(date.getHours()).padStart(2, '0');
  const mm = String(date.getMinutes()).padStart(2, '0');
  return hh + ':' + mm;
}
