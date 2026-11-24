assert_web_session <- function(session, arg_name = "session") {
  if (!inherits(session, "winrpa_web_session")) {
    rlang::abort(sprintf("'%s' must be a 'winrpa_web_session' object.", arg_name))
  }
  if (is.null(session$ptr)) {
    rlang::abort("Web CDP session is not connected.")
  }
  session
}

validate_web_selector <- function(css = NULL, xpath = NULL) {
  has_css <- !is.null(css)
  has_xpath <- !is.null(xpath)
  if (!has_css && !has_xpath) {
    rlang::abort("Supply either 'css' or 'xpath' to locate a web element.")
  }
  if (has_css && has_xpath) {
    rlang::abort("Specify exactly one of 'css' or 'xpath', not both.")
  }
  by <- if (has_css) "css" else "xpath"
  val <- if (has_css) css else xpath
  if (!is.character(val) || length(val) != 1L || is.na(val) || !nzchar(val)) {
    rlang::abort(sprintf("'%s' must be a single non-empty character string.", by))
  }
  list(by = by, selector = val)
}

resolve_web_target <- function(target, css = NULL, xpath = NULL, default_index = 1L) {
  if (inherits(target, "winrpa_web_element")) {
    if (!is.null(css) || !is.null(xpath)) {
      rlang::abort("Do not pass 'css' or 'xpath' when 'target' is already a 'winrpa_web_element'.")
    }
    assert_web_session(target$session, "target$session")
    return(target)
  }
  if (inherits(target, "winrpa_web_session")) {
    assert_web_session(target, "target")
    spec <- validate_web_selector(css, xpath)
    return(structure(
      list(
        session = target,
        selector = spec$selector,
        by = spec$by,
        index = as.integer(default_index)
      ),
      class = "winrpa_web_element"
    ))
  }
  rlang::abort("'target' must be a 'winrpa_web_session' or 'winrpa_web_element' object.")
}

cdp_json_str <- function(x) {
  s <- as.character(x)
  s <- gsub("\\", "\\\\", s, fixed = TRUE)
  s <- gsub("\"", "\\\"", s, fixed = TRUE)
  s <- gsub("\b", "\\b", s, fixed = TRUE)
  s <- gsub("\f", "\\f", s, fixed = TRUE)
  s <- gsub("\n", "\\n", s, fixed = TRUE)
  s <- gsub("\r", "\\r", s, fixed = TRUE)
  s <- gsub("\t", "\\t", s, fixed = TRUE)
  paste0("\"", s, "\"")
}

cdp_call <- function(session, method, params_json = "{}", timeout_ms = 15000L) {
  assert_web_session(session)
  if (rpa_abort_triggered()) {
    rlang::abort("winrpa emergency abort triggered before CDP command.")
  }
  raw <- cpp_cdp_send(session$ptr, method, params_json, as.integer(timeout_ms))
  cpp_cdp_parse_json(raw)
}

cdp_eval_raw <- function(session,
                         expression,
                         return_by_value = TRUE,
                         await_promise = TRUE,
                         timeout_ms = 15000L) {
  params <- sprintf(
    '{"expression":%s,"returnByValue":%s,"awaitPromise":%s}',
    cdp_json_str(expression),
    if (isTRUE(return_by_value)) "true" else "false",
    if (isTRUE(await_promise)) "true" else "false"
  )
  res <- cdp_call(session, "Runtime.evaluate", params, timeout_ms = timeout_ms)
  if (!is.null(res$exceptionDetails)) {
    desc <- res$exceptionDetails$exception$description
    if (!is.character(desc) || !nzchar(desc)) {
      desc <- res$exceptionDetails$text
    }
    if (!is.character(desc) || !nzchar(desc)) {
      desc <- "JavaScript evaluation threw an exception."
    }
    rlang::abort(desc)
  }
  res$result
}

build_query_deep_js <- function() {
  paste0(
    "function qDeep(sel){",
    "if(sel.indexOf(' >>> ')===-1){return document.querySelectorAll(sel);}",
    "var parts=sel.split(' >>> '),roots=[document];",
    "for(var i=0;i<parts.length;i++){",
    "var p=parts[i].trim(),next=[];",
    "for(var r=0;r<roots.length;r++){",
    "var hit=[];try{hit=roots[r].querySelectorAll(p);}catch(e){hit=[];}",
    "for(var h=0;h<hit.length;h++){",
    "if(i<parts.length-1){var node=hit[h];if(node.shadowRoot){next.push(node.shadowRoot);}else if(node.contentDocument){next.push(node.contentDocument);}}",
    "else{next.push(hit[h]);}",
    "}",
    "}",
    "roots=next;if(roots.length===0)return [];",
    "}",
    "return roots;",
    "}"
  )
}

