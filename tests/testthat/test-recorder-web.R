make_mock_web_events <- function(...) {
  dots <- list(...)
  n <- max(vapply(dots, length, integer(1L)))
  col <- function(nm, default) {
    v <- dots[[nm]]
    if (is.null(v)) rep(default, n) else if (length(v) == 1L && n > 1L) rep(v, n) else v
  }
  data.frame(
    kind = col("kind", "click"),
    timestamp_ms = as.numeric(col("timestamp_ms", seq_len(n) * 100)),
    url = col("url", "https://example.org/login"),
    selector = col("selector", "#btn"),
    tag = col("tag", "button"),
    input_type = col("input_type", ""),
    value = col("value", ""),
    button = as.integer(col("button", 0L)),
    clicks = as.integer(col("clicks", 1L)),
    key = col("key", ""),
    is_password = as.logical(col("is_password", FALSE)),
    stringsAsFactors = FALSE
  )
}

test_that("rpa_web_record_emit validates inputs and produces valid coalesced scripts", {
  expect_error(rpa_web_record_emit("invalid"), "winrpa_recording")
  expect_error(rpa_web_record_start("not_a_session"), "winrpa_web_session")
  expect_error(rpa_web_record_stop("not_a_session"), "winrpa_web_session")

  df <- make_mock_web_events(
    kind = c("navigate", "click", "input", "input", "click", "input", "input", "select", "click", "dblclick"),
    timestamp_ms = c(10, 100, 150, 220, 300, 350, 410, 500, 600, 650),
    url = "https://example.org/login",
    selector = c("", "#user", "#user", "#user", "#pwd", "#pwd", "#pwd", "#role", "#submit", "#submit"),
    tag = c("", "input", "input", "input", "input", "input", "input", "select", "button", "button"),
    input_type = c("", "text", "text", "text", "password", "password", "password", "select", "submit", "submit"),
    value = c("", "", "a", "alice", "", "x", "secret123", "admin", "", ""),
    is_password = c(FALSE, FALSE, FALSE, FALSE, TRUE, TRUE, TRUE, FALSE, FALSE, FALSE)
  )

  rec <- rpa_web_record_emit(df, coalesce_typing = TRUE, redact_passwords = TRUE)
  expect_s3_class(rec, "winrpa_recording")
  expect_equal(rec$mode, "web")
  expect_no_error(parse(text = rec$script))

  txt <- paste(rec$script, collapse = "\n")
  expect_match(txt, 'rpa_web_goto\\(sess, "https://example.org/login"\\)', perl = TRUE)
  expect_match(txt, "rpa_web_wait_network_idle\\(sess\\)", perl = TRUE)
  # Redundant click before text/password input is dropped; cumulative typing coalesced to last value
  expect_false(grepl('rpa_web_click\\(sess, css = "#user"\\)', txt, fixed = TRUE))
  expect_match(txt, 'rpa_web_type\\(sess, "alice", css = "#user", clear = TRUE\\)', perl = TRUE)
  # Password never leaks into script
  expect_match(txt, 'rpa_web_type\\(sess, Sys\\.getenv\\("WINRPA_SECRET"\\), css = "#pwd", clear = TRUE, secret = TRUE\\)', perl = TRUE)
  expect_false(grepl("secret123", txt, fixed = TRUE))
  # Select change emitted
  expect_match(txt, 'rpa_web_wait_selector\\(sess, css = "#role"\\)', perl = TRUE)
  # Double click collapsed to clicks = 2L
  expect_match(txt, 'rpa_web_click\\(sess, css = "#submit", clicks = 2L\\)', perl = TRUE)
})

test_that("live headless CDP web recorder captures real DOM clicks, typing, selects, and password redaction", {
  skip_on_cran()
  skip_if_not(.Platform$OS.type == "windows", "Windows-only CDP integration test")
  browser_exe <- winrpa:::cpp_cdp_find_browser("auto")
  skip_if_not(nzchar(browser_exe), "Microsoft Edge or Google Chrome not installed")

  sess <- rpa_web_launch(browser = "auto", headless = TRUE)
  on.exit(try(rpa_web_close(sess), silent = TRUE), add = TRUE)

  html <- paste0(
    "<html><body>",
    "<form onsubmit='event.preventDefault();document.getElementById(\"out\").textContent=\"submitted\";'>",
    "<input id='user' name='username' type='text'>",
    "<input id='pwd' name='password' type='password'>",
    "<select id='role'><option value='viewer'>Viewer</option><option value='admin'>Admin</option></select>",
    "<button id='go' type='submit'>Sign In</button>",
    "</form><div id='out'>idle</div></body></html>"
  )
  url <- paste0("data:text/html,", utils::URLencode(html, reserved = TRUE))
  rpa_web_goto(sess, url, wait = "load", timeout = 15)

  expect_true(rpa_web_record_start(sess, redact_passwords = TRUE))

  rpa_web_click(sess, css = "#user")
  rpa_web_type(sess, "dr_alice", css = "#user")
  rpa_web_click(sess, css = "#pwd")
  rpa_web_type(sess, "TopSecret99!", css = "#pwd", secret = TRUE)
  rpa_web_eval(sess, "(function(){var s=document.getElementById('role');s.value='admin';s.dispatchEvent(new Event('change',{bubbles:true}));})()")
  rpa_web_click(sess, css = "#go")
  Sys.sleep(0.15)

  rec <- rpa_web_record_stop(sess)
  expect_s3_class(rec, "winrpa_recording")
  expect_no_error(parse(text = rec$script))

  txt <- paste(rec$script, collapse = "\n")
  expect_match(txt, 'rpa_web_type\\(sess, "dr_alice", css = "#user", clear = TRUE\\)', perl = TRUE)
  expect_match(txt, 'rpa_web_type\\(sess, Sys\\.getenv\\("WINRPA_SECRET"\\), css = "#pwd", clear = TRUE, secret = TRUE\\)', perl = TRUE)
  expect_false(grepl("TopSecret99!", txt, fixed = TRUE))
  expect_match(txt, 'rpa_web_click\\(sess, css = "#go"\\)', perl = TRUE)
  expect_true(all(!grepl("TopSecret99!", rec$events$value, fixed = TRUE)))
})

