# Alert + Ambient Roadmap

## Goal

Move the project from a sensor demo into a small smart-home product with a stronger on-device experience.

The first product direction is:

1. Alert Screen
2. Ambient Mode
3. Optional Scene Text Layer

This roadmap is based on the current codebase:

- Local TFT UI in `main/main.c`
- Embedded web UI in `main/web/index.html`, `main/web/app.css`, `main/web/app.js`
- Shared runtime state through `dashboard_state_t`

## Why This Direction

This direction fits the current hardware and software constraints well:

1. It does not require extra servers or cloud services.
2. It reuses the existing runtime telemetry.
3. It improves product feel immediately on the TFT.
4. It matches the style of compact smart-home devices that emphasize context, room status, and fast-glance readability.

## Inspiration References

These references are used for product direction and UX inspiration, not for copying layouts directly.

1. Xiaomi Human x Car x Home ecosystem:
   https://www.mi.com/us/event/2024/human-car-home/

2. Aqara CES 2025 announcement, especially the idea of instant room visibility and compact display usefulness:
   https://www.aqara.com/us/news-us-2/ces-2025-aqara-showcases-innovative-smart-home-solutions-for-a-seamless-intuitive-experience/

3. Aqara Climate Sensor W100 product page, especially the ideas of glanceable display, dual information, and direct interaction:
   https://eu.aqara.com/products/aqara-climate-sensor-w100

4. Tuya Smart Control Panel concept, especially the idea of one screen as the entrance to multiple smart-home actions:
   https://solution.tuya.com/projects/CMb45sjcn3rxad

## Product Interpretation For This Project

The references above suggest a few recurring design ideas:

1. The device should communicate room condition, not only raw numbers.
2. The screen should support quick-glance understanding.
3. The UI should feel like a product mode system, not just separate demo screens.
4. Alerts should be decisive and visual.
5. Calm states should look minimal and ambient.

In this project, that translates into:

1. `MONITOR` remains the primary data screen.
2. `ALERT` becomes a real full-screen state, not just a stub.
3. `AMBIENT` becomes the idle / calm screen.
4. A small scene-language layer can explain conditions in plain phrases.

## Feature Set

### 1. Alert Screen

Purpose:
Show strong, immediate visual feedback when air quality crosses a warning threshold.

User value:

1. The device becomes actionable instead of passive.
2. The user can understand danger level in one glance.
3. The product feels intentional and premium.

Core behavior:

1. Trigger when AQI enters bad ranges.
2. Show a full-screen alert card with strong color and motion.
3. Include one short headline and one short recommendation.
4. Auto-return when air quality recovers, or hold for a cooldown window.

Recommended trigger policy:

1. AQI 1-2: no alert
2. AQI 3: soft advisory only
3. AQI 4: warning alert
4. AQI 5: urgent alert

Recommended UI contents:

1. Severity label:
   `AIR WATCH`, `AIR WARNING`, `AIR ALERT`
2. Main message:
   `VENTILATE ROOM`
   `CHECK AIR NOW`
3. Supporting line:
   `AQI IS RISING`
   `POOR AIR DETECTED`
4. One recommendation:
   `OPEN WINDOWS`
   `MOVE TO FRESHER SPACE`

Recommended animation:

1. Slow pulse glow for level 4
2. Stronger pulse or border flash for level 5
3. No noisy animation that makes text harder to read

Implementation steps:

1. Add alert-state helper functions in `main/main.c`
2. Map AQI to alert severity and message
3. Replace current `draw_alarm_screen()` stub with a real `draw_alert_screen()`
4. Add timing logic for alert hold and recovery cooldown
5. Decide how the local menu should interact with active alerts

Code areas:

1. `dashboard_state_t` usage in `main/main.c`
2. AQI helper section in `main/main.c`
3. `draw_alarm_screen()` in `main/main.c`
4. local screen switching logic in `main/main.c`

Definition of done:

1. AQI 4 and 5 show clearly different alert states
2. Alert text is readable from a distance
3. Alert does not flicker when AQI fluctuates
4. Returning to normal state feels smooth

### 2. Ambient Mode

Purpose:
Turn the device into a calm, always-on room companion when conditions are normal.

User value:

