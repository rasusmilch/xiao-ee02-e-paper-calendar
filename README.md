# xiao-ee02-e-paper-calendar

Battery-powered EE02 five-day calendar and weather dashboard.

The [original prototype article](https://www.makerguides.com/e-paper-wall-calendar-with-ai-generated-artwork-and-weather-display/) explains the inherited artwork-based firmware; its artwork feature is **retired in the approved redesign** but remains present in current source until the removal is implemented.

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

The currently committed firmware on `main` wakes to show AI artwork, current weather, five-day weather forecast, a monthly calendar, up to six events from a Google Apps Script endpoint, and battery indication. Its existing sleep policy excludes 00:00–04:59 when local time is known. These are implementation observations, not a requirement to preserve the artwork or the legacy calendar interface. This prototype has not been validated as a reproducible firmware build or on target hardware.

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
5. **Generate daily AI image** — When current weather is valid, generate if today's image is absent or `PIC_SKIP_EXISTING_IMAGE` is disabled:
   - Sends current weather + season to OpenAI GPT (`gpt-4o-mini`) to create an image prompt
   - Sends the prompt to OpenAI Image Generation (`gpt-image-1-mini`) returning a 1024×1024 PNG
   - Decodes the PNG and applies Floyd-Steinberg dithering to the Spectra 6 palette
   - Stores the 4bpp raw image in SPIFFS
6. **Render the display** — Draws all sections (picture, weather, calendar, events, battery) to the e-paper
7. **Deep sleep** until the next cycle; with `DEEP_SLEEP_HOURS = 0`, setup runs once and the empty loop performs no further updates

## Current implementation — Layout

```
┌───────────────────────────────────────────────┐
│                    ┌─────────────────────────┐│
│                    │      Weather Panel      ││
│   AI Picture       │                         ││
│   (1024×1024)      │  Day name, temperature  ││
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

Current source uses [constants.h](constants.h) for settings, [driver.h](driver.h) for display selection and local credentials. These compile-time inputs will be superseded by device-hosted configuration for the approved redesign:

| Constant | Description |
|----------|-------------|
| `WX_LOCATION` | City and country for weather (e.g. `"Lohr am Main,DE"`) |
| `TIMEZONE` | POSIX timezone string for NTP sync |
| `PIC_THEMES` | Array of image-generation themes; one is randomly selected for each generated prompt |
| `NUM_PIC_THEMES` | Theme count derived from the array |
| `PIC_SKIP_EXISTING_IMAGE` | Skip generation if today's image already exists |
| `DEEP_SLEEP_HOURS` | Hours between wake cycles (0 = disabled, for debugging) |

For an operator build, create a local `credentials.h` with your credentials and API keys. It is intentionally excluded from Git; keep real values out of commits, shared logs, and receipts. The following values are illustrative only:

```
const char *SSID = "xxx";
const char *PASSWORD = "xxx";
const char *OPENAI_API_KEY = "sk-proj-xxx";
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

## Dependencies

| Library | Documented version |
|---------|---------|
| [Seeed_GFX](https://github.com/Seeed-Studio/Seeed_GFX) (TFT_eSPI fork) | 2.0.3 |
| ArduinoJson | 7.4.3 |
| [PNGdec fork referenced by the sketch](https://github.com/maet3608/PNGdec) | 1.1.6 |

A version number alone does not identify compatible source. The exact Seeed_GFX revision supporting EE02/board-screen 510 and the PNGdec fork/buffer configuration remain unresolved. The inspected Seeed_GFX V2.0.3 tag and upstream PNGdec 1.1.6 cannot be assumed to reproduce the intended display/image path. Alpha ESP32 core package availability, exact board options, memory fit, and a complete build remain unverified. No alternative library version is declared supported; see the [engineering baseline](docs/ENGINEERING.md#known-engineering-limitations).

## Current implementation — Google Calendar Setup

The [google-calendar-api.gs](google-calendar-api.gs) file exposes today's calendar events as a JSON endpoint. For an authorized deployment, create a Google Apps Script project, set its `API_KEY` to a private value matching `CALENDAR_API_KEY`, and deploy as a web app using the identity/access settings described in the script. Set its URL in `GOOGLE_CALENDAR_URL` in local `credentials.h`.

The documented deployment executes as the deploying account and allows access to anyone possessing the endpoint/key. Review that account's calendar access and deployment timezone before use; timezone alignment with the firmware has not been verified. Current code enumerates all accessible calendars, returns at most eight events, and represents all-day events as `00:00`–`24:00`. Firmware retains at most six before sorting that retained subset. The script's comments describing six returned events and `--:--` placeholders disagree with its code; those comments are not an approved policy.

## Operation and recovery limits

There is no committed reproducible build/flash procedure or validated recovery runbook. The intended AP/web/factory-reset recovery described above is not yet implemented. A focused [host regression](tests/data_acquisition_test.cpp) covers acquisition orchestration; it does not validate Arduino integration or hardware. Keep local credentials private and use Serial output for diagnosis without sharing sensitive event/API content. Calendar retrieval is independent of weather success once acquisition Wi-Fi connects, but event data remains volatile across deep sleep, and SPIFFS can be formatted on mount failure. Do not rely on offline retention or preservation of cached artwork after filesystem failure. TLS certificate verification is currently disabled; security stabilization and owner-controlled hardware validation are still required. The redesign's selected recovery and storage policies are in the engineering anchor; implementation and hardware results remain unverified.

## License

See [LICENSE](LICENSE).
