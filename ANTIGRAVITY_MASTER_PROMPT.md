# Antigravity IDE Master Prompt: Build `winrpa` (Modern Native C++ RPA Package for R)

> **Recommended Antigravity Model Setting:** Select **`Orchestrator (Neon)`** in the model dropdown.
> - **Phase 1 (Read-Only Plan):** Gemini 4 Neon inspects the workspace, probes Rtools45/MinGW-w64 headers via MCP, and locks a verified build plan.
> - **Phase 2 (Fast Execute):** Gemini 4 Hydrogen scaffolds and compiles the C++17 / `cpp11` package, R selector DSL, and mock `testthat` suite.
> - **Phase 3 (Auto Re-Plan & Hardening):** If any MinGW-w64 COM/UUID link error, `R CMD check` NOTE/WARNING, or live DPI test fails, Orchestrator automatically escalates back to Neon to repair the architecture.

---

## Copy-Paste Prompt for Antigravity IDE

```text
You are building a modern, CRAN-ready, zero-JVM / zero-Python native desktop RPA package for R named `winrpa` (C++17 via `cpp11`), designed to solve every gap left by `KeyboardSimulator` and `rMouse`.

### Workspace Assets Already Available
- Project skill: `.agents/skills/r-rpa-native-builder/SKILL.md` (load and follow strictly)
- Starter scaffold: `starter_pkg/` (promote/expand into the repo root or build directly inside `starter_pkg/`)
- Local verification MCP server: `rpa-dev-mcp` (`mcp_server/rpa_dev_mcp.py`)
- If `cran-six-sigma-mcp` / `cran-six-sigma-guardian` is also active in this workspace, run its 7-gate CRAN audit before finishing.

### Phase 1 — Read-Only Environment & Architecture Plan (Do NOT edit files until plan is locked)
1. Call `probe_rtools_uia_toolchain()` via `rpa-dev-mcp` to verify the installed R version, Rtools44/45 `g++` version, and whether MinGW-w64 UCRT64 compiles `uiautomationclient.h` + `CLSID_CUIAutomation` cleanly or requires inline GUID definitions.
2. Call `inspect_active_windows_and_dpi()` via `rpa-dev-mcp` to inspect current monitor geometry, virtual desktop bounds, and DPI scale factors.
3. Inspect `starter_pkg/` and `.agents/skills/r-rpa-native-builder/SKILL.md`.
4. Lock a phased build plan with explicit pass/fail gates and re-plan triggers for:
   - **Core C++17 Runtime (`src/`)**:
     - Dedicated background **MTA COM worker thread** (`mta_worker.cpp`) for all `IUIAutomation` calls (never call `CoInitializeEx(COINIT_MULTITHREADED)` on R's main STA thread), with apartment-safe `cpp11::external_pointer` release finalizers and `IUIAutomationCacheRequest` batched tree snapshots.
     - **DPI & Multi-Monitor Input Engine (`input_dpi.cpp`)**: Never assume `SetProcessDpiAwarenessContext` succeeds (host Rgui/RStudio may return `E_ACCESSDENIED`); query active awareness and use per-monitor coordinate translation + `MOUSEEVENTF_VIRTUALDESK` normalization against `SM_XVIRTUALSCREEN`/`SM_YVIRTUALSCREEN`. Support `KEYEVENTF_UNICODE`, hardware scan codes (`KEYEVENTF_SCANCODE`), RAII stuck-modifier cleanup, and non-destructive `with_clipboard()` transactions with `OpenClipboard` retry backoff.
     - **Control Patterns & Capture**: Support `Invoke`, `Value`, `Toggle`, `SelectionItem`, `ExpandCollapse`, `ScrollItem`, and `Grid`/`Table` (direct `data.frame` extraction), plus `PrintWindow(..., PW_RENDERFULLCONTENT)` fallback for GPU/Electron windows where `BitBlt` returns black.
     - **Safety & Runtime Hooks**: Dedicated background message-loop thread (`GetMessage`) for global `RegisterHotKey` emergency kill-switch (`std::atomic<bool>`) and low-level hooks (lock-free ring buffer, strictly < 50ms callback), plus `cpp11::check_user_interrupt()` every 50ms in all wait loops.
     - **Cross-Platform Guards**: Isolate all Windows headers/APIs behind `#ifdef _WIN32` so macOS and Linux compile cleanly with informative unsupported-OS stubs.
   - **R Control Plane (`R/`)**:
     - Pipe-friendly Selector DSL (`rpa_window()` |> `rpa_find()` |> `rpa_click()` / `rpa_type(secret = FALSE)` / `rpa_read_table()`).
     - Deterministic waits (`rpa_wait_for()`, `rpa_wait_until_gone()`, `rpa_wait_until_enabled()`, `rpa_wait_for_clipboard()`).
     - Opt-in failure artifact capture (`options(winrpa.debug_dir = ...)` dumping screenshot + pruned UIA tree on timeout).
     - Detached/locked desktop session detection (`OpenInputDesktop`) that fails fast with an actionable message.

### Phase 2 — Build & Automated Verification Gates
1. Implement the C++17 backend and R API cleanly using `cpp11` (`#define R_NO_REMAP`, zero `std::cout`/`printf`/`exit`/`abort`).
2. Write `testthat` unit tests that run **100% headlessly** on CRAN check servers (mocking selector logic, VirtualDesk coordinate math, RAII key-guard state, and cross-platform error paths), and guard any live desktop integration tests with `skip_if_not(interactive())`.
3. Guard all `@examples` that touch the desktop with `@examplesIf interactive() && .Platform$OS.type == "windows"`.
4. After each milestone, run:
   - `audit_cpp_cran_hygiene(src_dir)` via `rpa-dev-mcp` (must return `0` critical issues)
   - `run_r_package_check(pkg_path, as_cran=True)` via `rpa-dev-mcp` (must return `0 errors | 0 warnings | 0 unexplained notes`)
   - If `cran-six-sigma-mcp` is connected, run `run_six_sigma_sieve(pkg_path, strict_mode=True)` until `100/100`.

### Re-Plan Triggers (Stop and Re-Architect Immediately If Hit)
- MinGW-w64 fails to resolve `CLSID_CUIAutomation` or `IUIAutomation` headers -> switch to self-contained COM IID/CLSID definitions in `src/uia_guids.h` and verify linkage with `-lole32 -loleaut32 -luuid`.
- Any test mutates global R state (`options()`, `tempdir()`, `.GlobalEnv`) or hangs waiting for UI input -> isolate with `on.exit()` and headless mocks.
- Any C++ compiler warning under `-Wall -Wextra -Wconversion` -> fix root cause without disabling compiler diagnostics.

Start with Phase 1 now and present your locked plan before modifying source files.
```