1. The device looks good even when nothing urgent is happening.
2. It can stay on a desk or shelf without feeling noisy.
3. It gives quick-glance comfort information.

Core behavior:

1. Show time, AQI state, temperature, and humidity in a minimal layout.
2. Use fewer numbers and more mood/status language.
3. Change accent color and atmosphere according to room condition.

Recommended content:

1. Time
2. AQI label:
   `GOOD`, `MODERATE`, `POOR`, `UNHEALTHY`
3. Room phrase:
   `AIR STABLE`
   `FRESH ROOM`
   `VENTILATE SOON`
4. Small secondary metrics:
   temperature and humidity

Recommended visual style:

1. Large central status
2. Sparse typography
3. Slow ambient motion only
4. Soft gradients or halo effects

Mode entry ideas:

1. Manual mode from menu
2. Auto-enter after no interaction for a period
3. Auto-exit when an alert is active

Implementation steps:

1. Add a new local screen enum such as `LOCAL_SCREEN_AMBIENT`
2. Add a dedicated `draw_ambient_screen()` renderer
3. Add helper for room mood text based on AQI and humidity
4. Decide if smoke demo should include Ambient in dev mode
5. Add transitions between `MONITOR`, `AMBIENT`, and `ALERT`

Code areas:

1. `local_screen_t` in `main/main.c`
2. menu item rendering in `draw_menu_overlay()`
3. `draw_local_screen()` switch
4. optional idle timer support

Definition of done:

1. Ambient screen looks intentional in both healthy and poor conditions
2. It remains readable on a 160x128 TFT
3. It feels distinct from `MONITOR`
4. It exits cleanly when an alert is triggered

### 3. Scene Text Layer

Purpose:
Translate raw telemetry into short, product-like room interpretations.

User value:

1. Less cognitive load
2. More emotional product feel
3. Better differentiation from a plain sensor dashboard

Example scene phrases:

1. `AIR STABLE`
2. `FRESH ROOM`
3. `COMFORT GOOD`
4. `AIR DROPPING`
5. `VENTILATE NOW`
6. `REST MODE`

Suggested rules:

1. AQI 1-2 and humidity normal:
   `FRESH ROOM`
2. AQI 3:
   `AIR WATCH`
3. AQI 4:
   `VENTILATE SOON`
4. AQI 5:
   `CHECK AIR NOW`

Implementation steps:

1. Add a helper that maps runtime state to a short scene label
2. Reuse it in `MONITOR`, `AMBIENT`, and `ALERT`
3. Keep phrase count small and stable

Definition of done:

1. Phrases are short enough for the TFT
2. Different AQI states feel meaningfully different
3. The device communicates room condition with less raw data dependence

## Suggested Development Order

### Phase 1: Alert Screen

Build first because it gives the strongest visible product upgrade with the least architecture change.

Tasks:

1. Define alert severity model
2. Implement `draw_alert_screen()`
3. Add trigger and cooldown logic
4. Test with existing smoke AQI sweep

### Phase 1 Detailed Implementation

This is the most important section for the current project because it can be implemented on top of the existing code with minimal risk.

Important current anchors in `main/main.c`:

1. `local_screen_t`
2. `aqi_color()`
3. `aqi_label()`
4. `aqi_subtext()`
5. `draw_alarm_screen()`
6. `draw_local_screen()`

Recommended strategy for the first safe iteration:

1. Keep `LOCAL_SCREEN_ALARM` as-is to avoid touching menu order and smoke flow too much.
2. Repurpose the screen visually from `ALARM` into a real `ALERT` screen.
3. Do not add cooldown logic first.
4. First make AQI 4 and 5 render beautifully and clearly.
5. After that, add timing / anti-flicker logic.

### Phase 1 Step 1: Add Alert Model

Add this near the other enums in `main/main.c`.

```c
typedef enum {
  ALERT_LEVEL_NONE = 0,
  ALERT_LEVEL_ADVISORY,
  ALERT_LEVEL_WARNING,
  ALERT_LEVEL_URGENT,
} alert_level_t;
```

Then add helper prototypes near the top of the file:

```c
static alert_level_t alert_level_from_aqi(int aqi);
static const char *alert_title(alert_level_t level);
static const char *alert_headline(alert_level_t level);
static const char *alert_hint(alert_level_t level);
static uint16_t alert_accent_color(alert_level_t level);
```