build_locate_js <- function(by, selector, index = 1L) {
  idx0 <- as.integer(index) - 1L
  qsel <- cdp_json_str(selector)
  if (by == "css") {
    sprintf("(function(){%sreturn qDeep(%s)[%d] || null;})()", build_query_deep_js(), qsel, idx0)
  } else {
    sprintf(
      "(function(){var r=document.evaluate(%s,document,null,XPathResult.ORDERED_NODE_SNAPSHOT_TYPE,null);return r.snapshotItem(%d);})()",
      qsel, idx0
    )
  }
}

build_count_js <- function(by, selector) {
  qsel <- cdp_json_str(selector)
  if (by == "css") {
    sprintf("(function(){%sreturn qDeep(%s).length;})()", build_query_deep_js(), qsel)
  } else {
    sprintf(
      "(function(){var r=document.evaluate(%s,document,null,XPathResult.ORDERED_NODE_SNAPSHOT_TYPE,null);return r.snapshotLength;})()",
      qsel
    )
  }
}

#' Format or Print a `winrpa_web_session` Handle
#'
#' @param x A `winrpa_web_session` object.
#' @param ... Unused additional arguments.
#'
#' @return `format()` returns a character scalar; `print()` invisibly returns
#'   `x`.
#' @export
#' @examples
#' s <- structure(
#'   list(ptr = NULL, host = "127.0.0.1", port = 9222L, pid = 0L, browser = "edge", headless = TRUE),
#'   class = "winrpa_web_session"
#' )
#' format(s)
format.winrpa_web_session <- function(x, ...) {
  sprintf(
    "<winrpa_web_session: browser=%s, host=%s:%s, pid=%s, headless=%s>",
    as.character(x$browser),
    as.character(x$host),
    as.character(x$port),
    as.character(x$pid),
    if (isTRUE(x$headless)) "TRUE" else "FALSE"
  )
}

#' @rdname format.winrpa_web_session
#' @export
print.winrpa_web_session <- function(x, ...) {
  cat(format(x), "\n", sep = "")
  invisible(x)
}

#' Format or Print a `winrpa_web_element` Handle
#'
#' @param x A `winrpa_web_element` object.
#' @param ... Unused additional arguments.
#'
#' @return `format()` returns a character scalar; `print()` invisibly returns
#'   `x`.
#' @export
#' @examples
#' el <- structure(
#'   list(session = NULL, selector = "#submit", by = "css", index = 1L),
#'   class = "winrpa_web_element"
#' )
#' format(el)
format.winrpa_web_element <- function(x, ...) {
  sprintf(
    '<winrpa_web_element: [%s] "%s" (index=%d)>',
    as.character(x$by),
    as.character(x$selector),
    as.integer(x$index)
  )
}

#' @rdname format.winrpa_web_element
#' @export
print.winrpa_web_element <- function(x, ...) {
  cat(format(x), "\n", sep = "")
  invisible(x)
}

