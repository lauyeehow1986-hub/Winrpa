test_that("recorder lock-free ring buffer and VK helpers work headlessly", {
  res1 <- winrpa:::cpp_record_ring_selftest(16L)
  expect_equal(res1$capacity, 512L)
  expect_equal(res1$pushed, 16L)
  expect_equal(res1$pre_drain_dropped, 0L)
  expect_equal(res1$batch_dropped, 0L)
  expect_equal(res1$total_dropped, 0L)
  expect_equal(res1$count, 16L)
  expect_equal(res1$xs, seq_len(16L) * 2L)

  # Overflow capacity (512): pushing 600 items should drop 88 and keep newest 512
  res2 <- winrpa:::cpp_record_ring_selftest(600L)
  expect_equal(res2$pushed, 600L)
  expect_equal(res2$pre_drain_dropped, 88L)
  expect_equal(res2$batch_dropped, 88L)
  expect_equal(res2$total_dropped, 88L)
  expect_equal(res2$count, 512L)
  expect_equal(res2$xs[1L], 89L * 2L)
  expect_equal(res2$xs[512L], 600L * 2L)

  vk <- winrpa:::cpp_record_vk_selftest()
  expect_true(isTRUE(vk$mod_ctrl))
  expect_true(isTRUE(vk$mod_lshift))
  expect_false(isTRUE(vk$mod_a))
  expect_equal(vk$enter, "enter")
  expect_equal(vk$tab, "tab")
  expect_equal(vk$esc, "esc")
  expect_equal(vk$backspace, "backspace")
  expect_equal(vk$f1, "f1")
  expect_equal(vk$f12, "f12")
  expect_equal(vk$key_s, "s")
  expect_equal(vk$key_7, "7")
})

test_that("rpa_wait_window and rpa_wait_find validate inputs cleanly", {
  expect_error(rpa_wait_window(pid = -1L), "positive integer")
  expect_error(rpa_wait_window(regex = NA), "logical")
  expect_error(rpa_wait_find(max_depth = -1L), "non-negative")
  expect_error(rpa_wait_find(parent = "not_an_element", name = "OK"), "winrpa_element")
  expect_error(rpa_record_start(stop_hotkey = c("ctrl")), "at least one modifier")
  expect_error(rpa_record_start(stop_hotkey = c("bogusmod", "f9")), "Unknown stop_hotkey modifier")
  expect_error(rpa_record_save(list(), tempfile(fileext = ".R")), "winrpa_recording")
})

make_mock_desktop_events <- function(...) {
  dots <- list(...)
  n <- max(vapply(dots, length, integer(1L)))
  col <- function(nm, default) {
    v <- dots[[nm]]
    if (is.null(v)) rep(default, n) else if (length(v) == 1L && n > 1L) rep(v, n) else v
  }
  data.frame(
    kind = col("kind", "mouse_down"),
    timestamp_ms = col("timestamp_ms", seq_len(n) * 50),
    x = as.integer(col("x", 100L)),
    y = as.integer(col("y", 200L)),
    rel_x = as.integer(col("rel_x", 40L)),
    rel_y = as.integer(col("rel_y", 80L)),
    wheel_delta = as.integer(col("wheel_delta", 0L)),
    horizontal = as.logical(col("horizontal", FALSE)),
    button = col("button", ""),
    vk_code = as.integer(col("vk_code", 0L)),
    scan_code = as.integer(col("scan_code", 0L)),
    modifiers = as.integer(col("modifiers", 0L)),
    key_name = col("key_name", ""),
    unicode_text = col("unicode_text", ""),
    win_title = col("win_title", "Untitled - Notepad"),
    win_class = col("win_class", "Notepad"),
    win_hwnd = col("win_hwnd", "0x1000"),
    win_pid = as.integer(col("win_pid", 99999L)),
    win_left = as.integer(col("win_left", 60L)),
    win_top = as.integer(col("win_top", 120L)),
    win_right = as.integer(col("win_right", 860L)),
    win_bottom = as.integer(col("win_bottom", 720L)),
    el_name = col("el_name", ""),
    el_automation_id = col("el_automation_id", ""),
    el_control_type = col("el_control_type", ""),
    el_class_name = col("el_class_name", ""),
    el_hwnd = col("el_hwnd", ""),
    el_pid = as.integer(col("el_pid", 99999L)),
    el_left = as.integer(col("el_left", 90L)),
    el_top = as.integer(col("el_top", 190L)),
    el_right = as.integer(col("el_right", 190L)),
    el_bottom = as.integer(col("el_bottom", 230L)),
    is_password = as.logical(col("is_password", FALSE)),
    has_toggle = as.logical(col("has_toggle", FALSE)),
    has_select = as.logical(col("has_select", FALSE)),
    has_value = as.logical(col("has_value", FALSE)),
    stringsAsFactors = FALSE
  )
}

