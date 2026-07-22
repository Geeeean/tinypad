# TinyPad client

Cross-platform host-side daemon + UI for the TinyPad mixer/macro pad. One
process owns the audio backend and the USB connection to the firmware, and
serves a small local UI via [webview](https://github.com/webview/webview)
(WebView2 on Windows, WebKitGTK on Linux, WKWebView on macOS).

## Layout

```
include/, src/
  core/           platform-neutral: mixer/session model (mixer_state),
                  macro-button config (macro_map), on-device display/label
                  settings (device_settings), USB framing (device_link),
                  USB VID/PID/product-string matching (device_discovery)
  platform/       per-OS backends behind small vtable/interface headers:
                    audio_backend.h     -- PipeWire (Linux) / WASAPI (Windows) /
                                           Core Audio process taps (macOS)
                    serial_port.h       -- POSIX termios (Linux/macOS) / Win32
                                           COM port (Windows)
                    serial_enum.h       -- sysfs (Linux) / IOKit (macOS) /
                                           SetupAPI (Windows), for
                                           device_discovery's auto-connect
                    keyboard_inject.h   -- CGEventPost (macOS) / SendInput
                                           (Windows) / XTest (Linux), for
                                           macro buttons bound to a hotkey
                    tray.h              -- Shell_NotifyIcon (Windows) /
                                           NSStatusItem (macOS) /
                                           GtkStatusIcon (Linux), the Show/Quit
                                           icon shown while the window is closed
  ui/             ui_bridge.c: owns the webview window, binds JS-callable
                  native functions, pushes state snapshots into the page
tests/            ctest suite for core/ against a fake audio_backend_vtable_t
                  (see Testing below)
ui/               React + TypeScript + Tailwind v4 + shadcn/ui frontend (Vite
                  project, own package.json) -- shared verbatim across all
                  three platforms, ui_bridge.c is the only thing that
                  differs per OS. `npm run build` produces ui/dist/, a
                  single self-contained index.html (see below for why).
                  components/device/ renders a top-down mockup of the
                  physical pad (display, then knobs, then keys); clicking
                  any element opens its configuration dialog.
```

The wire protocol (packet layout, checksums, framing) lives in
`../shared/protocol.h` and is shared with the firmware -- it is the single
source of truth for both sides, not a hand-synced copy.

## Building

Requires CMake 3.16+, a C11 compiler, and Node.js/npm (for the `ui/`
frontend -- CMake shells out to `npm install`/`npm run build` as part of
the build, see below). `webview` is fetched automatically via
`FetchContent` at configure time (needs network access once).

```
cmake -S . -B build
cmake --build build
```

### The UI build (`client/ui/`)

`ui/` is a Vite + React + TypeScript + Tailwind v4 + shadcn/ui project with
its own `package.json`; CMake drives `npm install` and `npm run build` for
you (gated on file-content dependencies, so it only reruns when something
under `ui/` actually changed) and copies `ui/dist/` next to the executable.
You generally don't need to run npm yourself, but `cd ui && npm run dev`
works for iterating on the UI alone (with fake/no native data, since
`window.native_*` and `window.onState` only exist inside the real webview).

The build produces a **single self-contained `index.html`**
(`vite-plugin-singlefile`, all JS/CSS/fonts inlined) rather than Vite's
normal multi-file output, and a postbuild step
(`ui/scripts/postbuild.mjs`) strips `type="module"` and rewrites the entry
script to run on `DOMContentLoaded`. Both are required, not cosmetic: the
webview loads the app via a `file://` URL, and WebKit (confirmed via
WKWebView -- window opens, page stays blank, no visible error) refuses to
execute `<script type="module">` when the document's origin is `file://`,
module or not, imports or not. If you ever see a blank window, this is the
first thing to check -- inspect `ui/dist/index.html`'s script tag.

Per-OS system prerequisites for the webview backend:
- **Linux**: `webkit2gtk-4.1` + `gtk3` development packages (e.g.
  `libwebkit2gtk-4.1-dev` on Debian/Ubuntu), plus `libpipewire-0.3-dev` for
  the audio backend.
- **macOS**: none beyond Xcode command line tools (WKWebView/Cocoa/CoreAudio
  are system frameworks). Requires macOS 14.2+ for the Core Audio process-tap
  audio backend (see "Known gaps / follow-ups" below) -- everything else
  works on older versions too.
- **Windows**: the WebView2 SDK is handled by webview's own CMake; the
  WebView2 *runtime* is preinstalled on Windows 11 and most Windows 10
  machines (auto-provisioned via Windows Update).
- **Linux keystroke macros** additionally need `libx11-dev` + `libxtst-dev`
  (X11 XTest extension).

## The UI

The main window is a 2D top-down mockup of the physical pad, matching its
real layout: the display, then the row of 4 encoder knobs, then the 2x4
switch grid, with the detected audio sessions listed below. Every element
is clickable and opens a dialog with its configuration:
- **Display** -- reorders/toggles the on-device dashboard pieces (VU
  meters, waveform graph, macro grid) via `device_settings`'s `gui_layout`
  (`GuiComponent`/`GUI_COMPONENT_NONE` in `protocol.h`), pushed as part of
  `device_config_packet`.
- **Knob** -- which channel it controls (volume/mute) and what its press
  action does. Besides per-app sessions, "Master" is always selectable --
  the system's total output volume/mute (`kAudioDevicePropertyVolumeScalar`/
  `Mute` on macOS, `IAudioEndpointVolume` on Windows, the default sink node
  on PipeWire), bound via the reserved `MIXER_MASTER_SESSION_ID` rather than
  any one `AudioSession` from the live list.