#' Launch or Connect to a Chromium Browser Over Native Chrome DevTools Protocol
#'
#' Starts Microsoft Edge or Google Chrome cleanly in headless (`headless = TRUE`)
#' or visible mode on a dedicated loopback remote debugging port using Windows
#' WinHTTP WebSockets (with zero external R package dependencies), or attaches to
#' an already-running Chromium instance via `rpa_web_connect()`.
#'
#' @param browser Browser selector: `"auto"`, `"edge"`, `"chrome"`, or a full
#'   path to a Chromium executable.
#' @param headless Logical scalar; if `TRUE`, launches with `--headless=new` so
#'   automation runs in the background without occupying desktop focus.
#' @param url Initial URL to open (`"about:blank"` by default).
#' @param port Optional integer remote-debugging port (`NULL` picks a free
#'   loopback port automatically).
#' @param args Character vector of extra Chromium CLI flags.
#' @param host Remote debugging host (`"127.0.0.1"` by default).
#' @param target_index 1-based page target index to attach to in
#'   `rpa_web_connect()`.
#' @param timeout Numeric connection timeout in seconds.
#' @param session A `winrpa_web_session` object.
#'
#' @return `rpa_web_launch()` and `rpa_web_connect()` return a
#'   `"winrpa_web_session"` object; `rpa_web_close()` invisibly returns `NULL`.
#' @export
#' @examplesIf interactive() && .Platform$OS.type == "windows"
#' s <- rpa_web_launch(headless = TRUE)
#' rpa_web_close(s)
rpa_web_launch <- function(browser = c("auto", "edge", "chrome"),
                           headless = TRUE,
                           url = "about:blank",
                           port = NULL,
                           args = character(),
                           timeout = 15) {
  if (identical(browser, c("auto", "edge", "chrome"))) {
    browser <- "auto"
  }
  if (!is.character(browser) || length(browser) != 1L || is.na(browser) || !nzchar(browser)) {
    rlang::abort("'browser' must be \"auto\", \"edge\", \"chrome\", or a single non-empty executable path.")
  }
  if (!is.logical(headless) || length(headless) != 1L || is.na(headless)) {
    rlang::abort("'headless' must be a single non-NA logical value.")
  }
  if (!is.character(url) || length(url) != 1L || is.na(url)) {
    rlang::abort("'url' must be a single non-NA character string.")
  }
  if (!is.null(port) && (!is.numeric(port) || length(port) != 1L || is.na(port) || port <= 0)) {
    rlang::abort("'port' must be NULL or a positive integer.")
  }
  if (!is.character(args) || anyNA(args)) {
    rlang::abort("'args' must be a character vector with no NA values.")
  }
  if (!is.numeric(timeout) || length(timeout) != 1L || is.na(timeout) || timeout <= 0) {
    rlang::abort("'timeout' must be a positive numeric timeout.")
  }

  rpa_session_check(require_interactive = FALSE)
  res <- cpp_cdp_launch(
    browser,
    isTRUE(headless),
    url,
    if (is.null(port)) 0L else as.integer(port),
    args,
    as.integer(timeout * 1000)
  )
  structure(res, class = "winrpa_web_session")
}

#' @rdname rpa_web_launch
#' @export
rpa_web_connect <- function(port = 9222L,
                            host = "127.0.0.1",
                            target_index = 1L,
                            timeout = 8) {
  if (!is.numeric(port) || length(port) != 1L || is.na(port) || port <= 0) {
    rlang::abort("'port' must be a positive integer.")
  }
  if (!is.character(host) || length(host) != 1L || is.na(host) || !nzchar(host)) {
    rlang::abort("'host' must be a non-empty character string.")
  }
  if (!is.numeric(target_index) || length(target_index) != 1L || is.na(target_index) || target_index < 1) {
    rlang::abort("'target_index' must be a positive integer.")
  }
  if (!is.numeric(timeout) || length(timeout) != 1L || is.na(timeout) || timeout <= 0) {
    rlang::abort("'timeout' must be a positive numeric timeout.")
  }

  rpa_session_check(require_interactive = FALSE)
  res <- cpp_cdp_connect(
    host,
    as.integer(port),
    as.integer(target_index),
    as.integer(timeout * 1000)
  )
  structure(res, class = "winrpa_web_session")
}

#' @rdname rpa_web_launch
#' @export
rpa_web_close <- function(session) {
  if (!inherits(session, "winrpa_web_session")) {
    rlang::abort("'session' must be a 'winrpa_web_session' object.")
  }
  if (!is.null(session$ptr)) {
    try(cpp_cdp_close(session$ptr), silent = TRUE)
  }
  invisible(NULL)
}