test_that("rpa_record_emit pairs clicks, coalesces typing + Backspace, and emits valid R", {
  df <- make_mock_desktop_events(
    kind = c(
      "mouse_down", "mouse_up",
      "key_down", "key_down", "key_down", "key_down", "key_down", "key_down",
      "key_down"
    ),
    timestamp_ms = c(100, 140, 200, 230, 260, 290, 320, 350, 420),
    button = c("left", "left", "", "", "", "", "", "", ""),
    vk_code = c(0L, 0L, 65L, 66L, 88L, 8L, 67L, 13L, 83L),
    modifiers = c(0L, 0L, 0L, 0L, 0L, 0L, 0L, 0L, 1L),
    key_name = c("", "", "a", "b", "x", "backspace", "c", "enter", "s"),
    unicode_text = c("", "", "a", "b", "x", "", "c", "", ""),
    win_title = "*Untitled - Notepad",
    el_automation_id = "RichEditD2DPT",
    el_control_type = "Document"
  )

  rec <- rpa_record_emit(df, ignore_self = FALSE)
  expect_s3_class(rec, "winrpa_recording")
  txt <- paste(rec$script, collapse = "\n")

  expect_no_error(parse(text = rec$script))
  expect_match(txt, 'win <- rpa_wait_window\\(title = "Untitled - Notepad"\\)', perl = TRUE)
  expect_match(txt, "rpa_window_focus\\(win\\)", perl = TRUE)
  expect_match(txt, 'el_1 <- rpa_wait_find\\(win, automation_id = "RichEditD2DPT", control_type = "Document"\\)', perl = TRUE)
  expect_match(txt, "rpa_click\\(el_1\\)", perl = TRUE)
  expect_match(txt, 'rpa_type\\(el_1, "abc"\\)', perl = TRUE)
  expect_match(txt, 'rpa_key_chord\\("enter"\\)', perl = TRUE)
  expect_match(txt, 'rpa_key_chord\\(c\\("ctrl", "s"\\)\\)', perl = TRUE)

  # Print, format, and save round-trip
  expect_match(format(rec), "winrpa_recording: mode=desktop")
  out_file <- tempfile(fileext = ".R")
  on.exit(unlink(out_file), add = TRUE)
  saved <- rpa_record_save(rec, out_file)
  expect_true(file.exists(saved))
  expect_equal(readLines(saved), rec$script)
})

