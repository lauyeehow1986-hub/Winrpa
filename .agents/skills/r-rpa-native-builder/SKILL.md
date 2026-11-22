---
name: r-rpa-native-builder
description: Build, debug, and verify CRAN-compliant native C++17 (cpp11) desktop RPA R packages on Windows (Rtools45/MinGW-w64) with MTA COM UI Automation, VirtualDesk/per-monitor DPI SendInput, PrintWindow/DXGI capture, global hotkey kill-switch, and headless CRAN testing.
---
# Native C++ Desktop RPA Builder for R (`winrpa`)

Use this skill whenever designing, implementing, debugging, or testing a native C++ desktop RPA package for R in Antigravity IDE.

## Non-Negotiable Architecture Rules

1. **Dedicated MTA COM Worker Thread (Never Touch R's Main STA Thread)**
   - Rgui, RStudio, and other R packages often initialize R's main thread as STA (`COINIT_APARTMENTTHREADED`), causing `CoInitializeEx(NULL, COINIT_MULTITHREADED)` to fail with `RPC_E_CHANGED_MODE`.
   - Spawn a single persistent C++ worker thread that calls `CoInitializeEx(NULL, COINIT_MULTITHREADED)` once, processes queued UIA commands via `std::mutex` + `std::condition_variable`, and calls `CoUninitialize()` cleanly on package unload (`.onUnload`).
   - Every `cpp11::external_pointer` wrapping an `IUIAutomationElement*` must dispatch `->Release()` back to the MTA worker thread in its custom deleter so GC never releases a COM pointer on the wrong apartment.

2. **MinGW-w64 / Rtools45 Compatibility**
   - Always probe `uiautomationclient.h` with `probe_rtools_uia_toolchain()` first.
   - Link `-lole32 -loleaut32 -luuid -loleacc -luser32 -lgdi32 -lshcore` in `src/Makevars.win`.
   - Never rely on MSVC-only extensions (`#pragma comment(lib, ...)`, `_com_ptr_t`, ATL/WRL). Use standard C++17 RAII smart pointers for COM interfaces.

3. **Per-Monitor DPI & Multi-Monitor VirtualDesk Coordinates**
   - Never assume `SetProcessDpiAwarenessContext` succeeds; RStudio/Qt manifests lock it at startup and return `E_ACCESSDENIED`.
   - Use `GetSystemMetrics(SM_XVIRTUALSCREEN)`, `SM_YVIRTUALSCREEN`, `SM_CXVIRTUALSCREEN`, `SM_CYVIRTUALSCREEN` and `MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK` for all absolute mouse moves so negative coordinates on secondary monitors map accurately.

4. **Input Safety, Scan Codes & Non-Destructive Clipboard**
   - Support both `KEYEVENTF_UNICODE` and hardware scan codes (`KEYEVENTF_SCANCODE` via `MapVirtualKeyExW`) for legacy ERP/Citrix/RDP targets.
   - Wrap modifier chords (`Ctrl`/`Alt`/`Shift`/`Win`) in an RAII guard that emits key-up events on C++ exception or R interrupt.
   - Implement `OpenClipboard` retry-with-backoff (5 attempts, 15ms sleep) and a snapshot/restore `with_clipboard()` transaction.
   - Never log keystrokes when `secret = TRUE`.

5. **UIA Performance & Patterns**
   - Always batch tree queries using `IUIAutomationCacheRequest` + `TreeScope_Subtree`.
   - Implement `Invoke`, `Value`, `Toggle`, `SelectionItem`, `ExpandCollapse`, `ScrollItem`, and `Grid`/`Table` patterns.

6. **Capture, Hooks & Kill-Switch**
   - Prefer `PrintWindow(hwnd, hdc, PW_RENDERFULLCONTENT)` over bare `BitBlt` for occluded/Electron/GPU windows.
   - Run `RegisterHotKey` (emergency abort setting `std::atomic<bool> g_abort`) and `SetWindowsHookEx` on a dedicated thread running a `GetMessage` pump; keep hook callbacks lock-free and under 50ms.
   - Check `g_abort.load()` and `cpp11::check_user_interrupt()` every 50ms inside all polling/wait loops.

7. **CRAN Policy & Cross-Platform Cleanliness**
   - Guard all Windows headers and Win32 code with `#ifdef _WIN32` and provide informative error stubs on macOS/Linux.
   - Enforce `#define R_NO_REMAP`, zero `std::cout`/`printf`/`exit`/`abort`, and zero disabling of compiler warnings.
   - Keep `testthat` unit tests 100% headless-safe on CRAN; gate live desktop tests with `skip_if_not(interactive())` and examples with `@examplesIf interactive() && .Platform$OS.type == "windows"`.
   - Pair with `cran-six-sigma-guardian` (if installed) for the final 7-gate CRAN audit before release.
