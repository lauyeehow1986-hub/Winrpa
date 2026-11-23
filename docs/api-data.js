/* Complete API catalog covering all 44 exported functions + 4 S3 methods in winrpa */
window.WINRPA_API = [
  /* ===================== Windows & Selectors (12) ===================== */
  {
    name: "rpa_launch",
    category: "Windows & Selectors",
    signature: "rpa_launch(command, args = character(), wait = FALSE, minimized = FALSE, invisible = FALSE, env = character())",
    summary: "Starts a desktop application visibly (SW_SHOWNORMAL by default, unlike base::system2() which hides Windows GUI apps such as Windows 11 Notepad).",
    returns: "Invisibly returns the integer exit/status code from base::system2().",
    tags: ["Win32", "Session Guard", "Visible UI"],
    example: 'rpa_launch("notepad.exe")\nwin <- rpa_wait_for(\n  function() tryCatch(rpa_window(class_name = "Notepad"), error = function(e) NULL),\n  timeout = 8,\n  label = "Notepad window"\n)'
  },
  {
    name: "rpa_windows",
    category: "Windows & Selectors",
    signature: "rpa_windows()",
    summary: "Enumerates top-level windows visible to Microsoft UI Automation on the active desktop using the dedicated MTA COM worker thread.",
    returns: "A data.frame with columns name, class_name, automation_id, control_type, pid, hwnd, left, top, right, bottom, and enabled.",
    tags: ["MTA COM", "UIA"],
    example: 'wins <- rpa_windows()\nwins[, c("name", "class_name", "control_type", "pid", "hwnd")]'
  },
  {
    name: "rpa_window",
    category: "Windows & Selectors",
    signature: "rpa_window(title = NULL, class_name = NULL, pid = NULL, regex = FALSE)",
    summary: "Locates the first top-level desktop window matching optional title, Win32 class_name, or process ID criteria.",
    returns: "An object of class \"winrpa_element\". Throws an error if no matching window is found.",
    tags: ["MTA COM", "UIA", "Regex"],
    example: 'win <- rpa_window(class_name = "Notepad")\nrpa_window(title = ".*Notepad$", regex = TRUE)'
  },
  {
    name: "rpa_selector",
    category: "Windows & Selectors",
    signature: "rpa_selector(name = NULL, automation_id = NULL, control_type = NULL, class_name = NULL, regex = FALSE)",
    summary: "Constructs a reusable, pipe-friendly S3 selector specification for matching UI Automation controls.",
    returns: "An object of class \"winrpa_selector\".",
    tags: ["DSL", "Cross-Platform", "Regex"],
    example: 'save_btn <- rpa_selector(name = "Save", control_type = "Button")\nformat(save_btn)'
  },
  {
    name: "rpa_find",
    category: "Windows & Selectors",
    signature: "rpa_find(parent = NULL, selector = NULL, name = NULL, automation_id = NULL, control_type = NULL, class_name = NULL, regex = FALSE)",
    summary: "Searches the UI Automation subtree beneath parent (or desktop root) and returns the first matching control, failing clearly if none match.",
    returns: "A \"winrpa_element\" handle for the first match.",
    tags: ["MTA COM", "UIA", "Pipe-Friendly"],
    example: 'editor <- tryCatch(\n  rpa_find(win, control_type = "Document"),\n  error = function(e) rpa_find(win, control_type = "Edit")\n)'
  },
  {
    name: "rpa_find_all",
    category: "Windows & Selectors",
    signature: "rpa_find_all(parent = NULL, selector = NULL, name = NULL, automation_id = NULL, control_type = NULL, class_name = NULL, regex = FALSE, max_results = 100L)",
    summary: "Collects up to max_results matching UI Automation controls under a parent element or the desktop root.",
    returns: "A list of \"winrpa_element\" handles.",
    tags: ["MTA COM", "UIA"],
    example: 'buttons <- rpa_find_all(win, control_type = "Button", max_results = 25L)\nlength(buttons)'
  },
  {
    name: "rpa_element_info",
    category: "Windows & Selectors",
    signature: "rpa_element_info(target)",
    summary: "Retrieves O(1) cached UIA properties (bounds, HWND, PID, control type) and all supported UIA control patterns in a single MTA worker round-trip.",
    returns: "A named list: name, automation_id, control_type, class_name, pid, hwnd, left, top, right, bottom, enabled, patterns.",
    tags: ["MTA COM", "O(1) Cache"],
    example: 'info <- rpa_element_info(win)\ninfo[c("name", "class_name", "hwnd", "pid", "patterns")]'
  },
  {
    name: "rpa_inspect_tree",
    category: "Windows & Selectors",
    signature: "rpa_inspect_tree(target = NULL, max_depth = 3L, max_nodes = 200L)",
    summary: "Captures a bounded Control View snapshot of the UI Automation hierarchy for discovering names, control types, and automation IDs.",
    returns: "A data.frame of captured nodes including node_id, depth, control_type, name, and automation_id.",
    tags: ["MTA COM", "Discovery"],
    example: 'tree <- rpa_inspect_tree(win, max_depth = 3L, max_nodes = 50L)\nhead(tree[, c("node_id", "depth", "control_type", "name", "automation_id")], 15)'
  },
  {
    name: "format.winrpa_selector",
    category: "Windows & Selectors",
    signature: "format.winrpa_selector(x, ...)",
    summary: "Formats a winrpa_selector object into a concise human-readable summary string showing active filters and regex mode.",
    returns: "A character scalar representation of the selector.",
    tags: ["S3 Method", "Cross-Platform"],
    example: 'sel <- rpa_selector(name = "OK", control_type = "Button")\nformat(sel)'
  },
  {
    name: "print.winrpa_selector",
    category: "Windows & Selectors",
    signature: "print.winrpa_selector(x, ...)",
    summary: "Prints a formatted winrpa_selector specification to the console.",
    returns: "Invisibly returns x.",
    tags: ["S3 Method", "Cross-Platform"],
    example: 'sel <- rpa_selector(name = "OK", control_type = "Button")\nprint(sel)'
  },
  {
    name: "format.winrpa_element",
    category: "Windows & Selectors",
    signature: "format.winrpa_element(x, ...)",
    summary: "Formats a live winrpa_element handle with its UIA ControlType, Name, AutomationId, and native HWND (or detached status).",
    returns: "A character scalar describing the UI element.",
    tags: ["S3 Method", "MTA COM"],
    example: 'win <- rpa_window(class_name = "Notepad")\nformat(win)'
  },
  {
    name: "print.winrpa_element",
    category: "Windows & Selectors",
    signature: "print.winrpa_element(x, ...)",
    summary: "Prints a formatted winrpa_element summary to the R console.",
    returns: "Invisibly returns x.",
    tags: ["S3 Method", "MTA COM"],
    example: 'win <- rpa_window(class_name = "Notepad")\nprint(win)'
  },

  /* ===================== UI Actions & Tables (11) ===================== */
  {
    name: "rpa_click",
    category: "UI Actions & Tables",
    signature: "rpa_click(target, button = c(\"left\", \"right\", \"middle\"), clicks = 1L, method = c(\"auto\", \"invoke\", \"mouse\"))",
    summary: "Activates a UI element via UIA InvokePattern in auto/invoke mode, or scrolls into view and injects VirtualDesk-aware mouse clicks at its center.",
    returns: "Invisibly returns target for pipe chaining.",
    tags: ["MTA COM", "SendInput", "Pipe-Friendly"],
    example: 'save_btn <- rpa_find(win, name = "Save", control_type = "Button")\nrpa_click(save_btn)'
  },
  {
    name: "rpa_type",
    category: "UI Actions & Tables",
    signature: "rpa_type(target, text, mode = c(\"auto\", \"value\", \"unicode\", \"scancode\", \"paste\"), delay_ms = 5L, secret = FALSE)",
    summary: "Types text via ValuePattern, grouped UTF-16 KEYEVENTF_UNICODE surrogate pairs, hardware KEYEVENTF_SCANCODE, or transactional paste. Masks secrets and blocks mode=\"paste\" when secret=TRUE.",
    returns: "Invisibly returns target for pipe chaining.",
    tags: ["SendInput", "UTF-16 Surrogates", "Secret Safe"],
    example: 'editor |>\n  rpa_type("Hello! (Unicode: café 日本語)\\n", mode = "unicode") |>\n  rpa_type("Hardware scancode test 123\\n", mode = "scancode", delay_ms = 5L) |>\n  rpa_type("Clipboard paste OK\\n", mode = "paste")'
  },
  {
    name: "rpa_key_chord",
    category: "UI Actions & Tables",
    signature: "rpa_key_chord(keys, use_scancode = FALSE)",
    summary: "Dispatches a keyboard shortcut chord (such as c(\"ctrl\", \"s\")) guarded by C++17 RAII ModifierGuard so pressed modifiers unwind in LIFO order even on interrupt or error.",
    returns: "Invisibly returns NULL.",
    tags: ["SendInput", "RAII", "Safe Unwind"],
    example: 'rpa_key_chord(c("ctrl", "a"))\nrpa_key_chord(c("ctrl", "shift", "s"), use_scancode = TRUE)'
  },
  {
    name: "rpa_get_value",
    category: "UI Actions & Tables",
    signature: "rpa_get_value(target)",
    summary: "Reads the current text string from a control supporting UI Automation ValuePattern.",
    returns: "A character scalar.",
    tags: ["MTA COM", "UIA Pattern"],
    example: 'address_bar <- rpa_find(win, automation_id = "address")\nrpa_get_value(address_bar)'
  },
  {
    name: "rpa_set_value",
    category: "UI Actions & Tables",
    signature: "rpa_set_value(target, value, secret = FALSE)",
    summary: "Sets control text directly via UIA ValuePattern; when secret = TRUE, redacts value from any error messages.",
    returns: "Invisibly returns target for pipe chaining.",
    tags: ["MTA COM", "Secret Safe"],
    example: 'pwd <- rpa_find(win, automation_id = "passwordInput")\nrpa_set_value(pwd, Sys.getenv("APP_TOKEN"), secret = TRUE)'
  },
  {
    name: "rpa_toggle",
    category: "UI Actions & Tables",
    signature: "rpa_toggle(target)",
    summary: "Cycles a checkbox or toggle switch through UIA TogglePattern and returns the updated state.",
    returns: "Character scalar: \"off\", \"on\", or \"indeterminate\".",
    tags: ["MTA COM", "UIA Pattern"],
    example: 'wrap_chk <- rpa_find(win, name = "Word wrap", control_type = "CheckBox")\nstate <- rpa_toggle(wrap_chk)'
  },
  {
    name: "rpa_select",
    category: "UI Actions & Tables",
    signature: "rpa_select(target)",
    summary: "Selects a list item, tab, or radio option using UIA SelectionItemPattern.",
    returns: "Invisibly returns target.",
    tags: ["MTA COM", "UIA Pattern"],
    example: 'tab <- rpa_find(win, name = "Settings", control_type = "TabItem")\nrpa_select(tab)'
  },
  {
    name: "rpa_expand",
    category: "UI Actions & Tables",
    signature: "rpa_expand(target)",
    summary: "Expands a combo box, tree node, or accordion section via UIA ExpandCollapsePattern.",
    returns: "Invisibly returns target.",
    tags: ["MTA COM", "UIA Pattern"],
    example: 'combo <- rpa_find(win, control_type = "ComboBox")\nrpa_expand(combo)'
  },
  {
    name: "rpa_collapse",
    category: "UI Actions & Tables",
    signature: "rpa_collapse(target)",
    summary: "Collapses an expanded tree item, menu, or dropdown via UIA ExpandCollapsePattern.",
    returns: "Invisibly returns target.",
    tags: ["MTA COM", "UIA Pattern"],
    example: 'rpa_collapse(combo)'
  },
  {
    name: "rpa_scroll_into_view",
    category: "UI Actions & Tables",
    signature: "rpa_scroll_into_view(target)",
    summary: "Scrolls an off-screen control into the visible viewport using UIA ScrollItemPattern.",
    returns: "Invisibly returns target.",
    tags: ["MTA COM", "UIA Pattern"],
    example: 'row <- rpa_find(win, name = "Archive", control_type = "DataItem")\nrpa_scroll_into_view(row)'
  },
  {
    name: "rpa_read_table",
    category: "UI Actions & Tables",
    signature: "rpa_read_table(target)",
    summary: "Extracts an entire desktop DataGrid or Table control (headers + rows) via GridPattern and TablePattern directly into an R data.frame.",
    returns: "A character-column data.frame matching the UI grid dimensions and column headers.",
    tags: ["MTA COM", "Grid/Table Pattern"],
    example: 'grid <- rpa_find(win, control_type = "DataGrid")\ndf <- rpa_read_table(grid)\nhead(df)'
  },

  /* ===================== Mouse & Multi-Monitor DPI (7) ===================== */
  {
    name: "rpa_mouse_move",
    category: "Mouse & Multi-Monitor DPI",
    signature: "rpa_mouse_move(x, y)",
    summary: "Moves the cursor to absolute desktop pixel (x, y), automatically normalized for VirtualDesk negative secondary monitor coordinates.",
    returns: "Invisibly returns NULL.",
    tags: ["SendInput", "VirtualDesk"],
    example: 'rpa_mouse_move(-960L, 540L)'
  },
  {
    name: "rpa_mouse_click",
    category: "Mouse & Multi-Monitor DPI",
    signature: "rpa_mouse_click(x = NULL, y = NULL, button = c(\"left\", \"right\", \"middle\"), clicks = 1L)",
    summary: "Injects single or repeated mouse clicks at (x, y) using VirtualDesk absolute positioning, or at the current cursor location when x and y are NULL.",
    returns: "Invisibly returns NULL.",
    tags: ["SendInput", "VirtualDesk"],
    example: 'rpa_mouse_click(420L, 300L, button = "left", clicks = 2L)'
  },
  {
    name: "rpa_mouse_scroll",
    category: "Mouse & Multi-Monitor DPI",
    signature: "rpa_mouse_scroll(delta = -120L, horizontal = FALSE)",
    summary: "Emits vertical (MOUSEEVENTF_WHEEL) or horizontal (MOUSEEVENTF_HWHEEL) mouse scroll events via SendInput.",
    returns: "Invisibly returns NULL.",
    tags: ["SendInput", "Scroll"],
    example: 'rpa_mouse_scroll(delta = -240L)  # Scroll down 2 notches'
  },
  {
    name: "rpa_mouse_position",
    category: "Mouse & Multi-Monitor DPI",
    signature: "rpa_mouse_position()",
    summary: "Queries the current desktop cursor coordinates in virtual screen pixel space via Win32 GetCursorPos().",
    returns: "A named integer vector c(x = ..., y = ...).",
    tags: ["Win32", "DPI"],
    example: 'pos <- rpa_mouse_position()\npos'
  },
  {
    name: "rpa_display_metrics",
    category: "Mouse & Multi-Monitor DPI",
    signature: "rpa_display_metrics()",
    summary: "Reports virtual screen origin/span, all enumerated physical/work monitor rectangles, per-monitor effective DPI via GetDpiForMonitor(), and thread DPI awareness.",
    returns: "A named list containing dpi_awareness, virtual_left, virtual_top, virtual_width, virtual_height, and monitors.",
    tags: ["Per-Monitor DPI", "VirtualDesk"],
    example: 'm <- rpa_display_metrics()\nm$dpi_awareness'
  },
  {
    name: "rpa_try_enable_dpi_awareness",
    category: "Mouse & Multi-Monitor DPI",
    signature: "rpa_try_enable_dpi_awareness()",
    summary: "Attempts to opt the thread into Per-Monitor V2 DPI awareness while handling E_ACCESSDENIED gracefully when the host manifest (such as RStudio) already locked process DPI.",
    returns: "A named list indicating previous/updated DPI awareness and access status.",
    tags: ["Per-Monitor DPI", "E_ACCESSDENIED Safe"],
    example: 'dpi <- rpa_try_enable_dpi_awareness()\ndpi'
  },
  {
    name: "rpa_normalize_virtual_coords",
    category: "Mouse & Multi-Monitor DPI",
    signature: "rpa_normalize_virtual_coords(x, y, vx = 0L, vy = 0L, vw = 1920L, vh = 1080L)",
    summary: "Maps virtual screen coordinates (including negative values on monitors left/above primary) into exact 0..65535 integers for SendInput() MOUSEEVENTF_VIRTUALDESK. Works headlessly on all platforms.",
    returns: "Named integer vector c(norm_x = ..., norm_y = ...).",
    tags: ["VirtualDesk", "Cross-Platform"],
    example: 'rpa_normalize_virtual_coords(-960L, 540L, vx = -1920L, vy = 0L, vw = 3840L, vh = 1080L)\n# norm_x = 16388, norm_y = 32798'
  },

  /* ===================== Transactional Clipboard (5) ===================== */
  {
    name: "rpa_clipboard_get",
    category: "Transactional Clipboard",
    signature: "rpa_clipboard_get()",
    summary: "Reads CF_UNICODETEXT from the Windows clipboard using RAII ClipboardGuard (up to 5x15ms retry backoff against transient Excel/RDP locks).",
    returns: "Character scalar (UTF-8) or NULL if no Unicode text is available.",
    tags: ["ClipboardGuard", "UTF-16", "Retry Safe"],
    example: 'current <- rpa_clipboard_get()'
  },
  {
    name: "rpa_clipboard_set",
    category: "Transactional Clipboard",
    signature: "rpa_clipboard_set(text)",
    summary: "Writes UTF-8 text as CF_UNICODETEXT onto the Windows clipboard with RAII GlobalLock/CloseClipboard safety and 5x15ms retry backoff.",
    returns: "Invisibly returns NULL.",
    tags: ["ClipboardGuard", "UTF-16", "Retry Safe"],
    example: 'rpa_clipboard_set("my original clipboard text")'
  },
  {
    name: "rpa_clipboard_sequence",
    category: "Transactional Clipboard",
    signature: "rpa_clipboard_sequence()",
    summary: "Reads the monotonic Win32 GetClipboardSequenceNumber() counter without opening or locking the clipboard.",
    returns: "Integer scalar sequence counter.",
    tags: ["Win32", "Lock-Free Read"],
    example: 'seq_before <- rpa_clipboard_sequence()'
  },
  {
    name: "rpa_with_clipboard",
    category: "Transactional Clipboard",
    signature: "rpa_with_clipboard(expr, text = NULL)",
    summary: "Non-destructive clipboard transaction: snapshots existing clipboard text, registers an on.exit() restore hook, optionally sets temporary payload, evaluates expr, and restores original text even on error.",
    returns: "The evaluated result of expr.",
    tags: ["Transactional", "RAII / on.exit", "Zero Data Loss"],
    example: 'rpa_clipboard_set("my original clipboard text")\ninside <- rpa_with_clipboard({\n  rpa_clipboard_get()\n}, text = "temporary RPA payload")'
  },
  {
    name: "rpa_wait_for_clipboard",
    category: "Transactional Clipboard",
    signature: "rpa_wait_for_clipboard(timeout = 5, interval = 0.05, prev_seq = NULL)",
    summary: "Waits deterministically until the Windows clipboard sequence number advances and non-NULL text is available, honoring emergency kill-switch aborts.",
    returns: "Character scalar containing updated clipboard text.",
    tags: ["Wait", "Abort-Aware"],
    example: 'seq0 <- rpa_clipboard_sequence()\nrpa_key_chord(c("ctrl", "c"))\ncopied <- rpa_wait_for_clipboard(timeout = 5, prev_seq = seq0)'
  },

  /* ===================== Waits & Capture (4) ===================== */
  {
    name: "rpa_screenshot",
    category: "Waits & Capture",
    signature: "rpa_screenshot(target = NULL, path = tempfile(pattern = \"winrpa_\", fileext = \".bmp\"))",
    summary: "Captures a target window (via PrintWindow with PW_RENDERFULLCONTENT and BitBlt fallback) or the entire virtual desktop directly to a 24-bit BMP file with zero external R dependencies.",
    returns: "Character scalar path to the written .bmp image.",
    tags: ["GDI / PrintWindow", "24-bit BMP", "Zero Deps"],
    example: 'bmp <- tempfile(pattern = "notepad_", fileext = ".bmp")\nrpa_screenshot(win, path = bmp)'
  },
  {
    name: "rpa_wait_for",
    category: "Waits & Capture",
    signature: "rpa_wait_for(fn, timeout = 5, interval = 0.05, label = \"condition\", debug_window = NULL)",
    summary: "Polls fn() at <=50ms chunks while checking the lock-free emergency abort flag. When options(winrpa.debug_dir = ...) is set, writes timestamped .log + UIA tree + optional .bmp on timeout.",
    returns: "The first truthy value returned by fn().",
    tags: ["Deterministic", "Abort-Aware", "Debug Dumps"],
    example: 'win <- rpa_wait_for(\n  function() tryCatch(rpa_window(class_name = "Notepad"), error = function(e) NULL),\n  timeout = 5,\n  interval = 0.05,\n  label = "Notepad window"\n)'
  },
  {
    name: "rpa_wait_until_gone",
    category: "Waits & Capture",
    signature: "rpa_wait_until_gone(fn, timeout = 5, interval = 0.05, label = \"element to disappear\", debug_window = NULL)",
    summary: "Polls fn() until it returns NULL or FALSE (for example waiting for a splash screen or modal dialog to close).",
    returns: "Invisibly returns TRUE once the target disappears.",
    tags: ["Deterministic", "Abort-Aware"],
    example: 'rpa_wait_until_gone(\n  function() tryCatch(rpa_window(title = "Loading..."), error = function(e) NULL),\n  timeout = 10\n)'
  },
  {
    name: "rpa_wait_until_enabled",
    category: "Waits & Capture",
    signature: "rpa_wait_until_enabled(target, timeout = 5, interval = 0.05, debug_window = NULL)",
    summary: "Waits until a UIA element's cached IsEnabled property becomes TRUE, capturing target diagnostics into winrpa.debug_dir if timed out.",
    returns: "Invisibly returns target for pipe chaining.",
    tags: ["MTA COM", "Deterministic"],
    example: 'submit_btn <- rpa_find(win, name = "Submit", control_type = "Button")\nrpa_wait_until_enabled(submit_btn, timeout = 10) |> rpa_click()'
  },

  /* ===================== Safety, Kill-Switch & Hooks (9) ===================== */
  {
    name: "rpa_session_check",
    category: "Safety, Kill-Switch & Hooks",
    signature: "rpa_session_check(allow_non_windows = FALSE)",
    summary: "Verifies pre-flight execution readiness: confirms Windows OS (unless allow_non_windows=TRUE), unlocked interactive desktop, and cleared emergency abort flag.",
    returns: "Invisibly returns TRUE or aborts with a clear diagnostic message.",
    tags: ["Session Guard", "Pre-Flight"],
    example: 'rpa_session_check()'
  },
  {
    name: "rpa_is_interactive_desktop",
    category: "Safety, Kill-Switch & Hooks",
    signature: "rpa_is_interactive_desktop()",
    summary: "Detects locked workstations or disconnected RDP sessions via Win32 OpenInputDesktop() so GUI actions fail fast instead of hanging silently.",
    returns: "Logical scalar (TRUE if an active interactive desktop is attached on Windows; FALSE otherwise).",
    tags: ["Win32", "Cross-Platform"],
    example: 'if (rpa_is_interactive_desktop()) {\n  nrow(rpa_windows())\n}'
  },
  {
    name: "rpa_enable_killswitch",
    category: "Safety, Kill-Switch & Hooks",
    signature: "rpa_enable_killswitch(modifiers = c(\"ctrl\", \"alt\"), key = \"F12\")",
    summary: "Registers a system-wide RegisterHotKey emergency stop on a dedicated background GetMessageW() message loop, setting the lock-free atomic abort flag when pressed.",
    returns: "Logical scalar indicating whether hotkey registration succeeded.",
    tags: ["Emergency Abort", "Atomic", "Background Pump"],
    example: 'rpa_enable_killswitch()  # Registers Ctrl + Alt + F12'
  },
  {
    name: "rpa_disable_killswitch",
    category: "Safety, Kill-Switch & Hooks",
    signature: "rpa_disable_killswitch()",
    summary: "Unregisters the global emergency kill-switch hotkey from the background message-pump thread.",
    returns: "Logical scalar indicating success.",
    tags: ["Emergency Abort", "Cleanup"],
    example: 'rpa_disable_killswitch()'
  },
  {
    name: "rpa_abort_triggered",
    category: "Safety, Kill-Switch & Hooks",
    signature: "rpa_abort_triggered()",
    summary: "Queries the global lock-free std::atomic<bool> emergency abort flag toggled by the OS hotkey.",
    returns: "Logical scalar (TRUE if aborted).",
    tags: ["Atomic", "Cross-Platform"],
    example: 'rpa_abort_triggered()'
  },
  {
    name: "rpa_reset_abort",
    category: "Safety, Kill-Switch & Hooks",
    signature: "rpa_reset_abort()",
    summary: "Clears the global emergency abort flag so subsequent automation steps may resume.",
    returns: "Invisibly returns NULL.",
    tags: ["Atomic", "Cross-Platform"],
    example: 'if (rpa_abort_triggered()) {\n  rpa_reset_abort()\n}'
  },
  {
    name: "rpa_hook_start",
    category: "Safety, Kill-Switch & Hooks",
    signature: "rpa_hook_start()",
    summary: "Installs a low-level WH_KEYBOARD_LL hook on the background message-pump thread that streams hardware keystrokes into a lock-free 2048-slot ring buffer.",
    returns: "Logical scalar indicating whether the hook started cleanly.",
    tags: ["WH_KEYBOARD_LL", "Ring Buffer"],
    example: 'rpa_hook_start()\nSys.sleep(3)\nevents <- rpa_hook_drain()\nrpa_hook_stop()'
  },
  {
    name: "rpa_hook_drain",
    category: "Safety, Kill-Switch & Hooks",
    signature: "rpa_hook_drain()",
    summary: "Drains all buffered key events recorded by WH_KEYBOARD_LL without blocking the hook callback.",
    returns: "A data.frame with columns vk_code, scan_code, key_up, and timestamp_ms.",
    tags: ["WH_KEYBOARD_LL", "Lock-Free Telemetry"],
    example: 'events <- rpa_hook_drain()\nprint(events)'
  },
  {
    name: "rpa_hook_stop",
    category: "Safety, Kill-Switch & Hooks",
    signature: "rpa_hook_stop()",
    summary: "Cleanly unhooks WH_KEYBOARD_LL from the background message-pump thread.",
    returns: "Logical scalar indicating success.",
    tags: ["WH_KEYBOARD_LL", "Cleanup"],
    example: 'rpa_hook_stop()'
  }
];
