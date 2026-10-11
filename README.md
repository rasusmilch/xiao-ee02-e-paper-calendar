# xiao-ee02-e-paper-calendar

Battery-powered EE02 five-day calendar and weather dashboard.

The [original prototype article](https://www.makerguides.com/e-paper-wall-calendar-with-ai-generated-artwork-and-weather-display/) describes the historical artwork-based firmware. AI artwork generation, processing, caching and rendering have been removed. The former 1024×1024 region is reserved for the future five-day calendar; its temporarily empty state is an intermediate development state, not a completed display design or independently releasable product outcome.

## Approved product direction (not yet implemented)

- **Display:** 1600×1200 Spectra 6, rolling next five days starting today, fixed waking-hour window (exact hours configurable), timed grid the leading candidate versus a five-day agenda. The week view **replaces the 1024×1024 artwork area**. Display layout is not finally selected.
- **Other panels:** retain weather, multi-day forecast, today's detailed events, battery indication, and their existing positions initially. Whether the legacy monthly calendar remains is Open.
- **Calendars:** select multiple source calendars; support Google, iCloud/CalDAV and general ICS by source-independent internal event representation. Entirely on-device fetching/normalization; **no separate backend, hosted processor or update preprocessor**. An ICS-first initial transport is favored but not yet a committed provider-access scope; private iCloud versus public ICS access remains Open.
- **Memory:** use bounded event/scratch storage, disciplined stack/internal heap/PSRAM allocation, and a staged shared PSRAM scratch-then-render buffer where driver ownership allows. Do not assume PSRAM survives deep sleep or that direct DMA is supported without exact-library and hardware verification.
- **Storage/time:** NVS stores changed persistent configuration only (Wi-Fi, timezone, calendar URLs/selection, refresh settings). No daily churn, events, weather or routine counters go to NVS. RTC-retained memory may hold validated compact sleep context; reconstruct after invalid RTC/reset. NTP on cold boot when required, then approximately daily; use internal RTC between syncs.
- **Refresh/recovery:** battery-powered, single user-configurable coordinated data-acquisition/render cycle, independently validated per source. Successful calendars display even if another calendar fails, with a named error bar; a weather failure is shown in its own panel while previously valid forecast data may remain. Three consecutive complete acquisition failures cause a full error screen; cold boot tries Wi-Fi before presenting recovery instructions.
- **Setup:** device-hosted Wi-Fi webpage (LAN while explicitly awake, or open/no-password AP). AP stops after 10 minutes without meaningful user activity or 30 minutes maximum, then sleeps. Entering a new password may show characters; after save it is masked and the saved value is never echoed. Calendar URL fields remain displayed. An operator-accessible hardware AP trigger is required; simultaneous Setup+Refresh hold for five seconds is a **proposed, unverified** factory-reset gesture, not yet a pin/wake contract.

This is the **approved design intent**, not an assertion of deployed features. Exact schema limits, recurrence correctness, framebuffer/DMA behavior, regional display behavior, battery runtime, and recovery button implementation require validation. See [docs/ENGINEERING.md](docs/ENGINEERING.md) for owned contracts, Open decisions, and testing.

## Current prototype implementation (source observed; not the redesign)

The current source wakes to show current weather, five-day weather forecast, a monthly calendar, up to six events from a Google Apps Script endpoint, and battery indication, preserving their existing positions and dividers. The former artwork region remains empty after the existing screen clear. Its existing sleep policy excludes 00:00–04:59 when local time is known. These are implementation observations, not approval of the final layout or retention of the legacy monthly calendar. This prototype has not been validated as a reproducible firmware build or on target hardware.

## Repository workflow

Retain the Seeed XIAO ESP32-S3 Plus/EE02 Arduino platform. Carry out the redesign as separately reviewable semantic changes with static and focused host behavior tests. Full firmware compilation and hardware validation remain owner-controlled milestones; compilation is preferred but not a prerequisite for initial repairs. Uncompiled code is not claimed firmware-compatible, and CI or hosted builds are not required.

See [AGENTS.md](AGENTS.md) for developer workflow, [docs/ENGINEERING.md](docs/ENGINEERING.md) for technical requirements, current-source boundaries and roadmap, and [CHANGELOG.md](CHANGELOG.md) for merge-facing history.

## Hardware

| Component | Detail |
|-----------|--------|
| **Board** | Seeed Studio XIAO ESP32S3 Plus |
| **Display** | XIAO EE02 Expansion Board (6-color e-paper, 1600×1200) |
| **Battery** | Li-Po battery (connected via XIAO battery header) |

## Current implementation — How It Works

1. **Wake from deep sleep** — Every `DEEP_SLEEP_HOURS` (default: 3 hours), or at 05:00 if the wake time would fall in the dead zone
2. **Connect to Wi-Fi** and sync time via NTP
3. **Fetch weather data** from OpenWeatherMap (current, 3-hourly forecast, and daily grouped forecast); after the data-acquisition Wi-Fi connection succeeds, each fetch is attempted independently in this order
4. **Fetch calendar events** from a Google Apps Script web app on the same connection, regardless of weather/forecast request results. Each fetch function is called once per connected acquisition; the existing calendar request retry remains internal to its fetch function. Source-specific failures are logged, and `updateWeather()` still returns current-weather success. If Wi-Fi fails, no fetch is attempted
5. **Render the display** — Initializes, rotates and clears the e-paper, draws the existing dividers, daily forecast, monthly calendar, events, battery and weather (including the 3-hourly forecast), then updates and sleeps the panel. No artwork requests, decoding, cache access or filesystem initialization remain
6. **Deep sleep** until the next cycle; with `DEEP_SLEEP_HOURS = 0`, setup runs once and the empty loop performs no further updates

## Current implementation — Layout

```
┌───────────────────────────────────────────────┐
│                    ┌─────────────────────────┐│
│                    │      Weather Panel      ││
│   Reserved for     │                         ││
│   future week view │  Day name, temperature  ││
│                    │  Weather icon (large)   ││
│                    │  Humidity, rain, wind   ││
│                    │                         ││
│                    │  3-hourly forecast (5×) ││
├────────────────────┼─────────────────────────┤│
│                    │    Monthly Calendar     ││
│   5-Day Forecast   │                         ││
│                    │    Calendar Events      ││
│                    │               [Bat]     ││
└───────────────────────────────────────────────┘
```

## Current implementation — Configuration

Current source uses [constants.h](constants.h) for settings, [driver.h](driver.h) for display selection and local `credentials.h` for secrets. These compile-time inputs will be superseded by device-hosted configuration for the approved redesign:

| Constant | Description |
|----------|-------------|
| `WX_LOCATION` | City and country for weather (e.g. `"Lohr am Main,DE"`) |
| `TIMEZONE` | POSIX timezone string for NTP sync |
| `DEEP_SLEEP_HOURS` | Hours between wake cycles (0 = disabled, for debugging) |

For an operator build, create a local `credentials.h` with your credentials and API keys. It is intentionally excluded from Git; keep real values out of commits, shared logs, and receipts. The following values are illustrative only:

```
const char *SSID = "xxx";
const char *PASSWORD = "xxx";
const char *OPENWEATHER_API_KEY = "xxx";
const char *GOOGLE_CALENDAR_URL = "https://script.google.com/macros/s/xxx";
const char *CALENDAR_API_KEY = "xxx";
```

## Arduino IDE Settings

These are the settings documented by the sketch. A complete successful firmware build from reproducibly pinned inputs has not been demonstrated. Do not interpret this table as verified toolchain or display-driver compatibility.

| Setting | Value |
|---------|-------|
| Board | XIAO_ESP32S3_PLUS |
| USB CDC on Boot | Enabled |
| PSRAM | OPI PSRAM |
| Partition Scheme | Default with spiffs (3MB APP/1.5MB SPIFFS) |
| ESP32 Core | 4.0.0-alpha1 |

The documented partition selection is unchanged. The application no longer mounts or uses SPIFFS; artwork retirement does not erase old flash contents, format storage or migrate partitions.

## Dependencies

| Library | Documented version |
|---------|---------|
| [Seeed_GFX](https://github.com/Seeed-Studio/Seeed_GFX) (TFT_eSPI fork) | 2.0.3 |
| ArduinoJson | 7.4.3 |

A version number alone does not identify compatible source. The exact Seeed_GFX revision supporting EE02/board-screen 510 remains unresolved; the inspected V2.0.3 tag cannot be assumed to reproduce the intended display path. Alpha ESP32 core package availability, exact board options, memory fit, and a complete build remain unverified. No alternative library version is declared supported; see the [engineering baseline](docs/ENGINEERING.md#known-engineering-limitations). PNGdec and OpenAI credentials are no longer application requirements; the core networking/TLS dependencies remain.

## Current implementation — Google Calendar Setup

The [google-calendar-api.gs](google-calendar-api.gs) file exposes today's calendar events as a JSON endpoint. For an authorized deployment, create a Google Apps Script project, set its `API_KEY` to a private value matching `CALENDAR_API_KEY`, and deploy as a web app using the identity/access settings described in the script. Set its URL in `GOOGLE_CALENDAR_URL` in local `credentials.h`.

The documented deployment executes as the deploying account and allows access to anyone possessing the endpoint/key. Review that account's calendar access and deployment timezone before use; timezone alignment with the firmware has not been verified. Current code enumerates all accessible calendars, returns at most eight events, and represents all-day events as `00:00`–`24:00`. Firmware retains at most six before sorting that retained subset. The script's comments describing six returned events and `--:--` placeholders disagree with its code; those comments are not an approved policy.

## Operation and recovery limits

There is no committed reproducible build/flash procedure or validated recovery runbook. The intended AP/web/factory-reset recovery described above is not yet implemented. A focused [host regression](tests/data_acquisition_test.cpp) covers acquisition orchestration; it does not validate Arduino integration, display geometry, memory peaks or hardware. Keep local credentials private and use Serial output for diagnosis without sharing sensitive event/API content. Calendar retrieval is independent of weather success once acquisition Wi-Fi connects, but event data remains volatile across deep sleep. No application filesystem operations or persistent data cache remain; do not rely on offline retention. TLS certificate verification is currently disabled; security stabilization and owner-controlled hardware validation are still required. The redesign's selected recovery and storage policies are in the engineering anchor; implementation and hardware results remain unverified.

## License

See [LICENSE](LICENSE).
