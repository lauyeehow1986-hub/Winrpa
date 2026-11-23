# winrpa

<!-- badges: start -->
[![GitHub Pages](https://img.shields.io/badge/docs-GitHub%20Pages-6366f1)](https://lauyeehow1986-hub.github.io/Winrpa/)
[![C++17](https://img.shields.io/badge/C%2B%2B-17%20%2F%20cpp11-38bdf8)](https://github.com/lauyeehow1986-hub/Winrpa)
[![License: MIT](https://img.shields.io/badge/License-MIT-34d399)](LICENSE)
<!-- badges: end -->

**Native Desktop UI Automation and Input Control for R** powered by pure C++17 and `cpp11` — zero Python, zero Java bridges, and zero runtime image dependencies.

**Interactive Documentation, Feature Matrix, Screenshots & Searchable API Explorer:**
[https://lauyeehow1986-hub.github.io/Winrpa/](https://lauyeehow1986-hub.github.io/Winrpa/)

![winrpa Live UIA and Multi-Mode Input Showcase](https://lauyeehow1986-hub.github.io/Winrpa/assets/live-notepad-automation.png)

## Key Features

- **Dedicated MTA COM UI Automation Worker:** Isolates `IUIAutomation` from R's STA GUI thread with O(1) cached property retrieval, bounded `rpa_inspect_tree()` snapshots, regex selectors (`rpa_selector()`), and `rpa_read_table()` grid extraction to `data.frame`.
- **Multi-Mode Input Injection & RAII Chords:** Supports `auto`, `value`, grouped UTF-16 `unicode` surrogate pairs (`>= 2ms` yield for RichEditD2DPT), hardware `scancode`, transactional `paste`, and `secret = TRUE` masking.
- **Per-Monitor V2 DPI & VirtualDesk Mouse Control:** Handles negative multi-monitor coordinates cleanly via `MOUSEEVENTF_VIRTUALDESK` (`0..65535`) and queries `GetDpiForMonitor()`.
- **Non-Destructive Clipboard Transactions:** `rpa_with_clipboard()` snapshots and restores clipboard contents on exit even on error, backed by 5×15 ms retry locking (`ClipboardGuard`).
- **Global Kill-Switch & Low-Level Hook Telemetry:** Registers system-wide `Ctrl+Alt+F12` (`RegisterHotKey`) flipping a lock-free atomic abort flag, plus `WH_KEYBOARD_LL` ring buffer telemetry.
- **Deterministic Waits, Timeout Dumps & Window Capture:** Polls conditions every `<= 50ms` with abort checks, dumps forensic `.log` + UIA trees on timeout (`options(winrpa.debug_dir = ...)`), and writes 24-bit BMP captures via `PrintWindow(PW_RENDERFULLCONTENT)` / `BitBlt`.
- **CRAN-Clean & Zero-Hang Lifecycle:** Safe `pkgload::unload()` / reload and clean process exit via `reg.finalizer(..., onexit = TRUE)` with portable non-Windows stubs.

## Installation

```r
# Install development version from GitHub (requires Rtools on Windows)
remotes::install_github("lauyeehow1986-hub/Winrpa")
```

## Quick Start

```r
library(winrpa)

# Launch Notepad visibly (unlike base::system2(), which defaults to invisible = TRUE on Windows)
rpa_launch("notepad.exe")

win <- rpa_wait_for(
  function() tryCatch(rpa_window(class_name = "Notepad"), error = function(e) NULL),
  timeout = 5,
  interval = 0.05,
  label = "Notepad window"
)

editor <- tryCatch(
  rpa_find(win, control_type = "Document"),
  error = function(e) rpa_find(win, control_type = "Edit")
)

editor |>
  rpa_type("Hello from winrpa! (Unicode: café 日本語)\n", mode = "unicode") |>
  rpa_type("Hardware scancode test 123\n", mode = "scancode", delay_ms = 5L)

bmp <- tempfile(pattern = "notepad_", fileext = ".bmp")
rpa_screenshot(win, path = bmp)
```

## Capturing Fresh Live Screenshots

To replace `docs/assets/live-notepad-automation.png` with a fresh capture from your live Windows 11 desktop, run from the repository root in R or RStudio:

```r
source("docs/capture_screenshots.R")
```