test_that("rpa_record_emit handles double-clicks, drags, semantic controls, wheel, and password redaction", {
  df <- make_mock_desktop_events(
    kind = c(
      "mouse_down", "mouse_up", "mouse_down", "mouse_up",
      "mouse_down", "mouse_up",
      "mouse_down", "mouse_up",
      "mouse_down", "mouse_up",
      "key_down", "key_down", "key_down", "key_down",
      "mouse_wheel", "mouse_wheel"
    ),
    timestamp_ms = c(
      100, 120, 210, 230,
      400, 430,
      600, 620,
      700, 1700,
      1800, 1840, 1880, 1920,
      2100, 2200
    ),
    x = c(50, 50, 51, 51, 120, 120, 200, 320, 300, 300, 300, 300, 300, 300, 400, 400),
    y = c(50, 50, 51, 51, 120, 120, 200, 200, 300, 300, 300, 300, 300, 300, 400, 400),
    rel_x = c(10, 10, 11, 11, 60, 60, 140, 260, 240, 240, 240, 240, 240, 240, 340, 340),
    rel_y = c(10, 10, 11, 11, 20, 20, 80, 80, 180, 180, 180, 180, 180, 180, 280, 280),
    wheel_delta = c(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -120, -120),
    button = c(
      "left", "left", "left", "left",
      "left", "left",
      "left", "left",
      "left", "left",
      "", "", "", "",
      "", ""
    ),
    vk_code = c(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 83, 69, 67, 13, 0, 0),
    key_name = c("", "", "", "", "", "", "", "", "", "", "s", "e", "c", "enter", "", ""),
    unicode_text = c("", "", "", "", "", "", "", "", "", "", "s", "e", "c", "", "", ""),
    el_name = c(
      "Open", "Open", "Open", "Open",
      "Remember me", "Remember me",
      "", "",
      "Password", "Password",
      "Password", "Password", "Password", "Password",
      "", ""
    ),
    el_automation_id = c(
      "12345678-1234-1234-1234-1234567890ab", "12345678-1234-1234-1234-1234567890ab",
      "12345678-1234-1234-1234-1234567890ab", "12345678-1234-1234-1234-1234567890ab",
      "", "",
      "", "",
      "", "",
      "", "", "", "",
      "", ""
    ),
    el_control_type = c(
      "Button", "Button", "Button", "Button",
      "CheckBox", "CheckBox",
      "", "",
      "Edit", "Edit",
      "Edit", "Edit", "Edit", "Edit",
      "", ""
    ),
    has_toggle = c(rep(FALSE, 4), TRUE, TRUE, rep(FALSE, 10)),
    is_password = c(rep(FALSE, 8), TRUE, TRUE, TRUE, TRUE, TRUE, TRUE, FALSE, FALSE)
  )

  rec <- rpa_record_emit(
    df,
    ignore_self = FALSE,
    wait_threshold_ms = 500,
    double_click_ms = 500,
    drag_px = 4L
  )
  txt <- paste(rec$script, collapse = "\n")
  expect_no_error(parse(text = rec$script))

  # GUID automation_id rejected -> ranked name + control_type + exact = TRUE
  expect_false(grepl("12345678-1234", txt, fixed = TRUE))
  expect_match(txt, 'name = "Open", control_type = "Button", exact = TRUE', perl = TRUE)
  expect_match(txt, "rpa_click\\(el_1, clicks = 2L\\)", perl = TRUE)

  # Semantic toggle on CheckBox
  expect_match(txt, "rpa_toggle\\(el_2\\)", perl = TRUE)

  # Drag emitted rather than click
  expect_match(txt, "# Drag left from", perl = TRUE)

  # Pause > 500ms uses rpa_wait_find on Password edit, and password text is redacted
  expect_match(txt, 'rpa_wait_find\\(win, name = "Password", control_type = "Edit", exact = TRUE\\)', perl = TRUE)
  expect_match(txt, 'rpa_type\\(el_3, Sys\\.getenv\\("WINRPA_SECRET"\\), secret = TRUE\\)', perl = TRUE)
  expect_false(grepl('"sec"', txt, fixed = TRUE))
  expect_match(txt, 'rpa_key_chord\\("enter"\\)', perl = TRUE)

  # Coalesced wheel ticks moved cursor first then scrolled -240L
  expect_match(txt, "rpa_mouse_move\\(win_info\\$left \\+ 340L, win_info\\$top \\+ 280L\\)", perl = TRUE)
  expect_match(txt, "rpa_mouse_scroll\\(delta = -240L\\)", perl = TRUE)
})