test_that("live CDP web recorder captures Shadow DOM, iframe, and autofill username/password inputs and replays >>> selectors", {
  skip_on_cran()
  skip_if_not(.Platform$OS.type == "windows", "Windows-only CDP integration test")
  browser_exe <- winrpa:::cpp_cdp_find_browser("auto")
  skip_if_not(nzchar(browser_exe), "Microsoft Edge or Google Chrome not installed")

  sess <- rpa_web_launch(browser = "auto", headless = TRUE)
  on.exit(try(rpa_web_close(sess), silent = TRUE), add = TRUE)

  html <- paste0(
    "<html><body>",
    "<div id='host'></div>",
    "<input id='auto_u' type='text'>",
    "<input id='auto_p' type='password'>",
    "<iframe id='fr' style='width:320px;height:140px;' srcdoc=\"<input id='if_u' type='text'><input id='if_p' type='password'><button id='if_btn' type='button' onclick='this.textContent=&quot;clicked&quot;'>Go</button>\"></iframe>",
    "<script>",
    "var sh = document.getElementById('host').attachShadow({mode:'open'});",
    "sh.innerHTML = '<input id=\"sh_u\" type=\"text\"><input id=\"sh_p\" type=\"password\">';",
    "</script></body></html>"
  )
  rpa_web_goto(sess, paste0("data:text/html,", utils::URLencode(html, reserved = TRUE)), wait = "load", timeout = 15)

  expect_true(rpa_web_record_start(sess, redact_passwords = TRUE))

  # 1. Shadow DOM typing (username + password) via piercing selector
  rpa_web_click(sess, css = "#host >>> #sh_u")
  rpa_web_type(sess, "shadow_user", css = "#host >>> #sh_u")
  rpa_web_click(sess, css = "#host >>> #sh_p")
  rpa_web_type(sess, "ShadowPass1!", css = "#host >>> #sh_p", secret = TRUE)

  # 2. Autofill / change-only + silent unblurred populate before stop
  rpa_web_eval(sess, "var a=document.getElementById('auto_u'); a.value='auto_user'; a.dispatchEvent(new Event('change',{bubbles:true}));")
  rpa_web_eval(sess, "document.getElementById('auto_p').value='SilentPass2!';")

  # 3. Same-origin iframe typing (username + password) + button click via piercing selector
  rpa_web_click(sess, css = "#fr >>> #if_u")
  rpa_web_type(sess, "iframe_user", css = "#fr >>> #if_u")
  rpa_web_click(sess, css = "#fr >>> #if_p")
  rpa_web_type(sess, "IframePass3!", css = "#fr >>> #if_p", secret = TRUE)
  rpa_web_click(sess, css = "#fr >>> #if_btn")
  Sys.sleep(0.15)

  rec <- rpa_web_record_stop(sess)
  expect_s3_class(rec, "winrpa_recording")
  expect_no_error(parse(text = rec$script))

  txt <- paste(rec$script, collapse = "\n")
  expect_match(txt, 'rpa_web_type\\(sess, "shadow_user", css = "#host >>> #sh_u", clear = TRUE\\)', perl = TRUE)
  expect_match(txt, 'rpa_web_type\\(sess, Sys\\.getenv\\("WINRPA_SECRET"\\), css = "#host >>> #sh_p", clear = TRUE, secret = TRUE\\)', perl = TRUE)
  expect_match(txt, 'rpa_web_type\\(sess, "auto_user", css = "#auto_u", clear = TRUE\\)', perl = TRUE)
  expect_match(txt, 'rpa_web_type\\(sess, Sys\\.getenv\\("WINRPA_SECRET"\\), css = "#auto_p", clear = TRUE, secret = TRUE\\)', perl = TRUE)
  expect_match(txt, 'rpa_web_type\\(sess, "iframe_user", css = "#fr >>> #if_u", clear = TRUE\\)', perl = TRUE)
  expect_match(txt, 'rpa_web_type\\(sess, Sys\\.getenv\\("WINRPA_SECRET"\\), css = "#fr >>> #if_p", clear = TRUE, secret = TRUE\\)', perl = TRUE)
  expect_match(txt, 'rpa_web_click\\(sess, css = "#fr >>> #if_btn"\\)', perl = TRUE)

  # Verify no plaintext passwords leaked anywhere
  expect_false(grepl("ShadowPass1!", txt, fixed = TRUE))
  expect_false(grepl("SilentPass2!", txt, fixed = TRUE))
  expect_false(grepl("IframePass3!", txt, fixed = TRUE))
  expect_true(all(!grepl("ShadowPass1!|SilentPass2!|IframePass3!", rec$events$value, perl = TRUE)))

  # Verify emitted iframe click hit the iframe button accurately
  btn_txt <- rpa_web_text(sess, css = "#fr >>> #if_btn")
  expect_equal(btn_txt, "clicked")
})
