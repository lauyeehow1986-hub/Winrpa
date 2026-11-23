test_that("rpa_selector validates inputs and formats cleanly", {
  sel <- rpa_selector(
    name = "Save",
    automation_id = "btnSave",
    control_type = "Button",
    class_name = "Button",
    regex = TRUE
  )
  expect_s3_class(sel, "winrpa_selector")
  fmt <- format(sel)
  expect_type(fmt, "character")
  expect_match(fmt, "Save")
  expect_match(fmt, "btnSave")
  expect_match(fmt, "Button")

  printed <- NULL
  capture.output({
    printed <- print(sel)
  })
  expect_identical(printed, sel)

  expect_error(rpa_selector(regex = "yes"), "logical")
  expect_error(rpa_selector(name = 123), "character")
})

test_that("rpa_wait_for handles immediate, delayed, timeout, abort, and debug capture", {
  on.exit(rpa_reset_abort(), add = TRUE)
  rpa_reset_abort()

  expect_equal(
    rpa_wait_for(function() "ready", timeout = 0.5, interval = 0.01, label = "immediate"),
    "ready"
  )

  state <- new.env(parent = emptyenv())
  state$attempts <- 0L
  delayed <- rpa_wait_for(
    function() {
      state$attempts <- state$attempts + 1L
      if (state$attempts >= 3L) 42L else NULL
    },
    timeout = 1.0,
    interval = 0.01,
    label = "delayed counter"
  )
  expect_equal(delayed, 42L)
  expect_equal(state$attempts, 3L)

  debug_dir <- tempfile("winrpa_debug_")
  dir.create(debug_dir, recursive = TRUE)
  on.exit(unlink(debug_dir, recursive = TRUE), add = TRUE)
  old_opt <- options(winrpa.debug_dir = debug_dir)
  on.exit(options(old_opt), add = TRUE)

  expect_error(
    rpa_wait_for(function() FALSE, timeout = 0.05, interval = 0.01, label = "missing button"),
    "missing button"
  )
  artifacts <- list.files(debug_dir, full.names = TRUE)
  expect_true(length(artifacts) >= 1L)
  expect_true(any(grepl("\\.log$", artifacts)))

  winrpa:::cpp_trigger_abort()
  expect_error(
    rpa_wait_for(function() NULL, timeout = 1.0, interval = 0.01, label = "aborted wait"),
    "abort"
  )
})

test_that("Input and secret validation errors never leak secret strings", {
  secret_val <- "SuperSecretPassw0rd!99"
  err <- tryCatch(
    rpa_type("not-an-element", secret_val, secret = TRUE),
    error = function(e) conditionMessage(e)
  )
  expect_false(grepl(secret_val, err, fixed = TRUE))

  err2 <- tryCatch(
    rpa_set_value("not-an-element", secret_val, secret = TRUE),
    error = function(e) conditionMessage(e)
  )
  expect_false(grepl(secret_val, err2, fixed = TRUE))

  expect_error(rpa_mouse_click(button = "invalid_button"))
  expect_error(rpa_mouse_click(clicks = 0L), "clicks")
  expect_error(rpa_key_chord(character(0)), "non-empty")
  expect_error(rpa_key_chord(c("ctrl", "")), "non-empty")
  expect_error(rpa_click("not-an-element"), "winrpa_element")
})

test_that("rpa_launch defaults to visible launch and validates arguments", {
  fmls <- formals(rpa_launch)
  expect_identical(fmls$wait, FALSE)
  expect_identical(fmls$minimized, FALSE)
  expect_identical(fmls$invisible, FALSE)

  expect_error(rpa_launch(""), "command")
  expect_error(rpa_launch(NA_character_), "command")
  expect_error(rpa_launch(123), "command")
  expect_error(rpa_launch("notepad.exe", args = c("a", NA_character_)), "args")
  expect_error(rpa_launch("notepad.exe", wait = "no"), "wait")
  expect_error(rpa_launch("notepad.exe", minimized = NA), "minimized")
  expect_error(rpa_launch("notepad.exe", invisible = 1L), "invisible")
  expect_error(rpa_launch("notepad.exe", env = 123), "env")
})

test_that("rpa_type defaults to 5ms pacing, supports paste mode, and blocks paste with secret", {
  fmls <- formals(rpa_type)
  expect_identical(fmls$delay_ms, 5L)
  expect_true("paste" %in% eval(fmls$mode))

  fake_elem <- structure(list(ptr = NULL), class = "winrpa_element")
  err <- tryCatch(
    rpa_type(fake_elem, "TopSecret!42", mode = "paste", secret = TRUE),
    error = function(e) conditionMessage(e)
  )
  expect_match(err, "paste")
  expect_match(err, "secret")
  expect_false(grepl("TopSecret!42", err, fixed = TRUE))
})