- **Switch** -- its on-device label (shown in the switch's label box on the
  physical display, `MACRO_LABEL_LEN`-1 = 9 characters, truncated beyond
  that) and what pressing it does.

## Macro actions

Each of the 8 switches and 4 encoder buttons can be bound to an action from
its dialog's action editor:
- **Toggle mute** -- mutes/unmutes whichever session is assigned to a channel.
- **Send keystroke** -- synthesizes a system-wide keystroke *sequence* (up
  to `MACRO_KEYSTROKE_MAX_STEPS`, currently 64) via
  `platform/keyboard_inject.h`, e.g. Cmd+C, Cmd+V, Cmd+/ as three steps
  each with their own modifiers. Steps run in order with a short delay
  between them (`MACRO_KEYSTROKE_STEP_DELAY_MS` in `device_link.c`) so the
  target app registers each one distinctly. The action editor's
  `KeystrokeEditor` offers two ways to build the sequence, toggled with the
  record/keyboard icons in its corner:
  - **Record** (default) -- click the box, then just press the keys: each
    completed combo (e.g. actually holding Cmd and pressing C) appends one
    step immediately, live, in order. Click away to stop. Every physical
    key is captured as pressed, including Backspace/Delete/Escape
    themselves (all valid macro keys) -- fix a mistake with the X on that
    step's chip rather than a keyboard shortcut. See
    `ui/src/lib/keystrokeCapture.ts` for the `KeyboardEvent.code` ->
    macro-key mapping (layout-independent; keyed off the physical key, not
    the character it produces).
  - **Type** -- the previous free-text field, e.g. `cmd+c cmd+v cmd+/`
    (space-separated steps, modifiers joined to a key with `+`; see
    `ui/src/lib/keystrokeParser.ts` for the exact grammar and aliases --
    besides letters/digits/named keys it also supports punctuation:
    period, comma, slash, semicolon, quote, minus, equal, and both
    brackets). Still useful for keys awkward to physically press in a
    browser context, or quick edits.

  Both modes commit to the same step list, so switching between them
  mid-edit never loses anything already recorded/typed.
  Requires OS permission on macOS (see below) and an X11/XWayland session
  on Linux (see below).
- **Log (test)** -- always "succeeds" and just logs to stderr; no audio
  session or OS permission required, useful for confirming a physical
  button reaches the app at all.

Encoder rotation (volume up/down) is wired directly to its channel and
doesn't go through the macro map.

**macOS Accessibility permission**: keystroke injection uses
`CGEventPost`, which macOS gates behind Accessibility access. The first
attempt triggers the system's permission dialog; until you grant it
(System Settings -> Privacy & Security -> Accessibility -> add/enable
TinyPad), every keystroke macro fails and logs why.

**Linux Wayland caveat**: keystroke injection uses the X11 XTest
extension, which works under X11 and XWayland but has no equivalent on a
native (non-XWayland) Wayland session -- `XOpenDisplay` will fail and log
why. A `/dev/uinput`-based backend would work under both but needs
elevated/udev-granted permission, so it's a follow-up rather than the
default.

## Running

```
./tinypad [serial-device-path]
```

With no argument (and no `TINYPAD_DEVICE` set), the app auto-discovers the
TinyPad by USB identity (vendor id + product string, falling back to
vendor+product id) and connects with no path needed. Passing a path
explicitly (or setting `TINYPAD_DEVICE`) skips discovery and always (re)opens
that exact path instead -- useful for development or picking a specific
device out of several. Either way, `device_link`'s connect/reconnect logic
retries every ~1.5s whenever no device is attached, including after an
unplug, so plugging the device in (or back in) picks it up automatically.
Typical manual paths: `/dev/ttyACM0` (Linux), `/dev/cu.usbmodemXXXX`
(macOS), `COM5` (Windows).