#' Navigate and Wait for Page Load or Network Idle Over CDP
#'
#' Navigates a CDP browser session via `Page.navigate` and waits deterministically
#' for true `DOMContentLoaded`, `load`, or XHR/fetch network-idle events.
#'
#' @param session A `winrpa_web_session` object.
#' @param url Destination URL (for example `https://`, `file://`, or `data:`
#'   schemes).
#' @param wait Post-navigation wait mode: `"load"`, `"domcontentloaded"`, or
#'   `"none"`.
#' @param state Load readiness state for `rpa_web_wait_load()`: `"load"` or
#'   `"domcontentloaded"`.
#' @param idle_ms Minimum quiet time in milliseconds with `<= max_inflight`
#'   active HTTP/XHR/fetch requests for `rpa_web_wait_network_idle()`.
#' @param max_inflight Maximum tolerated in-flight network requests to consider
#'   network idle (default `0L`).
#' @param timeout Maximum wait time in seconds.
#' @param interval Polling interval in seconds.
#'
#' @return Invisibly returns `session` for pipe chaining.
#' @export
#' @examplesIf interactive() && .Platform$OS.type == "windows"
#' s <- rpa_web_launch(headless = TRUE)
#' on.exit(rpa_web_close(s), add = TRUE)
#' rpa_web_goto(s, "about:blank")
rpa_web_goto <- function(session,
                         url,
                         wait = c("load", "domcontentloaded", "none"),
                         timeout = 15) {
  assert_web_session(session)
  if (!is.character(url) || length(url) != 1L || is.na(url) || !nzchar(url)) {
    rlang::abort("'url' must be a single non-empty character string.")
  }
  wait <- match.arg(wait)
  if (!is.numeric(timeout) || length(timeout) != 1L || is.na(timeout) || timeout <= 0) {
    rlang::abort("'timeout' must be a positive numeric timeout.")
  }

  rpa_session_check(require_interactive = FALSE)
  s0 <- cpp_cdp_event_state(session$ptr)
  nav <- cdp_call(
    session,
    "Page.navigate",
    sprintf('{"url":%s}', cdp_json_str(url)),
    timeout_ms = as.integer(timeout * 1000)
  )
  if (is.character(nav$errorText) && nzchar(nav$errorText)) {
    rlang::abort(sprintf("Navigation failed for '%s': %s", url, nav$errorText))
  }

  if (wait != "none") {
    rpa_wait_for(
      function() {
        cur <- cpp_cdp_event_state(session$ptr)
        seq_ok <- if (wait == "load") cur$load_seq > s0$load_seq else cur$dom_seq > s0$dom_seq
        if (isTRUE(seq_ok)) {
          return(TRUE)
        }
        rs <- tryCatch(rpa_web_eval(session, "document.readyState"), error = function(e) "")
        if (wait == "domcontentloaded" && identical(rs, "interactive")) {
          return(TRUE)
        }
        NULL
      },
      timeout = timeout,
      interval = 0.03,
      label = sprintf("page %s for %s", wait, url)
    )
  }
  invisible(session)
}

#' @rdname rpa_web_goto
#' @export
rpa_web_wait_load <- function(session,
                              state = c("load", "domcontentloaded"),
                              timeout = 15,
                              interval = 0.05) {
  assert_web_session(session)
  state <- match.arg(state)
  rpa_session_check(require_interactive = FALSE)

  rpa_wait_for(
    function() {
      rs <- tryCatch(rpa_web_eval(session, "document.readyState"), error = function(e) "")
      if (state == "load" && identical(rs, "complete")) {
        return(TRUE)
      }
      if (state == "domcontentloaded" && rs %in% c("interactive", "complete")) {
        return(TRUE)
      }
      NULL
    },
    timeout = timeout,
    interval = interval,
    label = sprintf("document %s state", state)
  )
  invisible(session)
}

#' @rdname rpa_web_goto
#' @export
rpa_web_wait_network_idle <- function(session,
                                      idle_ms = 500L,
                                      max_inflight = 0L,
                                      timeout = 15,
                                      interval = 0.05) {
  assert_web_session(session)
  if (!is.numeric(idle_ms) || length(idle_ms) != 1L || is.na(idle_ms) || idle_ms < 0) {
    rlang::abort("'idle_ms' must be a non-negative integer.")
  }
  if (!is.numeric(max_inflight) || length(max_inflight) != 1L || is.na(max_inflight) || max_inflight < 0) {
    rlang::abort("'max_inflight' must be a non-negative integer.")
  }
  rpa_session_check(require_interactive = FALSE)

  s0 <- cpp_cdp_event_state(session$ptr)
  last_state <- s0
  tryCatch(
    rpa_wait_for(
      function() {
        st <- cpp_cdp_event_state(session$ptr)
        last_state <<- st
        if (!isTRUE(st$connected)) {
          rlang::abort("Web CDP session disconnected while waiting for network idle.")
        }
        if (!is.null(st$net_idle_seq) && !is.null(s0$net_idle_seq) &&
            st$net_idle_seq > s0$net_idle_seq) {
          return(TRUE)
        }
        if (st$in_flight <= as.integer(max_inflight) &&
            st$ms_since_net_activity >= as.numeric(idle_ms)) {
          return(TRUE)
        }
        NULL
      },
      timeout = timeout,
      interval = interval,
      label = sprintf("network idle (>=%dms)", as.integer(idle_ms))
    ),
    error = function(e) {
      msg <- conditionMessage(e)
      if (grepl("Timed out", msg, fixed = TRUE)) {
        rlang::abort(sprintf(
          "%s (last observed in_flight = %s, quiet_ms = %.0f)",
          msg,
          as.character(last_state$in_flight),
          as.numeric(last_state$ms_since_net_activity)
        ))
      }
      rlang::abort(msg)
    }
  )
  invisible(session)
}