test_that("rpa_record_emit filters self PID and extra ignore_pids and falls back to window-relative clicks", {
  metrics <- winrpa:::cpp_record_system_metrics()
  self_pid <- if (metrics$self_pid > 0L) metrics$self_pid else 4242L
  df <- make_mock_desktop_events(
    kind = c("mouse_down", "mouse_up", "mouse_down", "mouse_up"),
    timestamp_ms = c(100, 120, 300, 320),
    button = c("left", "left", "right", "right"),
    rel_x = c(10L, 10L, 55L, 55L),
    rel_y = c(10L, 10L, 95L, 95L),
    win_pid = c(self_pid, self_pid, 88888L, 88888L),
    el_pid = c(self_pid, self_pid, 88888L, 88888L),
    win_hwnd = c("0x99", "0x99", "0x200", "0x200"),
    win_title = c("RStudio", "RStudio", "TargetApp", "TargetApp"),
    el_name = c("Stop", "Stop", "", ""),
    el_control_type = c("Button", "Button", "", "")
  )

  rec <- rpa_record_emit(df, ignore_self = TRUE, ignore_pids = self_pid)
  txt <- paste(rec$script, collapse = "\n")
  expect_no_error(parse(text = rec$script))
  expect_false(grepl("RStudio", txt, fixed = TRUE))
  expect_match(txt, 'win <- rpa_wait_window\\(title = "TargetApp"\\)', perl = TRUE)
  expect_match(txt, 'rpa_mouse_click\\(x = win_info\\$left \\+ 55L, y = win_info\\$top \\+ 95L, button = "right"\\)', perl = TRUE)
})

