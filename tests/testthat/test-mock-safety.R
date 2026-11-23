test_that("Virtual desktop normalization handles negative and multi-monitor coordinates", {
  # Dual 1920x1080 monitors: secondary LEFT of primary (vx = -1920, vw = 3840)
  left_edge <- rpa_normalize_virtual_coords(
    -1920L, 0L, vx = -1920L, vy = 0L, vw = 3840L, vh = 1080L
  )
  expect_equal(unname(left_edge), c(0L, 0L))
  expect_named(left_edge, c("norm_x", "norm_y"))

  seam <- rpa_normalize_virtual_coords(
    0L, 540L, vx = -1920L, vy = 0L, vw = 3840L, vh = 1080L
  )
  expect_true(seam[["norm_x"]] >= 32760L && seam[["norm_x"]] <= 32785L)

  right_edge <- rpa_normalize_virtual_coords(
    1919L, 1079L, vx = -1920L, vy = 0L, vw = 3840L, vh = 1080L
  )
  expect_equal(unname(right_edge), c(65535L, 65535L))

  # Secondary monitor ABOVE primary (vy = -1080, vh = 2160)
  top_center <- rpa_normalize_virtual_coords(
    960L, -540L, vx = 0L, vy = -1080L, vw = 1920L, vh = 2160L
  )
  expect_true(top_center[["norm_y"]] > 16000L && top_center[["norm_y"]] < 17000L)

  # Out-of-bounds clamping and degenerate bounds
  clamped <- rpa_normalize_virtual_coords(
    -5000L, 9999L, vx = 0L, vy = 0L, vw = 1920L, vh = 1080L
  )
  expect_equal(unname(clamped), c(0L, 65535L))

  degenerate <- rpa_normalize_virtual_coords(100L, 100L, vx = 0L, vy = 0L, vw = 1L, vh = 1L)
  expect_equal(unname(degenerate), c(0L, 0L))
})

test_that("Abort flag starts clean, triggers, and resets deterministically", {
  on.exit(rpa_reset_abort(), add = TRUE)
  rpa_reset_abort()
  expect_false(rpa_abort_triggered())

  winrpa:::cpp_trigger_abort()
  expect_true(rpa_abort_triggered())

  rpa_reset_abort()
  expect_false(rpa_abort_triggered())
})

test_that("RAII ModifierGuard releases pressed keys in LIFO order on normal and error unwind", {
  normal_res <- winrpa:::cpp_test_modifier_guard_lifecycle(c(17L, 18L, 16L), simulate_error = FALSE)
  expect_false(normal_res$unwound)
  expect_equal(normal_res$released, c(16L, 18L, 17L))

  unwind_res <- winrpa:::cpp_test_modifier_guard_lifecycle(c(17L, 18L, 16L), simulate_error = TRUE)
  expect_true(unwind_res$unwound)
  expect_equal(unwind_res$released, c(16L, 18L, 17L))
})

test_that("Lock-free HookEventRingBuffer preserves FIFO order across wrap-around", {
  res <- winrpa:::cpp_ring_buffer_selftest(300L)
  expect_equal(res$capacity, 256L)
  expect_equal(res$pushed, 300L)
  expect_equal(res$dropped, 44L)
  expect_equal(res$count, 256L)
  expect_equal(res$vk_codes, seq(45L, 300L))
})

test_that("Pure C++ 24-bit BMP encoder writes valid headers and row padding", {
  raw_bmp <- winrpa:::cpp_encode_bmp_test(5L, 3L)
  expect_type(raw_bmp, "raw")
  # 54-byte header + 3 rows * 16 padded bytes (5 * 3 = 15 -> padded to 16) = 102 bytes
  expect_length(raw_bmp, 102L)
  expect_equal(raw_bmp[1:2], charToRaw("BM"))

  width_bytes <- as.integer(raw_bmp[19:22])
  height_bytes <- as.integer(raw_bmp[23:26])
  expect_equal(sum(width_bytes * c(1L, 256L, 65536L, 16777216L)), 5L)
  expect_equal(sum(height_bytes * c(1L, 256L, 65536L, 16777216L)), 3L)
})

test_that("MtaComWorker restarts cleanly after cpp_shutdown_runtime()", {
  skip_on_os(c("mac", "linux", "solaris"))
  df1 <- winrpa:::cpp_uia_list_windows()
  expect_s3_class(df1, "data.frame")

  winrpa:::cpp_shutdown_runtime()

  df2 <- winrpa:::cpp_uia_list_windows()
  expect_s3_class(df2, "data.frame")
})