### Phase 1 Step 2: Map AQI To Alert Content

Add these helpers near the existing AQI helper section, ideally close to `aqi_color()` and `aqi_label()`.

```c
static alert_level_t alert_level_from_aqi(int aqi) {
  if (aqi >= 5) {
    return ALERT_LEVEL_URGENT;
  }
  if (aqi >= 4) {
    return ALERT_LEVEL_WARNING;
  }
  if (aqi >= 3) {
    return ALERT_LEVEL_ADVISORY;
  }
  return ALERT_LEVEL_NONE;
}

static const char *alert_title(alert_level_t level) {
  switch (level) {
  case ALERT_LEVEL_ADVISORY:
    return "AIR WATCH";
  case ALERT_LEVEL_WARNING:
    return "AIR WARNING";
  case ALERT_LEVEL_URGENT:
    return "AIR ALERT";
  default:
    return "AIR STABLE";
  }
}

static const char *alert_headline(alert_level_t level) {
  switch (level) {
  case ALERT_LEVEL_ADVISORY:
    return "WATCH ROOM AIR";
  case ALERT_LEVEL_WARNING:
    return "VENTILATE ROOM";
  case ALERT_LEVEL_URGENT:
    return "CHECK AIR NOW";
  default:
    return "AIR IS NORMAL";
  }
}

static const char *alert_hint(alert_level_t level) {
  switch (level) {
  case ALERT_LEVEL_ADVISORY:
    return "OPEN AIR IF NEEDED";
  case ALERT_LEVEL_WARNING:
    return "OPEN WINDOWS SOON";
  case ALERT_LEVEL_URGENT:
    return "MOVE TO FRESHER SPACE";
  default:
    return "LOW RISK";
  }
}

static uint16_t alert_accent_color(alert_level_t level) {
  switch (level) {
  case ALERT_LEVEL_ADVISORY:
    return COLOR_YELLOW;
  case ALERT_LEVEL_WARNING:
    return COLOR_ORANGE;
  case ALERT_LEVEL_URGENT:
    return COLOR_RED;
  default:
    return COLOR_LIME;
  }
}
```

Why this is important:

1. It gives one single source of truth for alert semantics.
2. It keeps rendering code clean.
3. It prepares the same language to be reused later in Ambient and Scene text.

### Phase 1 Step 3: Change The Alarm Renderer To Consume Runtime State

Current code:

```c
static void draw_alarm_screen(void)
```

Recommended first change:

```c
static void draw_alarm_screen(const dashboard_state_t *state)
```

This is one of the most important changes because the alert screen must react to live AQI.

Then replace the current stub with something like this:

```c
static void draw_alarm_screen(const dashboard_state_t *state) {
  alert_level_t level;
  uint16_t accent;
  const char *title;
  const char *headline;
  const char *hint;
  char aqi_text[16];

  if (state == NULL) {
    draw_local_header("AIR ALERT", "STATE MISSING");
    return;
  }

  level = alert_level_from_aqi(state->aqi);
  accent = alert_accent_color(level);
  title = alert_title(level);
  headline = alert_headline(level);
  hint = alert_hint(level);

  fb_clear(COLOR_BG);
  fb_fill_rect(0, 0, TFT_WIDTH, TFT_HEIGHT, RGB565(4, 8, 14));

  fb_fill_rect(0, 0, TFT_WIDTH, 6, accent);
  fb_fill_rect(10, 14, 140, 100, RGB565(8, 18, 30));
  fb_draw_rect(10, 14, 140, 100, accent);
  fb_draw_rect(12, 16, 136, 96, RGB565(28, 48, 68));

  fb_draw_text5x7(18, 24, title, accent, 2);
  fb_draw_text5x7(18, 48, headline, COLOR_WHITE, 2);
  fb_draw_text5x7(18, 72, hint, COLOR_MUTED, 1);

  snprintf(aqi_text, sizeof(aqi_text), "AQI %d", state->aqi);
  fb_fill_rect(18, 88, 52, 18, RGB565(12, 28, 44));
  fb_draw_rect(18, 88, 52, 18, accent);
  fb_draw_text5x7(25, 94, aqi_text, COLOR_WHITE, 1);
}
```

This is intentionally simple and safe:

