test_that("Pure C++17 JSON and Base64 codecs round-trip cleanly", {
  js_res <- winrpa:::cpp_cdp_json_selftest()
  expect_true(isTRUE(js_res$ok))
  expect_identical(js_res$roundtrip_text, "hello \"winrpa\"\\line\n")
  expect_identical(js_res$unicode_text, "caf\u00e9")
  expect_equal(js_res$count, 42L)
  expect_true(isTRUE(js_res$flag))
  expect_true(isTRUE(js_res$invalid_threw))

  # "d2lucnBh" -> "winrpa", "iVBORw==" -> c(0x89, 0x50, 0x4E, 0x47)
  expect_identical(winrpa:::cpp_cdp_base64_selftest("d2lucnBh"), charToRaw("winrpa"))
  expect_identical(
    as.integer(winrpa:::cpp_cdp_base64_selftest("iVBORw==")),
    c(0x89L, 0x50L, 0x4EL, 0x47L)
  )
  expect_identical(
    winrpa:::cpp_cdp_base64_selftest("YWI="),
    charToRaw("ab")
  )

  nf <- winrpa:::cpp_cdp_network_filter_selftest()
  expect_false(isTRUE(nf$websocket))
  expect_false(isTRUE(nf$eventsource))
  expect_false(isTRUE(nf$ping))
  expect_false(isTRUE(nf$csp))
  expect_false(isTRUE(nf$preflight))
  expect_false(isTRUE(nf$data_url))
  expect_false(isTRUE(nf$blob_url))
  expect_true(isTRUE(nf$xhr))
  expect_true(isTRUE(nf$fetch))
  expect_true(isTRUE(nf$document))
})

test_that("rpa_session_check() supports require_interactive = FALSE and rpa_post_* dispatch cleanly", {
  expect_true("require_interactive" %in% names(formals(rpa_session_check)))
  expect_identical(formals(rpa_session_check)$require_interactive, TRUE)
  expect_true(isTRUE(rpa_session_check(allow_non_windows = TRUE, require_interactive = FALSE)))

  for (fn_name in c("rpa_post_click", "rpa_post_text")) {
    expect_true(exists(fn_name, mode = "function"), info = fn_name)
  }

  expect_error(rpa_post_click(list()), "winrpa_element")
  expect_error(rpa_post_text(list(), "abc"), "winrpa_element")
  expect_error(rpa_post_click("0x1234", button = "middle"))

  secret_pw <- "HiddenBgSecret!77"
  err <- tryCatch(
    rpa_post_text("not_a_hwnd", secret_pw, secret = TRUE),
    error = function(e) conditionMessage(e)
  )
  expect_false(grepl(secret_pw, err, fixed = TRUE))

  fake_elem <- structure(list(ptr = NULL), class = "winrpa_element")
  log_env <- new.env(parent = emptyenv())
  log_env$clicks <- list()
  log_env$texts <- list()
  log_env$sess_args <- list()

  testthat::local_mocked_bindings(
    rpa_session_check = function(allow_non_windows = FALSE, require_interactive = TRUE) {
      log_env$sess_args <- c(log_env$sess_args, list(require_interactive))
      invisible(TRUE)
    },
    cpp_uia_element_info = function(ptr) {
      list(hwnd = "0x1234", left = 10L, top = 20L, right = 110L, bottom = 60L)
    },
    cpp_post_click = function(hwnd_hex, x, y, button, use_screen_coords) {
      log_env$clicks <- c(log_env$clicks, list(list(
        hwnd = hwnd_hex, x = x, y = y, button = button, screen = use_screen_coords
      )))
      invisible(NULL)
    },
    cpp_post_text = function(hwnd_hex, text) {
      log_env$texts <- c(log_env$texts, list(list(hwnd = hwnd_hex, text = text)))
      invisible(NULL)
    }
  )

  expect_identical(rpa_post_click(fake_elem), fake_elem)
  expect_identical(rpa_post_text(fake_elem, "bg payload"), fake_elem)
  expect_equal(log_env$clicks[[1L]]$hwnd, "0x1234")
  expect_equal(log_env$clicks[[1L]]$x, 60L)
  expect_equal(log_env$clicks[[1L]]$y, 40L)
  expect_true(isTRUE(log_env$clicks[[1L]]$screen))
  expect_equal(log_env$texts[[1L]]$text, "bg payload")
  expect_true(all(!vapply(log_env$sess_args, isTRUE, logical(1L))))
})