test_that("rpa_record_emit emits standalone Win key and upgrades Start -> SearchTextBox flows cleanly", {
  metrics <- winrpa:::cpp_record_system_metrics()
  self_pid <- if (metrics$self_pid > 0L) metrics$self_pid else 4242L

  # 1. Standalone Win key tap + SearchTextBox typing spanning Start -> Search handoff
  df_win <- make_mock_desktop_events(
    kind = c("key_tap", "key_down", "key_down", "key_down", "key_down", "key_down", "key_down", "key_down", "key_down"),
    timestamp_ms = c(100, 300, 340, 380, 420, 460, 500, 540, 620),
    vk_code = c(91L, 78L, 79L, 84L, 69L, 80L, 65L, 68L, 13L),
    key_name = c("win", "n", "o", "t", "e", "p", "a", "d", "enter"),
    unicode_text = c("", "n", "o", "t", "e", "p", "a", "d", ""),
    win_title = c("", "Start", "Search", "Search", "Search", "Search", "Search", "Search", "Search"),
    win_class = c(
      "", "Windows.UI.Core.CoreWindow", "Windows.UI.Core.CoreWindow",
      "Windows.UI.Core.CoreWindow", "Windows.UI.Core.CoreWindow", "Windows.UI.Core.CoreWindow",
      "Windows.UI.Core.CoreWindow", "Windows.UI.Core.CoreWindow", "Windows.UI.Core.CoreWindow"
    ),
    win_hwnd = c("", "0x111", "0x222", "0x222", "0x222", "0x222", "0x222", "0x222", "0x222"),
    win_pid = c(0L, rep(55555L, 8L)),
    el_automation_id = c("", rep("SearchTextBox", 8L)),
    el_control_type = c("", rep("Edit", 8L)),
    el_pid = c(0L, rep(55555L, 8L))
  )

  rec <- rpa_record_emit(df_win, ignore_self = TRUE, ignore_pids = self_pid)
  expect_no_error(parse(text = rec$script))
  txt <- paste(rec$script, collapse = "\n")

  expect_match(txt, 'rpa_key_chord\\(c\\("win", "s"\\)\\)', perl = TRUE)
  expect_match(txt, "Sys\\.sleep\\(0\\.4\\)", perl = TRUE)
  expect_match(
    txt,
    'win <- rpa_wait_window\\(title = "\\^Search\\$", class_name = "Windows\\.UI\\.Core\\.CoreWindow", regex = TRUE\\)',
    perl = TRUE
  )
  expect_false(grepl("rpa_window_focus(win)", txt, fixed = TRUE))
  expect_match(txt, 'rpa_wait_find\\(win, automation_id = "SearchTextBox", control_type = "Edit"\\)', perl = TRUE)
  expect_match(txt, 'rpa_type\\(el_1, "notepad"\\)', perl = TRUE)
  expect_match(txt, 'rpa_key_chord\\("enter"\\)', perl = TRUE)

  # 2. Standalone Win key tap without SearchTextBox keeps rpa_key_chord("win")
  df_tap_only <- make_mock_desktop_events(
    kind = "key_tap",
    timestamp_ms = 100,
    vk_code = 91L,
    key_name = "win",
    win_title = "",
    win_class = "",
    win_hwnd = "",
    win_pid = 0L,
    el_pid = 0L
  )
  rec_tap <- rpa_record_emit(df_tap_only, ignore_self = TRUE)
  expect_no_error(parse(text = rec_tap$script))
  txt_tap <- paste(rec_tap$script, collapse = "\n")
  expect_match(txt_tap, 'rpa_key_chord\\("win"\\)', perl = TRUE)
  expect_match(txt_tap, "Sys\\.sleep\\(0\\.4\\)", perl = TRUE)

  # 3. Taskbar Start button click followed by SearchTextBox upgrades cleanly to Win+S
  df_start_click <- make_mock_desktop_events(
    kind = c("mouse_down", "mouse_up", "key_down", "key_down", "key_down"),
    timestamp_ms = c(100, 140, 350, 390, 430),
    button = c("left", "left", "", "", ""),
    vk_code = c(0L, 0L, 67L, 77L, 68L),
    key_name = c("", "", "c", "m", "d"),
    unicode_text = c("", "", "c", "m", "d"),
    win_title = c("", "", "Search", "Search", "Search"),
    win_class = c("Shell_TrayWnd", "Shell_TrayWnd", "Windows.UI.Core.CoreWindow", "Windows.UI.Core.CoreWindow", "Windows.UI.Core.CoreWindow"),
    win_hwnd = c("0x10", "0x10", "0x20", "0x20", "0x20"),
    el_name = c("Start", "Start", "", "", ""),
    el_automation_id = c("StartButton", "StartButton", "SearchTextBox", "SearchTextBox", "SearchTextBox"),
    el_control_type = c("Button", "Button", "Edit", "Edit", "Edit")
  )
  rec_click <- rpa_record_emit(df_start_click, ignore_self = FALSE)
  expect_no_error(parse(text = rec_click$script))
  txt_click <- paste(rec_click$script, collapse = "\n")
  expect_match(txt_click, 'rpa_key_chord\\(c\\("win", "s"\\)\\)', perl = TRUE)
  expect_false(grepl("Shell_TrayWnd", txt_click, fixed = TRUE))
  expect_match(txt_click, 'rpa_type\\(el_1, "cmd"\\)', perl = TRUE)

  # 4. Win+R modifier chord emits c("win", "r") cleanly
  df_winr <- make_mock_desktop_events(
    kind = "key_down",
    timestamp_ms = 100,
    vk_code = 82L,
    modifiers = 8L,
    key_name = "r",
    win_title = "Program Manager",
    win_class = "Progman"
  )
  rec_winr <- rpa_record_emit(df_winr, ignore_self = FALSE)
  expect_no_error(parse(text = rec_winr$script))
  expect_match(paste(rec_winr$script, collapse = "\n"), 'rpa_key_chord\\(c\\("win", "r"\\)\\)', perl = TRUE)
})