1. no extra state machine yet
2. no alert cooldown yet
3. no new screen enum yet
4. easy to plug into smoke AQI sweep immediately

### Phase 1 Step 4: Pass Runtime State Into The Alert Screen

Because `draw_alarm_screen()` now needs telemetry, update the screen switch.

Current shape:

```c
case LOCAL_SCREEN_ALARM:
  draw_alarm_screen();
  break;
```

Change to:

```c
case LOCAL_SCREEN_ALARM:
  draw_alarm_screen(state);
  break;
```

That update belongs in `draw_local_screen()`.

### Phase 1 Step 5: Optional Auto-Trigger Logic

If the product should automatically jump into the alert screen, add a helper like this:

```c
static bool should_force_alert_screen(const dashboard_state_t *state) {
  if (state == NULL) {
    return false;
  }
  return alert_level_from_aqi(state->aqi) >= ALERT_LEVEL_WARNING;
}
```

Then in the runtime loop or before rendering, force the active screen when needed:

```c
if (should_force_alert_screen(&state)) {
  local_menu_set_active_screen(LOCAL_SCREEN_ALARM, false);
}
```

Safe note:

1. Do this only after the visual alert screen is stable.
2. Otherwise debugging screen flow becomes harder.
3. During early development, manual smoke test access is easier.

### Phase 1 Step 6: Add Anti-Flicker Later

Do not start here. Add it only after the basic alert screen is working.

When ready, add a tiny runtime state:

```c
static bool s_alert_latched;
static int64_t s_alert_hold_started_us;
```

Then use a simple policy:

1. Enter latch when AQI >= 4
2. Hold at least 4 to 6 seconds
3. Clear latch only when AQI <= 2

Pseudo-code:

```c
if (!s_alert_latched && state->aqi >= 4) {
  s_alert_latched = true;
  s_alert_hold_started_us = esp_timer_get_time();
}

if (s_alert_latched) {
  int64_t hold_ms = (esp_timer_get_time() - s_alert_hold_started_us) / 1000LL;
  if (state->aqi <= 2 && hold_ms >= 5000) {
    s_alert_latched = false;
  }
}
```

This is important because raw AQI can jump around during smoke tests or real sensors.

### Phase 1 Step 7: Test Checklist With Existing Smoke Test

Use the existing AQI sweep in the project.

Check these transitions:

1. AQI 1-2: alert screen should look calm if entered manually
2. AQI 3: advisory language only
3. AQI 4: warning state is clearly stronger
4. AQI 5: urgent state is unmistakable

What to fix first if it looks wrong:

1. text too long
2. too many lines
3. weak contrast
4. orange and red looking too similar

### Phase 1 Minimal Patch Summary

If you want the shortest useful implementation, these are the exact important changes:

1. add `alert_level_t`
2. add `alert_*()` helper functions
3. change `draw_alarm_screen(void)` to `draw_alarm_screen(const dashboard_state_t *state)`
4. update `draw_local_screen()` to pass `state`
5. keep everything else unchanged for the first pass

### Phase 2: Ambient Mode

Build second because it complements Alert and improves everyday idle experience.

Tasks:

1. Add new screen enum and menu item
2. Implement calm layout
3. Add room mood text
4. Add auto-entry or manual access

### Phase 2 Detailed Implementation

Ambient mode is best implemented only after the Alert screen text language is already stable.

Important safety decision for this codebase:

Do not remove or reshuffle existing screens too early.

Current project already has working menu and smoke-test behavior for:

1. `MONITOR`
2. `WIFI`
3. `ALARM`
4. `GAME`
5. `MEMORY`

Recommended migration options:

1. Lowest risk:
   keep the existing enum order and temporarily repurpose `GAME` into `AMBIENT`
2. Medium risk:
   add `LOCAL_SCREEN_AMBIENT` as a sixth screen and then update menu + smoke flow together

For this project, the recommended path is option 1 first, because it preserves current behavior with the fewest moving parts.

### Phase 2 Step 1: Add New Screen Enum

Update `local_screen_t` in `main/main.c`.

Current:

```c
typedef enum {
  LOCAL_SCREEN_MONITOR = 0,
  LOCAL_SCREEN_WIFI,
  LOCAL_SCREEN_ALARM,
  LOCAL_SCREEN_GAME,
  LOCAL_SCREEN_MEMORY,
  LOCAL_SCREEN_COUNT,
} local_screen_t;
```