test_that("rpa_web_* API surface, validation, S3 methods, and secret masking work cleanly", {
  web_fns <- c(
    "rpa_web_launch",
    "rpa_web_connect",
    "rpa_web_close",
    "rpa_web_goto",
    "rpa_web_find",
    "rpa_web_find_all",
    "rpa_web_click",
    "rpa_web_type",
    "rpa_web_text",
    "rpa_web_eval",
    "rpa_web_wait_load",
    "rpa_web_wait_network_idle",
    "rpa_web_wait_selector",
    "rpa_web_screenshot",
    "rpa_web_set_files",
    "rpa_web_window"
  )
  for (fn in web_fns) {
    expect_true(exists(fn, mode = "function"), info = fn)
  }

  fake_sess <- structure(
    list(
      ptr = NULL,
      host = "127.0.0.1",
      port = 19222L,
      pid = 999L,
      browser = "edge",
      headless = TRUE,
      profile_dir = ""
    ),
    class = "winrpa_web_session"
  )
  expect_match(format(fake_sess), "winrpa_web_session")
  out_s <- capture.output(res_s <- print(fake_sess))
  expect_identical(res_s, fake_sess)
  expect_true(length(out_s) >= 1L)

  fake_web_el <- structure(
    list(session = fake_sess, selector = "#btn", by = "css", index = 1L),
    class = "winrpa_web_element"
  )
  expect_match(format(fake_web_el), "winrpa_web_element")
  out_e <- capture.output(res_e <- print(fake_web_el))
  expect_identical(res_e, fake_web_el)
  expect_true(length(out_e) >= 1L)

  expect_error(rpa_web_find(fake_sess), "css|xpath")
  expect_error(rpa_web_find(fake_sess, css = "#a", xpath = "//a"), "exactly one")
  expect_error(rpa_web_find(fake_sess, css = ""), "non-empty")
  expect_error(rpa_web_set_files(fake_web_el, tempfile("no_such_file_")), "exist")

  secret_str <- "TopSecretWebToken!88"
  err_w <- tryCatch(
    rpa_web_type("bad_target", secret_str, css = "#pw", secret = TRUE),
    error = function(e) conditionMessage(e)
  )
  expect_false(grepl(secret_str, err_w, fixed = TRUE))
})

test_that("Headless Edge/Chrome CDP end-to-end works (CSS, XPath, click, type, DOM/idle waits, upload, PNG)", {
  skip_on_cran()
  skip_on_os(c("mac", "linux", "solaris"))
  browser_bin <- winrpa:::cpp_cdp_find_browser("auto")
  skip_if_not(nzchar(browser_bin), "Neither Microsoft Edge nor Google Chrome is installed.")

  sess <- rpa_web_launch(browser = "auto", headless = TRUE)
  on.exit(try(rpa_web_close(sess), silent = TRUE), add = TRUE)

  html <- paste0(
    "<!doctype html><html><head><meta charset='utf-8'><title>winrpa CDP Test</title></head>",
    "<body>",
    "<h1 id='title'>winrpa CDP Heading</h1>",
    "<input id='name-box' type='text' value='' />",
    "<button id='btn-go' type='button'>Run Action</button>",
    "<input id='file-up' type='file' />",
    "<div id='status'>idle</div>",
    "<script>",
    "document.getElementById('btn-go').addEventListener('click', function() {",
    "  var v = document.getElementById('name-box').value;",
    "  document.getElementById('status').textContent = 'clicked:' + v;",
    "  setTimeout(function() {",
    "    var d = document.createElement('div');",
    "    d.id = 'delayed';",
    "    d.textContent = 'async-ready';",
    "    document.body.appendChild(d);",
    "  }, 80);",
    "});",
    "</script></body></html>"
  )
  data_url <- paste0("data:text/html;charset=utf-8,", utils::URLencode(html, reserved = TRUE))

  rpa_web_goto(sess, data_url, wait = "load", timeout = 15)
  rpa_web_wait_load(sess, state = "domcontentloaded", timeout = 10)
  rpa_web_wait_network_idle(sess, idle_ms = 100L, timeout = 10)

  expect_identical(rpa_web_text(sess, css = "#title"), "winrpa CDP Heading")
  btn_css <- rpa_web_find(sess, css = "#btn-go")
  btn_xp <- rpa_web_find(sess, xpath = "//button[@id='btn-go']")
  expect_s3_class(btn_css, "winrpa_web_element")
  expect_s3_class(btn_xp, "winrpa_web_element")

  rpa_web_type(sess, "winrpa CDP caf\u00e9", css = "#name-box")
  rpa_web_click(btn_xp)

  expect_identical(rpa_web_text(sess, css = "#status"), "clicked:winrpa CDP caf\u00e9")
  delayed <- rpa_web_wait_selector(sess, css = "#delayed", timeout = 5)
  expect_identical(rpa_web_text(delayed), "async-ready")

  # Smooth-scroll page regression: clicking off-screen button below fold must still hit
  rpa_web_eval(sess, paste0(
    "document.documentElement.style.scrollBehavior = 'smooth';",
    "var f = document.createElement('button');",
    "f.id = 'far-btn'; f.textContent = 'Far Button';",
    "f.style.marginTop = '3200px';",
    "f.addEventListener('click', function(){ document.getElementById('status').textContent = 'far-clicked'; });",
    "document.body.appendChild(f);"
  ))
  rpa_web_click(sess, css = "#far-btn")
  expect_identical(rpa_web_text(sess, css = "#status"), "far-clicked")

  up_file <- tempfile("cdp_upload_", fileext = ".txt")
  on.exit(unlink(up_file), add = TRUE)
  writeLines("upload ok", up_file)
  rpa_web_set_files(sess, up_file, css = "#file-up")
  expect_equal(rpa_web_eval(sess, "document.getElementById('file-up').files.length"), 1L)

  out_png <- tempfile("cdp_shot_", fileext = ".png")
  on.exit(unlink(out_png), add = TRUE)
  res_png <- rpa_web_screenshot(sess, path = out_png)
  expect_true(file.exists(res_png))
  expect_true(file.info(res_png)$size > 500)
  hdr <- readBin(res_png, what = "raw", n = 4L)
  expect_identical(as.integer(hdr), c(0x89L, 0x50L, 0x4EL, 0x47L))
})