#' Find Web Elements Using CSS Selectors or XPath Over CDP
#'
#' Queries the live DOM using standard CSS selectors (`document.querySelectorAll`)
#' or XPath expressions (`document.evaluate`), returning `"winrpa_web_element"`
#' handles or waiting until a selector becomes present and visible.
#'
#' @param session A `winrpa_web_session` object.
#' @param css Optional CSS selector string.
#' @param xpath Optional XPath expression string.
#' @param index 1-based match index for `rpa_web_find()`.
#' @param visible Logical scalar; when `TRUE` in `rpa_web_wait_selector()`,
#'   requires non-zero bounding size and visible CSS `display`/`visibility`.
#' @param timeout Numeric maximum wait duration in seconds.
#' @param interval Polling interval in seconds.
#'
#' @return `rpa_web_find()` and `rpa_web_wait_selector()` return a
#'   `"winrpa_web_element"` object; `rpa_web_find_all()` returns a list of
#'   `"winrpa_web_element"` objects.
#' @export
#' @examplesIf interactive() && .Platform$OS.type == "windows"
#' s <- rpa_web_launch(headless = TRUE)
#' on.exit(rpa_web_close(s), add = TRUE)
#' rpa_web_goto(s, "about:blank")
#' rpa_web_find(s, css = "body")
rpa_web_find <- function(session, css = NULL, xpath = NULL, index = 1L) {
  spec <- validate_web_selector(css, xpath)
  if (!is.numeric(index) || length(index) != 1L || is.na(index) || index < 1) {
    rlang::abort("'index' must be a positive integer.")
  }
  assert_web_session(session)
  rpa_session_check(require_interactive = FALSE)
  n <- as.integer(cdp_eval_raw(session, build_count_js(spec$by, spec$selector))$value)
  if (is.na(n) || n < as.integer(index)) {
    rlang::abort(sprintf("No DOM element matched %s=\"%s\" at index %d.", spec$by, spec$selector, as.integer(index)))
  }
  structure(
    list(
      session = session,
      selector = spec$selector,
      by = spec$by,
      index = as.integer(index)
    ),
    class = "winrpa_web_element"
  )
}

#' @rdname rpa_web_find
#' @export
rpa_web_find_all <- function(session, css = NULL, xpath = NULL) {
  spec <- validate_web_selector(css, xpath)
  assert_web_session(session)
  rpa_session_check(require_interactive = FALSE)
  n <- as.integer(cdp_eval_raw(session, build_count_js(spec$by, spec$selector))$value)
  if (is.na(n) || n <= 0L) {
    return(list())
  }
  lapply(seq_len(n), function(idx) {
    structure(
      list(
        session = session,
        selector = spec$selector,
        by = spec$by,
        index = idx
      ),
      class = "winrpa_web_element"
    )
  })
}