Recommended low-risk version:

```c
typedef enum {
  LOCAL_SCREEN_MONITOR = 0,
  LOCAL_SCREEN_WIFI,
  LOCAL_SCREEN_ALARM,
  LOCAL_SCREEN_AMBIENT,
  LOCAL_SCREEN_MEMORY,
  LOCAL_SCREEN_COUNT,
} local_screen_t;
```

Safe migration note:

1. The example above replaces `GAME`.
2. Do this only after Phase 1 is stable.
3. If you want zero menu regression risk, keep the current enum untouched and only rename `draw_game_screen()` visually into Ambient first.

Recommended zero-regression version for this project:

```c
static void draw_game_screen(void) {
  draw_ambient_screen(&state);
}
```

In practice you would adapt this properly by passing state through the switch, but the key point is:

1. use the existing `GAME` slot first
2. do not expand menu state machine and smoke test until Ambient visuals are already approved

### Phase 2 Step 2: Add Ambient Phrase Helper

Add a single canonical helper close to the AQI section:

```c
static const char *room_scene_label(const dashboard_state_t *state) {
  if (state == NULL) {
    return "NO DATA";
  }
  if (state->aqi <= 2 && state->humidity_pct <= 70) {
    return "FRESH ROOM";
  }
  if (state->aqi == 3) {
    return "AIR WATCH";
  }
  if (state->aqi == 4) {
    return "VENTILATE SOON";
  }
  if (state->aqi >= 5) {
    return "CHECK AIR NOW";
  }
  return "AIR STABLE";
}
```

Important rule:

Do not redefine this helper again later with different logic.

This project should keep exactly one canonical `room_scene_label()` implementation and reuse it everywhere.

### Phase 2 Step 3: Add Ambient Renderer

Minimal first version:

```c
static void draw_ambient_screen(const dashboard_state_t *state) {
  char temp_text[16];
  char hum_text[16];
  const char *scene;
  uint16_t accent;

  if (state == NULL) {
    draw_local_header("AMBIENT", "NO STATE");
    return;
  }

  scene = room_scene_label(state);
  accent = aqi_color(state->aqi);

  fb_clear(COLOR_BG);
  fb_fill_rect(0, 0, TFT_WIDTH, TFT_HEIGHT, RGB565(2, 6, 12));
  fb_fill_circle(126, 22, 28, RGB565(8, 24, 40));
  fb_fill_circle(26, 104, 22, RGB565(6, 18, 30));

  fb_draw_text5x7(14, 18, "AMBIENT", COLOR_MUTED, 1);
  fb_draw_text(16, 38, scene, accent, 2);
  fb_draw_text(16, 68, aqi_label(state->aqi), COLOR_WHITE, 4);

  snprintf(temp_text, sizeof(temp_text), "%.1fC", state->temp_tenths_c / 10.0f);
  snprintf(hum_text, sizeof(hum_text), "%d%%RH", state->humidity_pct);

  fb_draw_text5x7(16, 102, temp_text, COLOR_WHITE, 1);
  fb_draw_text5x7(86, 102, hum_text, COLOR_MUTED, 1);
}
```

### Phase 2 Step 4: Wire It Into The Screen Switch

Add a case in `draw_local_screen()`:

```c
case LOCAL_SCREEN_AMBIENT:
  draw_ambient_screen(state);
  break;
```

### Phase 2 Step 5: Add Menu Text

Update the menu label array:

```c
static const char *kMenuItems[LOCAL_SCREEN_COUNT] = {
    "MONITOR", "WIFI CONFIG", "ALERT", "AMBIENT", "MEMORY"};
```

This is one of the important visible changes because it turns the product language from a dev menu into a product menu.

### Phase 3: Scene Text Layer

Build third because it should be shared by multiple screens once the visual structure is stable.

Tasks:

1. Finalize phrase dictionary
2. Apply to Monitor
3. Apply to Ambient
4. Apply to Alert

### Phase 3 Detailed Implementation

Keep this layer intentionally small.

Bad approach:

1. dozens of phrases
2. complicated rules
3. frequent text jumps

Good approach:

