# xiao-ee02-e-paper-calendar

Calendar, Weather and AI picture on XIAO EE02 E-Paper.

For a detailed description see the [E-Paper Wall Calendar with AI-Generated Artwork and Weather Display](https://www.makerguides.com/e-paper-wall-calendar-with-ai-generated-artwork-and-weather-display/) article.

## Overview

A battery-powered e-paper dashboard built on the **Seeed Studio XIAO ESP32S3 Plus** with the **EE02 6-color e-paper display** (1600×1200, Spectra 6 palette). It wakes periodically to refresh the screen with:

- **AI-generated artwork** — Weather- and season-aware images in a Japanese anime style, selected from `PIC_THEMES` (including Totoro), generated via OpenAI's API and Floyd-Steinberg dithered for the 6-color display
- **Current weather** — Temperature, humidity, rainfall, wind speed, and a hand-drawn weather icon fetched from OpenWeatherMap
- **5-day forecast** — Daily high/low temperatures, weather icons, and descriptions grouped from 3-hourly forecast data
- **Monthly calendar** — Full month grid with today highlighted and weekend days in red
- **Calendar events** — Today's events pulled from calendars accessible to Google Apps Script, with up to six displayed by the firmware
- **Battery indicator** — Battery charge percentage drawn in the corner

The firmware schedules deep sleep between updates and avoids wakes in 00:00–04:59 when local time is available. These are current implementation settings; sleep current and physical wake behavior have not been independently validated.

This is a working-intent prototype. The advertised capabilities above describe its intended use and current implementation, not a validated production system.

## Project direction and repository guidance

Stabilization retains the XIAO ESP32-S3 Plus/EE02 hardware and Arduino framework, preserves calendar, weather, and artwork capabilities, and prioritizes correctness, security, reliability, and testability. Reproducible build validation comes before substantive firmware repairs; power optimization follows foundational fixes. UI redesign, additional calendar providers, and framework/library migration are deferred. Existing defaults do not select future calendar, retention, degraded-operation, or refresh policy.

See [AGENTS.md](AGENTS.md) for development instructions, [docs/ENGINEERING.md](docs/ENGINEERING.md) for architecture, constraints, validation limits and Open decisions, and [CHANGELOG.md](CHANGELOG.md) for canonical project-change history.

## Hardware

| Component | Detail |
|-----------|--------|
| **Board** | Seeed Studio XIAO ESP32S3 Plus |
| **Display** | XIAO EE02 Expansion Board (6-color e-paper, 1600×1200) |
| **Battery** | Li-Po battery (connected via XIAO battery header) |

## How It Works

1. **Wake from deep sleep** — Every `DEEP_SLEEP_HOURS` (default: 3 hours), or at 05:00 if the wake time would fall in the dead zone
2. **Connect to Wi-Fi** and sync time via NTP
3. **Fetch weather data** from OpenWeatherMap (current, 3-hourly forecast, and daily grouped forecast)
4. **Fetch calendar events** from a Google Apps Script web app; current code attempts calendar and forecast retrieval only after current-weather retrieval succeeds
5. **Generate daily AI image** — When current weather is valid, generate if today's image is absent or `PIC_SKIP_EXISTING_IMAGE` is disabled:
   - Sends current weather + season to OpenAI GPT (`gpt-4o-mini`) to create an image prompt
   - Sends the prompt to OpenAI Image Generation (`gpt-image-1-mini`) returning a 1024×1024 PNG
   - Decodes the PNG and applies Floyd-Steinberg dithering to the Spectra 6 palette
   - Stores the 4bpp raw image in SPIFFS
6. **Render the display** — Draws all sections (picture, weather, calendar, events, battery) to the e-paper
7. **Deep sleep** until the next cycle; with `DEEP_SLEEP_HOURS = 0`, setup runs once and the empty loop performs no further updates

## Layout

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

## Configuration

Application settings are in [constants.h](constants.h); display selection is declared in [driver.h](driver.h), and credentials are supplied locally:

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

## Google Calendar Setup

The [google-calendar-api.gs](google-calendar-api.gs) file exposes today's calendar events as a JSON endpoint. For an authorized deployment, create a Google Apps Script project, set its `API_KEY` to a private value matching `CALENDAR_API_KEY`, and deploy as a web app using the identity/access settings described in the script. Set its URL in `GOOGLE_CALENDAR_URL` in local `credentials.h`.

The documented deployment executes as the deploying account and allows access to anyone possessing the endpoint/key. Review that account's calendar access and deployment timezone before use; timezone alignment with the firmware has not been verified. Current code enumerates all accessible calendars, returns at most eight events, and represents all-day events as `00:00`–`24:00`. Firmware retains at most six before sorting that retained subset. The script's comments describing six returned events and `--:--` placeholders disagree with its code; those comments are not an approved policy.

## Operation and recovery limits

There is no committed reproducible build/flash procedure, automated test suite, or validated recovery runbook. Keep local credentials private and use Serial output for diagnosis without sharing sensitive event/API content. Current code can skip calendar retrieval after weather failure, lose volatile event data across deep sleep, and format SPIFFS on mount failure. Do not rely on offline retention or preservation of cached artwork after filesystem failure. TLS certificate verification is currently disabled; security stabilization and hardware validation are still required. Recovery/retention choices remain Open in the engineering anchor.

## License

See [LICENSE](LICENSE).