#' @rdname rpa_web_find
#' @export
rpa_web_wait_selector <- function(session,
                                  css = NULL,
                                  xpath = NULL,
                                  visible = TRUE,
                                  timeout = 10,
                                  interval = 0.05) {
  assert_web_session(session)
  spec <- validate_web_selector(css, xpath)
  if (!is.logical(visible) || length(visible) != 1L || is.na(visible)) {
    rlang::abort("'visible' must be a single non-NA logical value.")
  }
  rpa_session_check(require_interactive = FALSE)

  loc_js <- build_locate_js(spec$by, spec$selector, 1L)
  check_js <- if (isTRUE(visible)) {
    sprintf(
      "(function(){var el=%s;if(!el)return false;var st=getComputedStyle(el);if(st.display==='none'||st.visibility==='hidden')return false;var r=el.getBoundingClientRect();return (r.width>0||r.height>0);})()",
      loc_js
    )
  } else {
    sprintf("(function(){return Boolean(%s);})()", loc_js)
  }

  rpa_wait_for(
    function() {
      ok <- tryCatch(cdp_eval_raw(session, check_js)$value, error = function(e) FALSE)
      if (isTRUE(ok)) TRUE else NULL
    },
    timeout = timeout,
    interval = interval,
    label = sprintf('%s selector "%s"', spec$by, spec$selector)
  )
  structure(
    list(session = session, selector = spec$selector, by = spec$by, index = 1L),
    class = "winrpa_web_element"
  )
}

#' Interact With Web Elements, Evaluate JavaScript, Upload Files, and Capture PNGs
#'
#' Dispatches trusted CDP mouse clicks (`rpa_web_click()`), full-Unicode typing
#' (`rpa_web_type()`), text/attribute reads (`rpa_web_text()`), JavaScript
#' evaluation (`rpa_web_eval()`), headless file input population
#' (`rpa_web_set_files()`), lossless PNG screenshots (`rpa_web_screenshot()`),
#' and desktop UIA handoff (`rpa_web_window()`).
#'
#' @param target A `winrpa_web_element` handle or a `winrpa_web_session`
#'   (paired with `css` or `xpath`).
#' @param css Optional CSS selector when `target` is a `winrpa_web_session`.
#' @param xpath Optional XPath selector when `target` is a `winrpa_web_session`.
#' @param button Mouse button (`"left"`, `"right"`, or `"middle"`).
#' @param clicks Positive integer click count.
#' @param text Character scalar to insert via `Input.insertText`.
#' @param clear Logical scalar; if `TRUE`, clears existing input value before
#'   inserting `text`.
#' @param secret Logical scalar; if `TRUE`, redacts `text` from error messages.
#' @param attr Optional HTML attribute name; when `NULL`, reads element text.
#' @param session A `winrpa_web_session` object.
#' @param expression Character scalar JavaScript expression to evaluate.
#' @param files Character vector of existing local file paths for
#'   `<input type="file">`.
#' @param path Output `.png` path (defaults to a temporary file in `tempdir()`).
#' @param full_page Logical scalar; if `TRUE`, captures beyond the current
#'   viewport.
#'
#' @return Action helpers invisibly return `target` (or `session`);
#'   `rpa_web_text()` returns a character scalar (or `NULL`); `rpa_web_eval()`
#'   returns the evaluated JS value converted cleanly to R; `rpa_web_screenshot()`
#'   returns the PNG file path; `rpa_web_window()` returns a `"winrpa_element"`
#'   window handle.
#' @export
#' @examplesIf interactive() && .Platform$OS.type == "windows"
#' s <- rpa_web_launch(headless = TRUE)
#' on.exit(rpa_web_close(s), add = TRUE)
#' rpa_web_eval(s, "1 + 1")
rpa_web_click <- function(target,
                          css = NULL,
                          xpath = NULL,
                          button = c("left", "right", "middle"),
                          clicks = 1L) {
  button <- match.arg(button)
  if (!is.numeric(clicks) || length(clicks) != 1L || is.na(clicks) || clicks < 1) {
    rlang::abort("'clicks' must be a positive integer.")
  }
  el <- resolve_web_target(target, css, xpath)
  rpa_session_check(require_interactive = FALSE)

  loc_js <- build_locate_js(el$by, el$selector, el$index)
  coords_js <- sprintf(
    "(function(){var el=%s;if(!el)return null;var ox=0,oy=0,win=el.ownerDocument?el.ownerDocument.defaultView:null;while(win&&win!==window&&win.frameElement){win.frameElement.scrollIntoView({block:'center',inline:'center',behavior:'instant'});var fr=win.frameElement.getBoundingClientRect();ox+=fr.left;oy+=fr.top;win=win.parent;}el.scrollIntoView({block:'center',inline:'center',behavior:'instant'});var r=el.getBoundingClientRect();return {x:ox+r.left+r.width/2,y:oy+r.top+r.height/2,w:r.width,h:r.height};})()",
    loc_js
  )
  coords <- cdp_eval_raw(el$session, coords_js)$value
  if (is.null(coords)) {
    rlang::abort(sprintf("Web element (%s=\"%s\") was not found in the DOM.", el$by, el$selector))
  }

  px <- as.numeric(coords$x)
  py <- as.numeric(coords$y)
  qbtn <- cdp_json_str(button)
  btn_mask <- switch(button, left = 1L, right = 2L, middle = 4L)
  cdp_call(
    el$session,
    "Input.dispatchMouseEvent",
    sprintf('{"type":"mouseMoved","x":%.3f,"y":%.3f}', px, py)
  )
  for (i in seq_len(as.integer(clicks))) {
    cdp_call(
      el$session,
      "Input.dispatchMouseEvent",
      sprintf(
        '{"type":"mousePressed","x":%.3f,"y":%.3f,"button":%s,"buttons":%d,"clickCount":%d}',
        px, py, qbtn, btn_mask, i
      )
    )
    cdp_call(
      el$session,
      "Input.dispatchMouseEvent",
      sprintf(
        '{"type":"mouseReleased","x":%.3f,"y":%.3f,"button":%s,"buttons":0,"clickCount":%d}',
        px, py, qbtn, i
      )
    )
  }
  invisible(target)
}