1. one helper
2. 4 to 6 stable phrases
3. shared by multiple screens

Recommended shared helper:

Reuse the same `room_scene_label()` defined in Phase 2. Do not create another variant.

Then reuse it in important places:

1. `MONITOR`
2. `ALERT`
3. `AMBIENT`

Example in monitor:

```c
const char *scene = room_scene_label(state);
fb_draw_text5x7(10, 118, scene, COLOR_CYAN, 1);
```

This is the easiest way to make the whole device feel more product-like without changing architecture.

## Recommended Interaction Model

Normal condition:

1. Default screen can be `MONITOR` or `AMBIENT`
2. Menu remains available
3. User can navigate to `WIFI CFG`, `MEMORY`, and future modes

Warning condition:

1. `ALERT` takes focus
2. Device shows one main action recommendation
3. If condition improves, return to prior calm mode after a short delay

Important implementation note:

When auto-triggering Alert in a later phase, do not just force the screen every frame.

You should store the previous screen first, for example:

```c
static local_screen_t s_screen_before_alert = LOCAL_SCREEN_MONITOR;
```

Then use a guarded transition:

```c
if (!s_alert_latched && should_force_alert_screen(state)) {
  s_screen_before_alert = local_menu_snapshot().active_screen;
  local_menu_set_active_screen(LOCAL_SCREEN_ALARM, false);
  s_alert_latched = true;
}
```

And recover safely:

```c
if (s_alert_latched && alert_level_from_aqi(state->aqi) == ALERT_LEVEL_NONE) {
  local_menu_set_active_screen(s_screen_before_alert, false);
  s_alert_latched = false;
}
```

Phase 1 recommendation:

1. do not enable forced auto-trigger yet
2. finish visual alert rendering first
3. only add alert focus behavior after the team approves the screen visually

Development smoke-test condition:

1. AQI sweep continues to drive behavior review
2. Alert transitions should be observable without hardware integration
3. Ambient should be easy to test in the same sweep

## Design Constraints For This TFT

The 160x128 TFT is small, so design must stay disciplined.

Rules:

1. One main message per screen
2. One secondary line only if necessary
3. Use short all-caps labels carefully
4. Reserve heavy animation for alert states only
5. Maintain strong contrast

## Step-by-Step Build Plan

### Step 1: Lock Product Language

Decide and freeze:

1. Alert severity names
2. Scene phrases
3. Ambient mode headline style

Output:

1. Shared text table in code

### Step 2: Build Alert Logic

Implement:

1. AQI to alert severity mapping
2. AQI to recommendation mapping
3. cooldown / anti-flicker behavior

Output:

1. Stable alert-state helper functions

### Step 3: Build Alert UI

Implement:

1. Full-screen layout
2. Severity colors
3. Pulse animation

Output:

1. `draw_alert_screen()`

### Step 4: Build Ambient UI

Implement:

1. Minimal layout
2. time + status + small metrics
3. calm background and accent system

Output:

1. `draw_ambient_screen()`

### Step 5: Integrate Menu and Flow

Implement:

1. new screen enum if needed
2. menu item order
3. transitions and return behavior

Output:

1. coherent screen flow

### Step 6: Tune With Smoke Test

Implement:

1. review all AQI levels
2. tune durations
3. tune text lengths and spacing

Output:

1. visually stable demo behavior

## Testing Checklist

### Alert Screen

1. AQI rises from 3 to 4: warning appears
2. AQI rises from 4 to 5: urgent styling strengthens
3. AQI falls from 5 to 2: alert exits without flicker
4. Text remains readable in all severities

### Ambient Mode

1. Time renders correctly
2. AQI label updates correctly
3. Secondary metrics fit on screen
4. Alert overrides ambient when needed

### Scene Text

1. Phrases are not too long
2. Same room condition produces consistent phrases
3. Phrase logic does not fight with alert logic

## Risks

1. Overloading the small TFT with too much text
2. Too many screens reducing clarity
3. Animations making the device feel noisy
4. Alert transitions flickering if thresholds are too sensitive

## Recommended Next Action

Start with Phase 1 only:

1. keep the existing `MONITOR`
2. turn the current `ALARM` stub into a real `ALERT` product screen
3. validate it with smoke AQI sweep

After that is stable, move to `AMBIENT`.