test_that("rpa_click() scrolls element into view before reading bounding rect and clicking", {
  state <- new.env(parent = emptyenv())
  state$scrolled <- FALSE
  state$clicked <- NULL
  state$info_calls <- 0L
  fake_elem <- structure(list(ptr = NULL), class = "winrpa_element")

  testthat::local_mocked_bindings(
    rpa_session_check = function(...) invisible(TRUE),
    cpp_uia_scroll_into_view = function(ptr) {
      state$scrolled <- TRUE
      invisible(NULL)
    },
    cpp_uia_element_info = function(ptr) {
      state$info_calls <- state$info_calls + 1L
      if (!state$scrolled) {
        list(left = 100L, top = 1500L, right = 200L, bottom = 1540L, patterns = "ScrollItem")
      } else {
        list(left = 100L, top = 300L, right = 200L, bottom = 340L, patterns = "ScrollItem")
      }
    },
    cpp_mouse_click = function(x, y, button, clicks, move_first) {
      state$clicked <- list(x = x, y = y, button = button, clicks = clicks, move_first = move_first)
      invisible(NULL)
    }
  )

  res <- rpa_click(fake_elem, method = "mouse")
  expect_identical(res, fake_elem)
  expect_true(state$scrolled)
  expect_true(state$info_calls >= 2L)
  expect_equal(state$clicked$x, 150L)
  expect_equal(state$clicked$y, 320L)

  # Subtest B: pre-scroll empty bounding rect becomes valid after ScrollIntoView
  state$scrolled <- FALSE
  state$clicked <- NULL
  testthat::local_mocked_bindings(
    cpp_uia_element_info = function(ptr) {
      if (!state$scrolled) {
        list(left = 0L, top = 0L, right = 0L, bottom = 0L, patterns = "ScrollItem")
      } else {
        list(left = 50L, top = 100L, right = 150L, bottom = 140L, patterns = "ScrollItem")
      }
    }
  )

  rpa_click(fake_elem, method = "mouse")
  expect_equal(state$clicked$x, 100L)
  expect_equal(state$clicked$y, 120L)
})

test_that("rpa_window_* lifecycle helpers validate inputs and dispatch actions", {
  for (fn_name in c(
    "rpa_window_focus",
    "rpa_window_maximize",
    "rpa_window_minimize",
    "rpa_window_restore",
    "rpa_window_close"
  )) {
    expect_true(exists(fn_name, mode = "function"), info = fn_name)
    fn <- get(fn_name, mode = "function")
    expect_error(fn("not-an-element"), "winrpa_element")
  }

  calls <- new.env(parent = emptyenv())
  calls$actions <- character()
  calls$focused <- 0L
  fake_win <- structure(list(ptr = NULL), class = "winrpa_element")

  testthat::local_mocked_bindings(
    rpa_session_check = function(...) invisible(TRUE),
    cpp_uia_window_focus = function(ptr) {
      calls$focused <- calls$focused + 1L
      invisible(NULL)
    },
    cpp_uia_window_state = function(ptr, action) {
      calls$actions <- c(calls$actions, action)
      invisible(NULL)
    }
  )

  expect_identical(rpa_window_focus(fake_win), fake_win)
  expect_identical(rpa_window_maximize(fake_win), fake_win)
  expect_identical(rpa_window_minimize(fake_win), fake_win)
  expect_identical(rpa_window_restore(fake_win), fake_win)
  expect_null(rpa_window_close(fake_win))

  expect_equal(calls$focused, 1L)
  expect_equal(calls$actions, c("maximize", "minimize", "restore", "close"))
})

test_that("rpa_selector(), rpa_find(), and rpa_find_all() support exact matching and max_depth validation", {
  sel <- rpa_selector(name = "OK", exact = TRUE)
  expect_true(isTRUE(sel$exact))
  expect_match(format(sel), "exact=TRUE")
  expect_error(rpa_selector(exact = "yes"), "logical")

  expect_true("exact" %in% names(formals(rpa_find)))
  expect_true("max_depth" %in% names(formals(rpa_find)))
  expect_true("exact" %in% names(formals(rpa_find_all)))
  expect_true("max_depth" %in% names(formals(rpa_find_all)))
  expect_identical(formals(rpa_find)$exact, FALSE)
  expect_null(formals(rpa_find)$max_depth)

  expect_error(rpa_find_all(name = "OK", max_depth = -1L), "max_depth")
  expect_error(rpa_find_all(name = "OK", max_depth = "deep"), "max_depth")
  expect_error(rpa_find_all(name = "OK", max_depth = NA_real_), "max_depth")
  expect_error(rpa_find(name = "OK", exact = "yes"), "logical")
  expect_error(rpa_find(name = "OK", max_depth = -2L), "max_depth")
})
