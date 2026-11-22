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