#' @rdname rpa_web_click
#' @export
rpa_web_type <- function(target,
                         text,
                         css = NULL,
                         xpath = NULL,
                         clear = FALSE,
                         secret = FALSE) {
  if (!is.character(text) || length(text) != 1L || is.na(text)) {
    rlang::abort("'text' must be a single non-NA character string.")
  }
  if (!is.logical(clear) || length(clear) != 1L || is.na(clear)) {
    rlang::abort("'clear' must be a single non-NA logical value.")
  }
  if (!is.logical(secret) || length(secret) != 1L || is.na(secret)) {
    rlang::abort("'secret' must be a single non-NA logical value.")
  }

  run_web_type <- function() {
    el <- resolve_web_target(target, css, xpath)
    rpa_session_check(require_interactive = FALSE)
    loc_js <- build_locate_js(el$by, el$selector, el$index)
    focus_js <- sprintf(
      "(function(){var el=%s;if(!el)return false;el.scrollIntoView({block:'center',inline:'center',behavior:'instant'});el.focus();%sreturn true;})()",
      loc_js,
      if (isTRUE(clear)) {
        "if('value' in el){el.value='';el.dispatchEvent(new Event('input',{bubbles:true,composed:true}));}"
      } else {
        ""
      }
    )
    ok <- cdp_eval_raw(el$session, focus_js)$value
    if (!isTRUE(ok)) {
      rlang::abort("Target web element could not be focused.")
    }
    cdp_call(
      el$session,
      "Input.insertText",
      sprintf('{"text":%s}', cdp_json_str(text))
    )
  }

  if (isTRUE(secret)) {
    tryCatch(
      run_web_type(),
      error = function(e) {
        rlang::abort("Failed to input secret text into web element.")
      }
    )
  } else {
    run_web_type()
  }
  invisible(target)
}

#' @rdname rpa_web_click
#' @export
rpa_web_text <- function(target, css = NULL, xpath = NULL, attr = NULL) {
  if (!is.null(attr) && (!is.character(attr) || length(attr) != 1L || is.na(attr) || !nzchar(attr))) {
    rlang::abort("'attr' must be NULL or a single non-empty character string.")
  }
  el <- resolve_web_target(target, css, xpath)
  rpa_session_check(require_interactive = FALSE)
  loc_js <- build_locate_js(el$by, el$selector, el$index)
  read_js <- if (is.null(attr)) {
    sprintf(
      "(function(){var el=%s;if(!el)return null;return (el.innerText !== undefined && el.innerText.length > 0) ? el.innerText : (el.textContent || '');})()",
      loc_js
    )
  } else {
    sprintf(
      "(function(){var el=%s;if(!el)return null;return el.getAttribute(%s);})()",
      loc_js,
      cdp_json_str(attr)
    )
  }
  val <- cdp_eval_raw(el$session, read_js)$value
  if (is.null(val)) NULL else as.character(val)
}