Every button/encoder event received from the device is logged to stderr
(what was pressed, and what it did or why it didn't) -- useful for
confirming the physical device is actually talking to the app.

### System tray / background mode

Closing the window doesn't quit the app -- it destroys the webview/JS engine
(freeing that memory) and drops to a tray/menu-bar icon, while the mixer and
USB connection keep running in the background exactly as before. From the
tray: **Show** recreates the window, **Quit** shuts everything down. On
Linux, the tray icon uses `GtkStatusIcon`, which does not render on stock
GNOME Shell without a third-party extension ("AppIndicator and
KStatusNotifierItem Support", "TopIcons", ...) -- it works out of the box on
KDE Plasma, XFCE, and GNOME with that extension installed.

The firmware notices the app going away independently of this: it reverts to
its idle splash screen if no levels packet has arrived in 750ms, whether the
app was cleanly quit, crashed, or the cable was unplugged.

### Startup settle window

`device_link` ignores (and logs) any command received within
`STARTUP_GRACE_MS` (500ms) of opening the port. This covers two real
failure modes: bytes already queued in the OS serial buffer from before the
app started reading, and firmware-side electrical transients right after
power-on. The firmware side has a matching fix: `input_manager` waits
`STARTUP_SETTLE_MS` (50ms) after configuring GPIOs, then seeds its debounce
state from the actual current pin readings instead of assuming everything
starts unpressed, so a transient during that window can't look like a real
button edge. The firmware half of this is code-reviewed and build-verified
but not electrically tested (no hardware on this dev machine) -- if you
still see spurious presses at boot, that's the first place to look.

## Testing

Two independent suites, neither needs real hardware:

```
cmake --build build --target tinypad_tests && ctest --test-dir build --output-on-failure
```
Exercises `core/` (protocol framing/checksums, `macro_map`,
`device_settings`, `mixer_state`, `device_discovery`'s VID/PID/product-string
matching) against a fake `audio_backend_vtable_t` (`tests/test_core.c`) -- no
webview, no real audio/keyboard backend, so it builds and runs identically
on every OS.

```
cd ui && npm test
```
Runs the frontend's `vitest` suite -- the keystroke text parser's grammar
(`src/lib/keystrokeParser.test.ts`: token/sequence parsing, modifier
aliases, and the parse/stringify round trip) and the keystroke *recorder*'s
`KeyboardEvent` -> macro-key mapping
(`src/lib/keystrokeCapture.test.ts`).

## Known gaps / follow-ups

- **macOS audio** (`src/platform/audio/audio_macos_coreaudio.m`) uses Core
  Audio's process-tap API (macOS 14.2+), which has no native "set this
  process's volume" call -- session enumeration and peak metering are
  verified working against real audio (a standalone harness linking just
  this file showed correct add/remove events and live non-zero peak values
  from a real playing process), but volume/mute rely on muting the tapped
  process at the source and reinjecting a gain-scaled copy ourselves, which
  is build-verified only, not confirmed audibly correct end-to-end. The
  first process tap created in a run triggers a one-time system permission
  prompt ("record audio from other apps"); if denied, sessions still list
  but stay at 0 peak and volume/mute calls fail gracefully.
- **"Master" support end-to-end verified on macOS only** (a standalone
  harness exercising the real `mixer_state.c` + `audio_macos_coreaudio.m`
  together: assign a knob to Master, poll, set volume, toggle mute,
  confirmed against the real system output device and restored to its
  original value afterward). Master's peak meter uses a global process tap
  (every process, unmuted, metering only) -- but a real device on this
  machine showed empirically that a tap captures audio *before*
  `kAudioDevicePropertyVolumeScalar`'s attenuation, so the tapped peak is
  multiplied by the current volume in software (`coreaudio_get_master`);
  confirmed with continuous real audio that the meter now scales
  correctly (~20x lower peak at 5% volume vs. 100%). Windows
  (`IAudioEndpointVolume`/`IAudioMeterInformation`) and Linux (default
  sink node, tracked via PipeWire's "default.audio.sink" metadata)
  implementations are build-verified only, same standing caveat as the
  rest of those backends below -- in particular, whether their own meter
  reads pre- or post-volume is unverified, so Master's VU meter may or may
  not track the fader there yet.
- **Windows serial/WASAPI/tray/enumeration backends are not build-verified**
  on this repo's dev machine (no Windows toolchain available) -- review and
  test against real hardware before relying on them. The macOS/Linux POSIX
  serial path and the PipeWire backend logic were ported from the
  previously-working implementation.
- **Linux tray icon (`GtkStatusIcon`)** is deprecated upstream and invisible
  on stock GNOME Shell -- see the "System tray / background mode" note under
  Running.
- **Careful with bare names in the repo's root `.gitignore`**: Tailwind v4
  respects `.gitignore` when auto-detecting which files to scan for class
  names. A bare entry meant only for a compiled binary (already redundant
  with a `build` entry) once silently excluded an entire source directory
  that happened to share its name from the CSS build -- every class in that
  directory rendered in the DOM but never got a stylesheet rule, with no
  error anywhere. Fixed by removing the redundant entry; the lesson is to
  keep root `.gitignore` patterns anchored (e.g. `/client/build/tinypad`)
  rather than bare when they could plausibly collide with a source
  directory name.
