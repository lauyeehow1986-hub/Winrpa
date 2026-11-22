test_that("Live Windows desktop inspection, DPI, capture, and killswitch work interactively", {
  skip_if_not(interactive() && .Platform$OS.type == "windows" && rpa_is_interactive_desktop())

  metrics <- rpa_display_metrics()
  expect_type(metrics, "list")
  expect_true(metrics$virtual_desktop$virtual_w > 0L)
  expect_true(nrow(metrics$monitors) >= 1L)

  wins <- rpa_windows()
  expect_s3_class(wins, "data.frame")
  expect_true(all(c("name", "pid", "hwnd") %in% names(wins)))

  pos <- rpa_mouse_position()
  expect_length(pos, 2L)
  expect_named(pos, c("x", "y"))

  dpi_status <- rpa_try_enable_dpi_awareness()
  expect_type(dpi_status, "list")
  expect_true("awareness" %in% names(dpi_status))

  bmp_path <- tempfile(fileext = ".bmp")
  on.exit(unlink(bmp_path), add = TRUE)
  first_hwnd <- if (nrow(wins) > 0L) wins$hwnd[nzchar(wins$hwnd)][1L] else NA_character_
  out <- rpa_screenshot(if (is.na(first_hwnd)) NULL else first_hwnd, path = bmp_path)
  expect_true(file.exists(out))
  expect_gt(file.info(out)$size, 1024)

  on.exit(try(rpa_disable_killswitch(), silent = TRUE), add = TRUE)
  expect_true(rpa_enable_killswitch())
  expect_true(rpa_disable_killswitch())
})
