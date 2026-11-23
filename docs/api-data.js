/* Complete API catalog covering all 77 exported functions + 10 S3 methods in winrpa */
window.WINRPA_API = [
  /* ===================== Windows & Selectors (17) ===================== */
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
    name: "rpa_window_focus",
    category: "Windows & Selectors",
    signature: "rpa_window_focus(target)",
    summary: "Restores a minimized window if needed, brings its native HWND to the foreground (with AttachThreadInput fallback), and focuses the target element via UIA SetFocus().",
    returns: "Invisibly returns target for pipe chaining.",
    tags: ["Window Lifecycle", "Win32 HWND", "Pipe-Friendly"],
    example: 'win <- rpa_window(class_name = "Notepad")\nrpa_window_focus(win)'
  },
  {
    name: "rpa_window_maximize",
    category: "Windows & Selectors",
    signature: "rpa_window_maximize(target)",
    summary: "Maximizes the target window via UIA WindowPattern::SetWindowVisualState(Maximized) with ShowWindow(SW_MAXIMIZE) HWND fallback.",
    returns: "Invisibly returns target for pipe chaining.",
    tags: ["WindowPattern", "Win32 HWND", "Pipe-Friendly"],
    example: 'win |>\n  rpa_window_focus() |>\n  rpa_window_maximize()'
  },
  {
    name: "rpa_window_minimize",
    category: "Windows & Selectors",
    signature: "rpa_window_minimize(target)",
    summary: "Minimizes the target window via UIA WindowPattern with ShowWindow(SW_MINIMIZE) HWND fallback.",
    returns: "Invisibly returns target for pipe chaining.",
    tags: ["WindowPattern", "Win32 HWND", "Pipe-Friendly"],
    example: 'rpa_window_minimize(win)'
  },
  {
    name: "rpa_window_restore",
    category: "Windows & Selectors",
    signature: "rpa_window_restore(target)",
    summary: "Restores a maximized or minimized window to normal bounds via UIA WindowPattern with ShowWindow(SW_RESTORE) HWND fallback.",
    returns: "Invisibly returns target for pipe chaining.",
    tags: ["WindowPattern", "Win32 HWND", "Pipe-Friendly"],
    example: 'rpa_window_restore(win)'
  },
  {
    name: "rpa_window_close",
    category: "Windows & Selectors",
    signature: "rpa_window_close(target)",
    summary: "Requests graceful window closure via UIA WindowPattern::Close() with PostMessageW(WM_CLOSE) HWND fallback.",
    returns: "Invisibly returns NULL.",
    tags: ["WindowPattern", "Win32 HWND"],
    example: 'rpa_window_close(win)'
  },
  {
    name: "rpa_selector",
    category: "Windows & Selectors",
    signature: "rpa_selector(name = NULL, automation_id = NULL, control_type = NULL, class_name = NULL, regex = FALSE, exact = FALSE)",
    summary: "Constructs a reusable, pipe-friendly S3 selector specification. Setting exact = TRUE matches name and class_name by case-insensitive equality so UIA can filter server-side.",
    returns: "An object of class \"winrpa_selector\".",
    tags: ["DSL", "Cross-Platform", "Exact/Regex"],
    example: 'save_btn <- rpa_selector(name = "Save", control_type = "Button", exact = TRUE)\nformat(save_btn)'
  },
  {
    name: "rpa_find",
    category: "Windows & Selectors",
    signature: "rpa_find(parent = NULL, selector = NULL, name = NULL, automation_id = NULL, control_type = NULL, class_name = NULL, regex = FALSE, exact = FALSE, max_depth = NULL)",
    summary: "Searches the UI Automation subtree beneath parent (or desktop root), pushing control_type, automation_id, and exact = TRUE conditions server-side with a FindFirstBuildCache fast path and optional bounded max_depth BFS.",
    returns: "A \"winrpa_element\" handle for the first match.",
    tags: ["MTA COM", "Server-Side UIA", "Bounded BFS"],
    example: 'editor <- tryCatch(\n  rpa_find(win, control_type = "Document", max_depth = 4L),\n  error = function(e) rpa_find(win, control_type = "Edit")\n)'
  },
  {
    name: "rpa_find_all",
    category: "Windows & Selectors",
    signature: "rpa_find_all(parent = NULL, selector = NULL, name = NULL, automation_id = NULL, control_type = NULL, class_name = NULL, regex = FALSE, exact = FALSE, max_results = 100L, max_depth = NULL)",
    summary: "Collects up to max_results matching UI Automation controls using server-side property conditions or ControlViewWalker bounded BFS when max_depth is supplied.",
    returns: "A list of \"winrpa_element\" handles.",
    tags: ["MTA COM", "Server-Side UIA", "Bounded BFS"],
    example: 'buttons <- rpa_find_all(win, control_type = "Button", max_depth = 4L, max_results = 25L)\nlength(buttons)'
  },
  {
    name: "rpa_element_info",
    category: "Windows & Selectors",
    signature: "rpa_element_info(target)",
    summary: "Retrieves O(1) cached UIA properties (bounds, HWND, PID, control type) and all 9 supported UIA control patterns (including Window) in a single MTA worker round-trip.",
    returns: "A named list: name, automation_id, control_type, class_name, pid, hwnd, left, top, right, bottom, enabled, patterns.",
    tags: ["MTA COM", "O(1) Cache", "9 Patterns"],
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
    summary: "Activates a UI element via UIA InvokePattern in auto/invoke mode, or scrolls off-screen controls into view and re-queries the updated bounding rectangle center before dispatching VirtualDesk-aware mouse clicks.",
    returns: "Invisibly returns target for pipe chaining.",
    tags: ["MTA COM", "Post-Scroll Re-Query", "SendInput"],
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
  {
    name: "rpa_post_click",
    category: "UI Actions & Tables",
    signature: "rpa_post_click(target, button = c(\"left\", \"right\"), x = NULL, y = NULL)",
    summary: "Sends non-stealing WM_LBUTTONDOWN/UP or WM_RBUTTONDOWN/UP messages directly to a background Win32 HWND via PostMessageW() without moving the physical mouse cursor or claiming foreground focus.",
    returns: "Invisibly returns target for pipe chaining.",
    tags: ["Background HWND", "PostMessageW", "Non-Stealing"],
    example: 'win <- rpa_window(class_name = "Notepad")\nrpa_post_click(win)'
  },
  {
    name: "rpa_post_text",
    category: "UI Actions & Tables",
    signature: "rpa_post_text(target, text, secret = FALSE)",
    summary: "Streams UTF-16 WM_CHAR messages directly to a background Win32 control HWND via PostMessageW() without touching SendInput or the Windows clipboard.",
    returns: "Invisibly returns target for pipe chaining.",
    tags: ["Background HWND", "PostMessageW", "Secret Safe"],
    example: 'rpa_post_text("0x1234", "Background text line\n", secret = FALSE)'
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
  {
    name: "rpa_wait_window",
    category: "Waits & Debug",
    signature: "rpa_wait_window(title = NULL, class_name = NULL, pid = NULL, regex = FALSE, timeout = 10, interval = 0.1)",
    summary: "Polls rpa_window() safely while swallowing expected not-found errors until the target top-level window appears or timeout expires.",
    returns: "A \"winrpa_element\" handle for the matched window.",
    tags: ["Wait Helper", "Window Lookup", "Abort-Aware"],
    example: 'win <- rpa_wait_window(title = "Untitled - Notepad", timeout = 8) |> rpa_window_focus()'
  },
  {
    name: "rpa_wait_find",
    category: "Waits & Debug",
    signature: "rpa_wait_find(parent = NULL, selector = NULL, name = NULL, automation_id = NULL, control_type = NULL, class_name = NULL, regex = FALSE, exact = FALSE, max_depth = NULL, timeout = 10, interval = 0.1)",
    summary: "Polls rpa_find() deterministically until a matching child control appears, capturing debug_window diagnostics if timed out.",
    returns: "A \"winrpa_element\" handle for the matched control.",
    tags: ["Wait Helper", "Server-Side UIA", "Abort-Aware"],
    example: 'ed <- rpa_wait_find(win, automation_id = "RichEditD2DPT", control_type = "Document", timeout = 8)'
  },

  /* ===================== Safety, Kill-Switch & Hooks (9) ===================== */
  {
    name: "rpa_session_check",
    category: "Safety, Kill-Switch & Hooks",
    signature: "rpa_session_check(allow_non_windows = FALSE, require_interactive = TRUE)",
    summary: "Verifies pre-flight execution readiness: confirms Windows OS (unless allow_non_windows=TRUE), cleared emergency abort flag, and active desktop when require_interactive=TRUE (headless CDP and background PostMessageW pass FALSE).",
    returns: "Invisibly returns TRUE or aborts with a clear diagnostic message.",
    tags: ["Session Guard", "Pre-Flight"],
    example: 'rpa_session_check()\nrpa_session_check(require_interactive = FALSE)'
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
  },

  /* ===================== Web Browser CDP (Edge/Chrome) (20) ===================== */
  {
    name: "rpa_web_launch",
    category: "Web Browser CDP (Edge/Chrome)",
    signature: "rpa_web_launch(browser = c(\"auto\", \"edge\", \"chrome\"), headless = TRUE, url = \"about:blank\", port = NULL, args = character(), timeout = 15)",
    summary: "Launches Microsoft Edge or Google Chrome in headless (--headless=new) or visible mode with a dedicated remote debugging port and connects over native WinHTTP WebSockets.",
    returns: "A \"winrpa_web_session\" handle.",
    tags: ["WinHTTP WS", "Headless CDP", "Zero R Deps"],
    example: 'sess <- rpa_web_launch(browser = "auto", headless = TRUE)\nrpa_web_close(sess)'
  },
  {
    name: "rpa_web_connect",
    category: "Web Browser CDP (Edge/Chrome)",
    signature: "rpa_web_connect(port = 9222L, host = \"127.0.0.1\", target_index = 1L, timeout = 8)",
    summary: "Attaches directly to an already-running Edge or Chrome instance started with --remote-debugging-port over loopback HTTP + WinHTTP WebSocket.",
    returns: "A \"winrpa_web_session\" handle.",
    tags: ["WinHTTP WS", "Existing Browser"],
    example: 'sess <- rpa_web_connect(port = 9222L)'
  },
  {
    name: "rpa_web_close",
    category: "Web Browser CDP (Edge/Chrome)",
    signature: "rpa_web_close(session)",
    summary: "Gracefully closes the WinHTTP WebSocket session, terminates managed browser processes, and cleans up temporary profile directories.",
    returns: "Invisibly returns NULL.",
    tags: ["Zero-Hang Cleanup", "CDP"],
    example: 'rpa_web_close(sess)'
  },
  {
    name: "rpa_web_goto",
    category: "Web Browser CDP (Edge/Chrome)",
    signature: "rpa_web_goto(session, url, wait = c(\"load\", \"domcontentloaded\", \"none\"), timeout = 15)",
    summary: "Navigates via CDP Page.navigate and waits deterministically for Page.loadEventFired or Page.domContentEventFired.",
    returns: "Invisibly returns session for pipe chaining.",
    tags: ["Navigation", "DOMContentLoaded", "Load Event"],
    example: 'rpa_web_goto(sess, "https://example.org", wait = "load")'
  },
  {
    name: "rpa_web_find",
    category: "Web Browser CDP (Edge/Chrome)",
    signature: "rpa_web_find(session, css = NULL, xpath = NULL, index = 1L)",
    summary: "Finds a DOM element using standard CSS selectors (document.querySelectorAll) or XPath expressions (document.evaluate) at the 1-based index.",
    returns: "A \"winrpa_web_element\" handle.",
    tags: ["CSS", "XPath", "DOM"],
    example: 'btn <- rpa_web_find(sess, css = "button.submit")\nrpa_web_click(btn)'
  },
  {
    name: "rpa_web_find_all",
    category: "Web Browser CDP (Edge/Chrome)",
    signature: "rpa_web_find_all(session, css = NULL, xpath = NULL)",
    summary: "Finds all matching DOM elements for a CSS selector or XPath expression.",
    returns: "A list of \"winrpa_web_element\" handles.",
    tags: ["CSS", "XPath", "DOM"],
    example: 'links <- rpa_web_find_all(sess, css = "nav a")'
  },
  {
    name: "rpa_web_click",
    category: "Web Browser CDP (Edge/Chrome)",
    signature: "rpa_web_click(target, css = NULL, xpath = NULL, button = c(\"left\", \"right\", \"middle\"), clicks = 1L)",
    summary: "Scrolls a DOM element smoothly into center view and dispatches trusted CDP Input.dispatchMouseEvent events without touching your physical cursor.",
    returns: "Invisibly returns target for pipe chaining.",
    tags: ["Trusted Mouse", "Headless Safe", "CSS/XPath"],
    example: 'rpa_web_click(sess, xpath = "//button[text()=\'Submit\']")'
  },
  {
    name: "rpa_web_type",
    category: "Web Browser CDP (Edge/Chrome)",
    signature: "rpa_web_type(target, text, css = NULL, xpath = NULL, clear = FALSE, secret = FALSE)",
    summary: "Focuses a DOM input (optionally clearing value first) and inserts arbitrary Unicode text via CDP Input.insertText; masks secret=TRUE strings on error.",
    returns: "Invisibly returns target for pipe chaining.",
    tags: ["Input.insertText", "Secret Safe", "Headless Safe"],
    example: 'rpa_web_type(sess, "hello world", css = "#search", clear = TRUE)'
  },
  {
    name: "rpa_web_text",
    category: "Web Browser CDP (Edge/Chrome)",
    signature: "rpa_web_text(target, css = NULL, xpath = NULL, attr = NULL)",
    summary: "Reads rendered text (innerText / textContent) or a specific HTML attribute from a DOM element matching CSS or XPath.",
    returns: "Character scalar (or NULL if absent).",
    tags: ["CSS", "XPath", "Scraping"],
    example: 'rpa_web_text(sess, css = "h1")\nrpa_web_text(sess, css = "a.main", attr = "href")'
  },
  {
    name: "rpa_web_eval",
    category: "Web Browser CDP (Edge/Chrome)",
    signature: "rpa_web_eval(session, expression)",
    summary: "Evaluates arbitrary JavaScript in the page context via Runtime.evaluate (awaiting Promises) and parses JSON results natively in C++17.",
    returns: "The evaluated JS value converted cleanly to R primitives/lists.",
    tags: ["Runtime.evaluate", "Await Promise", "C++ JSON"],
    example: 'rpa_web_eval(sess, "document.title")'
  },
  {
    name: "rpa_web_wait_load",
    category: "Web Browser CDP (Edge/Chrome)",
    signature: "rpa_web_wait_load(session, state = c(\"load\", \"domcontentloaded\"), timeout = 15, interval = 0.05)",
    summary: "Waits until document.readyState reaches interactive/complete for domcontentloaded or complete for load, honoring the global kill-switch.",
    returns: "Invisibly returns session.",
    tags: ["DOMContentLoaded", "Abort-Aware"],
    example: 'rpa_web_wait_load(sess, state = "domcontentloaded")'
  },
  {
    name: "rpa_web_wait_network_idle",
    category: "Web Browser CDP (Edge/Chrome)",
    signature: "rpa_web_wait_network_idle(session, idle_ms = 500L, max_inflight = 0L, timeout = 15, interval = 0.05)",
    summary: "Tracks CDP Network.requestWillBeSent vs loadingFinished/loadingFailed events in C++ and waits until <= max_inflight requests remain active continuously for idle_ms ms.",
    returns: "Invisibly returns session.",
    tags: ["XHR/Fetch Idle", "Network Events"],
    example: 'rpa_web_wait_network_idle(sess, idle_ms = 500L, timeout = 15)'
  },
  {
    name: "rpa_web_wait_selector",
    category: "Web Browser CDP (Edge/Chrome)",
    signature: "rpa_web_wait_selector(session, css = NULL, xpath = NULL, visible = TRUE, timeout = 10, interval = 0.05)",
    summary: "Polls for a CSS or XPath selector until present (and visible with non-zero bounds when visible=TRUE), honoring the global kill-switch.",
    returns: "A \"winrpa_web_element\" handle.",
    tags: ["CSS/XPath Wait", "Deterministic"],
    example: 'el <- rpa_web_wait_selector(sess, css = "#results-ready", timeout = 10)'
  },
  {
    name: "rpa_web_screenshot",
    category: "Web Browser CDP (Edge/Chrome)",
    signature: "rpa_web_screenshot(session, path = tempfile(pattern = \"winrpa_web_\", fileext = \".png\"), full_page = FALSE)",
    summary: "Captures viewport or full-page lossless PNG screenshots via CDP Page.captureScreenshot and Base64-decodes directly in C++17.",
    returns: "Character scalar path to the written PNG file.",
    tags: ["PNG Capture", "Full Page", "C++ Base64"],
    example: 'rpa_web_screenshot(sess, path = "page.png", full_page = TRUE)'
  },
  {
    name: "rpa_web_set_files",
    category: "Web Browser CDP (Edge/Chrome)",
    signature: "rpa_web_set_files(target, files, css = NULL, xpath = NULL)",
    summary: "Populates an <input type=\"file\"> control directly via DOM.requestNode + DOM.setFileInputFiles without opening a native OS file picker dialog.",
    returns: "Invisibly returns target.",
    tags: ["Headless Upload", "DOM.setFileInputFiles"],
    example: 'rpa_web_set_files(sess, "report.pdf", css = "input[type=file]")'
  },
  {
    name: "rpa_web_window",
    category: "Web Browser CDP (Edge/Chrome)",
    signature: "rpa_web_window(session)",
    summary: "Bridges a visible (headless = FALSE) CDP browser session to a desktop UIA winrpa_element handle for interacting with OS SSO prompts or print dialogs.",
    returns: "A \"winrpa_element\" handle for the browser window.",
    tags: ["Hybrid Bridge", "UIA Window"],
    example: 'sess <- rpa_web_launch(headless = FALSE)\nwin <- rpa_web_window(sess) |> rpa_window_focus()'
  },
  {
    name: "format.winrpa_web_session",
    category: "Web Browser CDP (Edge/Chrome)",
    signature: "format.winrpa_web_session(x, ...)",
    summary: "Formats a winrpa_web_session handle with browser type, debugging port, PID, and headless flag.",
    returns: "Character scalar summary.",
    tags: ["S3 Method", "Cross-Platform"],
    example: 'format(sess)'
  },
  {
    name: "print.winrpa_web_session",
    category: "Web Browser CDP (Edge/Chrome)",
    signature: "print.winrpa_web_session(x, ...)",
    summary: "Prints a formatted winrpa_web_session summary.",
    returns: "Invisibly returns x.",
    tags: ["S3 Method", "Cross-Platform"],
    example: 'print(sess)'
  },
  {
    name: "format.winrpa_web_element",
    category: "Web Browser CDP (Edge/Chrome)",
    signature: "format.winrpa_web_element(x, ...)",
    summary: "Formats a winrpa_web_element handle showing selector strategy (css/xpath), selector string, and match index.",
    returns: "Character scalar summary.",
    tags: ["S3 Method", "Cross-Platform"],
    example: 'format(btn)'
  },
  {
    name: "print.winrpa_web_element",
    category: "Web Browser CDP (Edge/Chrome)",
    signature: "print.winrpa_web_element(x, ...)",
    summary: "Prints a formatted winrpa_web_element summary.",
    returns: "Invisibly returns x.",
    tags: ["S3 Method", "Cross-Platform"],
    example: 'print(btn)'
  },

  /* ===================== Script Recorder (Desktop UIA & Web CDP) (10) ===================== */
  {
    name: "rpa_record_start",
    category: "Script Recorder",
    signature: "rpa_record_start(ignore_self = TRUE, redact_passwords = TRUE, include_wheel = TRUE, stop_hotkey = c(\"ctrl\", \"shift\", \"f9\"), ignore_pids = integer())",
    summary: "Starts the two-stage desktop recorder: lock-free WH_MOUSE_LL + WH_KEYBOARD_LL hooks (ignoring WM_MOUSEMOVE) paired with an async MTA UIA ElementFromPoint resolver thread and global stop hotkey.",
    returns: "Invisibly returns TRUE.",
    tags: ["WH_MOUSE_LL", "MTA Resolver", "Password Safe"],
    example: 'rpa_record_start(ignore_self = TRUE, redact_passwords = TRUE)\n# Perform desktop clicks/typing, then press Ctrl+Shift+F9 or call rpa_record_stop()\nrec <- rpa_record_stop()'
  },
  {
    name: "rpa_record_status",
    category: "Script Recorder",
    signature: "rpa_record_status()",
    summary: "Reports live desktop recorder state including active status, stop-hotkey trigger state, elapsed seconds, resolved event count, and ring-buffer drop count.",
    returns: "Named list: active, stopped_by_hotkey, elapsed_sec, captured_events, dropped_events.",
    tags: ["Diagnostics", "Non-Blocking"],
    example: 'rpa_record_status()'
  },
  {
    name: "rpa_record_stop",
    category: "Script Recorder",
    signature: "rpa_record_stop(file = NULL, coalesce_typing = TRUE, wait_threshold_ms = 800, redact_passwords = NULL, include_wheel = NULL, ignore_self = NULL)",
    summary: "Unhooks WH_MOUSE_LL / WH_KEYBOARD_LL, flushes in-flight MTA UIA lookups, normalizes clicks/double-clicks/wheels/typing/passwords, emits a replayable R script, and optionally writes it to file.",
    returns: "An object of class \"winrpa_recording\" with $script character vector and $events data.frame.",
    tags: ["Code Generation", "Auto-Waits", "Password Redaction"],
    example: 'rpa_record_start()\n# ... user actions ...\nrec <- rpa_record_stop(file = "recorded_flow.R")\nprint(rec)'
  },
  {
    name: "rpa_record_emit",
    category: "Script Recorder",
    signature: "rpa_record_emit(recording, coalesce_typing = TRUE, wait_threshold_ms = 800, redact_passwords = TRUE, include_wheel = TRUE, ignore_self = TRUE, ignore_pids = integer(), double_click_ms = NULL, drag_px = NULL)",
    summary: "Pure R normalization and script-generation pass over captured desktop events: filters self/parent PIDs, folds in-burst Backspace typos, ranks selectors (automation_id > name+exact > class_name > relative click), and emits rpa_wait_*() calls.",
    returns: "An object of class \"winrpa_recording\".",
    tags: ["Pure Codegen", "Selector Ranking", "Cross-Platform"],
    example: 'rec2 <- rpa_record_emit(rec, wait_threshold_ms = 500, coalesce_typing = TRUE)'
  },
  {
    name: "rpa_record_save",
    category: "Script Recorder",
    signature: "rpa_record_save(recording, file)",
    summary: "Writes the emitted R script lines from a desktop or web winrpa_recording object to disk cleanly.",
    returns: "Invisibly returns the normalized output file path.",
    tags: ["File Output", "Cross-Platform"],
    example: 'rpa_record_save(rec, tempfile(fileext = ".R"))'
  },
  {
    name: "rpa_web_record_start",
    category: "Script Recorder",
    signature: "rpa_web_record_start(session, redact_passwords = TRUE)",
    summary: "Attaches a CDP Runtime.addBinding + Page.addScriptToEvaluateOnNewDocument DOM event recorder capturing clicks, double-clicks, form typing, <select> changes, file inputs, and full-page navigations with ranked CSS selectors.",
    returns: "Invisibly returns TRUE.",
    tags: ["CDP Binding", "CSS Ranking", "Password Redaction"],
    example: 'sess <- rpa_web_launch(headless = FALSE)\nrpa_web_record_start(sess, redact_passwords = TRUE)'
  },
  {
    name: "rpa_web_record_stop",
    category: "Script Recorder",
    signature: "rpa_web_record_stop(session, file = NULL, coalesce_typing = TRUE, wait_threshold_ms = 800, redact_passwords = NULL)",
    summary: "Detaches the DOM recorder listener, drains queued CDP events from C++, normalizes inputs/clicks, and emits a runnable rpa_web_*() R script.",
    returns: "An object of class \"winrpa_recording\" (mode = \"web\").",
    tags: ["CDP Recorder", "rpa_web_* Codegen"],
    example: 'rec <- rpa_web_record_stop(sess, file = "web_flow.R")\nprint(rec)'
  },
  {
    name: "rpa_web_record_emit",
    category: "Script Recorder",
    signature: "rpa_web_record_emit(recording, coalesce_typing = TRUE, wait_threshold_ms = 800, redact_passwords = TRUE)",
    summary: "Regenerates a clean rpa_web_*() R script from a captured web recording data.frame or winrpa_recording object.",
    returns: "An object of class \"winrpa_recording\".",
    tags: ["Pure Codegen", "Cross-Platform"],
    example: 'rec2 <- rpa_web_record_emit(rec, redact_passwords = TRUE)'
  },
  {
    name: "format.winrpa_recording",
    category: "Script Recorder",
    signature: "format.winrpa_recording(x, ...)",
    summary: "Formats a compact summary string for a desktop or web winrpa_recording object.",
    returns: "Character scalar summary.",
    tags: ["S3 Method", "Cross-Platform"],
    example: 'format(rec)'
  },
  {
    name: "print.winrpa_recording",
    category: "Script Recorder",
    signature: "print.winrpa_recording(x, ...)",
    summary: "Prints a winrpa_recording summary header followed by the complete emitted R script.",
    returns: "Invisibly returns x.",
    tags: ["S3 Method", "Cross-Platform"],
    example: 'print(rec)'
  }
];