#' @rdname rpa_web_click
#' @export
rpa_web_eval <- function(session, expression) {
  assert_web_session(session)
  if (!is.character(expression) || length(expression) != 1L || is.na(expression)) {
    rlang::abort("'expression' must be a single non-NA character string.")
  }
  rpa_session_check(require_interactive = FALSE)
  res <- cdp_eval_raw(session, expression, return_by_value = TRUE, await_promise = TRUE)
  res$value
}

#' @rdname rpa_web_click
#' @export
rpa_web_screenshot <- function(session,
                               path = tempfile(pattern = "winrpa_web_", fileext = ".png"),
                               full_page = FALSE) {
  assert_web_session(session)
  if (!is.character(path) || length(path) != 1L || is.na(path) || !nzchar(path)) {
    rlang::abort("'path' must be a non-empty file path string.")
  }
  if (!is.logical(full_page) || length(full_page) != 1L || is.na(full_page)) {
    rlang::abort("'full_page' must be a single non-NA logical value.")
  }
  rpa_session_check(require_interactive = FALSE)
  params <- if (isTRUE(full_page)) {
    '{"format":"png","captureBeyondViewport":true,"fromSurface":true}'
  } else {
    '{"format":"png"}'
  }
  res <- cdp_call(session, "Page.captureScreenshot", params, timeout_ms = 20000L)
  if (!is.character(res$data) || !nzchar(res$data)) {
    rlang::abort("CDP Page.captureScreenshot returned no image data.")
  }
  cpp_cdp_write_base64_file(res$data, path)
}

#' @rdname rpa_web_click
#' @export
rpa_web_set_files <- function(target, files, css = NULL, xpath = NULL) {
  if (!is.character(files) || length(files) == 0L || anyNA(files) || any(!nzchar(files))) {
    rlang::abort("'files' must be a non-empty character vector of file paths.")
  }
  missing_files <- files[!file.exists(files)]
  if (length(missing_files) > 0L) {
    rlang::abort(sprintf("File(s) do not exist: %s", paste(missing_files, collapse = ", ")))
  }
  el <- resolve_web_target(target, css, xpath)
  norm_files <- normalizePath(files, winslash = "\\", mustWork = TRUE)

  rpa_session_check(require_interactive = FALSE)
  cdp_call(el$session, "DOM.getDocument", '{"depth":0}')
  loc_js <- build_locate_js(el$by, el$selector, el$index)
  eval_res <- cdp_eval_raw(el$session, loc_js, return_by_value = FALSE)
  obj_id <- eval_res$objectId
  if (!is.character(obj_id) || !nzchar(obj_id)) {
    rlang::abort(sprintf("Web file input (%s=\"%s\") was not found in the DOM.", el$by, el$selector))
  }
  node_res <- cdp_call(
    el$session,
    "DOM.requestNode",
    sprintf('{"objectId":%s}', cdp_json_str(obj_id))
  )
  node_id <- as.integer(node_res$nodeId)
  if (is.na(node_id) || node_id <= 0L) {
    rlang::abort("Failed to resolve DOM nodeId for file input element.")
  }
  files_json <- paste0("[", paste(vapply(norm_files, cdp_json_str, character(1L)), collapse = ","), "]")
  cdp_call(
    el$session,
    "DOM.setFileInputFiles",
    sprintf('{"files":%s,"nodeId":%d}', files_json, node_id)
  )
  invisible(target)
}

#' @rdname rpa_web_click
#' @export
rpa_web_window <- function(session) {
  assert_web_session(session)
  if (isTRUE(session$headless)) {
    rlang::abort("Headless CDP browser sessions do not expose a visible desktop UIA window.")
  }
  rpa_session_check(require_interactive = TRUE)

  win <- NULL
  if (!is.null(session$pid) && session$pid > 0L) {
    win <- tryCatch(rpa_window(pid = session$pid), error = function(e) NULL)
  }
  if (is.null(win)) {
    page_title <- tryCatch(rpa_web_eval(session, "document.title"), error = function(e) NULL)
    if (is.character(page_title) && length(page_title) == 1L && nzchar(page_title)) {
      win <- tryCatch(rpa_window(title = page_title), error = function(e) NULL)
    }
  }
  if (is.null(win)) {
    win <- rpa_window(class_name = "Chrome_WidgetWin_1")
  }
  win
}
