#' Capture a Window or Virtual Desktop Screenshot as a 24-bit BMP
#'
#' Captures the target window using `PrintWindow(..., PW_RENDERFULLCONTENT)`
#' (with fallback to `BitBlt`), or the entire virtual desktop when
#' `target = NULL`.
#'
#' @param target `NULL` (full virtual desktop), a `winrpa_element`, or a
#'   hexadecimal `hwnd` string (such as `"0x00010203"`).
#' @param path Output `.bmp` file path. Defaults to a temporary file in
#'   `tempdir()`.
#'
#' @return Character scalar path to the written BMP file.
#' @export
#' @examplesIf interactive() && .Platform$OS.type == "windows"
#' tmp <- tempfile(fileext = ".bmp")
#' on.exit(unlink(tmp), add = TRUE)
#' rpa_screenshot(path = tmp)
rpa_screenshot <- function(target = NULL,
                           path = tempfile(pattern = "winrpa_", fileext = ".bmp")) {
  if (!is.character(path) || length(path) != 1L || is.na(path) || !nzchar(path)) {
    rlang::abort("'path' must be a non-empty file path string.")
  }
  rpa_session_check()

  hwnd_hex <- ""
  if (!is.null(target)) {
    if (inherits(target, "winrpa_element")) {
      info <- cpp_uia_element_info(target$ptr)
      if (!nzchar(info$hwnd)) {
        rlang::abort("Target winrpa_element does not expose a native HWND.")
      }
      hwnd_hex <- info$hwnd
    } else if (is.character(target) && length(target) == 1L && !is.na(target) && nzchar(target)) {
      hwnd_hex <- target
    } else {
      rlang::abort("'target' must be NULL, a winrpa_element, or a hex hwnd string.")
    }
  }

  cpp_capture_window_bmp(hwnd_hex, path)
}

dump_debug_artifacts <- function(debug_dir, label, debug_window = NULL) {
  tryCatch(
    {
      dir.create(debug_dir, recursive = TRUE, showWarnings = FALSE)
      stamp <- format(Sys.time(), "%Y%m%d_%H%M%S")
      base_name <- sprintf("winrpa_timeout_%s", stamp)
      log_path <- file.path(debug_dir, paste0(base_name, ".log"))
      lines <- c(
        sprintf("timestamp: %s", format(Sys.time(), "%Y-%m-%dT%H:%M:%S")),
        sprintf("label: %s", label),
        sprintf("abort_triggered: %s", as.character(rpa_abort_triggered()))
      )
      if (!is.null(debug_window) && .Platform$OS.type == "windows" &&
          rpa_is_interactive_desktop() && !rpa_abort_triggered()) {
        if (inherits(debug_window, "winrpa_element")) {
          tree <- tryCatch(
            utils::head(rpa_inspect_tree(debug_window, max_depth = 3L, max_nodes = 80L), 80L),
            error = function(e) NULL
          )
          if (!is.null(tree)) {
            lines <- c(lines, "", "pruned_uia_tree:", utils::capture.output(print(tree)))
          }
        }
        bmp_path <- file.path(debug_dir, paste0(base_name, ".bmp"))
        cap <- tryCatch(rpa_screenshot(debug_window, path = bmp_path), error = function(e) NULL)
        if (!is.null(cap)) {
          lines <- c(lines, sprintf("screenshot: %s", cap))
        }
      }
      writeLines(lines, con = log_path, useBytes = TRUE)
    },
    error = function(e) NULL
  )
}

#' Wait Deterministically for a Condition or UI Element
#'
#' Polls `fn()` at short interruptible intervals while checking the emergency
#' abort flag. When `options(winrpa.debug_dir = ...)` is set, writes failure
#' diagnostics on timeout.
#'
#' @param fn Zero-argument function returning a truthy value when satisfied, or
#'   `NULL`/`FALSE` while still waiting.
#' @param timeout Numeric maximum wait duration in seconds.
#' @param interval Polling interval in seconds.
#' @param label Short human-readable condition label for error messages.
#' @param debug_window Optional `winrpa_element` or `hwnd` captured into
#'   `winrpa.debug_dir` on timeout.
#'
#' @return The first truthy value returned by `fn()`.
#' @export
#' @examples
#' rpa_wait_for(function() "ready", timeout = 0.2, interval = 0.01)
rpa_wait_for <- function(fn,
                         timeout = 5,
                         interval = 0.05,
                         label = "condition",
                         debug_window = NULL) {
  if (!is.function(fn)) {
    rlang::abort("'fn' must be a zero-argument function.")
  }
  if (!is.numeric(timeout) || length(timeout) != 1L || is.na(timeout) || timeout < 0) {
    rlang::abort("'timeout' must be a non-negative number.")
  }
  if (!is.numeric(interval) || length(interval) != 1L || is.na(interval) || interval <= 0) {
    rlang::abort("'interval' must be a positive number.")
  }
  if (!is.character(label) || length(label) != 1L || is.na(label)) {
    rlang::abort("'label' must be a single non-NA character string.")
  }

  deadline <- proc.time()[["elapsed"]] + timeout
  repeat {
    if (rpa_abort_triggered()) {
      rlang::abort(sprintf("Emergency abort triggered while waiting for %s.", label))
    }
    val <- fn()
    is_truthy <- !is.null(val) && !isFALSE(val) &&
      !(is.logical(val) && length(val) == 1L && is.na(val))
    if (is_truthy) {
      return(val)
    }
    remaining <- deadline - proc.time()[["elapsed"]]
    if (remaining <= 0) {
      debug_dir <- getOption("winrpa.debug_dir")
      if (is.character(debug_dir) && length(debug_dir) == 1L && nzchar(debug_dir)) {
        dump_debug_artifacts(debug_dir, label, debug_window)
      }
      rlang::abort(sprintf("Timed out after %.2fs waiting for %s.", timeout, label))
    }
    Sys.sleep(min(interval, 0.05, remaining))
  }
}

#' Wait Until a Condition Returns `NULL` or `FALSE`
#'
#' @param fn Zero-argument probe function returning `NULL` or `FALSE` once the
#'   target has disappeared.
#' @param timeout,interval,label,debug_window Passed to [rpa_wait_for()].
#'
#' @return Invisibly returns `TRUE`.
#' @export
#' @examples
#' rpa_wait_until_gone(function() FALSE, timeout = 0.2, interval = 0.01)
rpa_wait_until_gone <- function(fn,
                                timeout = 5,
                                interval = 0.05,
                                label = "element to disappear",
                                debug_window = NULL) {
  if (!is.function(fn)) {
    rlang::abort("'fn' must be a zero-argument function.")
  }
  rpa_wait_for(
    function() {
      val <- fn()
      if (is.null(val) || isFALSE(val)) TRUE else NULL
    },
    timeout = timeout,
    interval = interval,
    label = label,
    debug_window = debug_window
  )
  invisible(TRUE)
}

#' Wait Until a UI Automation Element Becomes Enabled
#'
#' @param target A `winrpa_element` object.
#' @param timeout,interval,debug_window Passed to [rpa_wait_for()].
#'
#' @return Invisibly returns `target`.
#' @export
#' @examplesIf interactive() && .Platform$OS.type == "windows"
#' wins <- rpa_windows()
#' if (nrow(wins) > 0L) {
#'   w <- rpa_window(pid = wins$pid[1L])
#'   rpa_wait_until_enabled(w, timeout = 1)
#' }
rpa_wait_until_enabled <- function(target,
                                   timeout = 5,
                                   interval = 0.05,
                                   debug_window = NULL) {
  assert_element(target)
  rpa_session_check()
  rpa_wait_for(
    function() {
      info <- tryCatch(cpp_uia_element_info(target$ptr), error = function(e) NULL)
      if (!is.null(info) && isTRUE(info$enabled)) TRUE else NULL
    },
    timeout = timeout,
    interval = interval,
    label = "element to become enabled",
    debug_window = if (is.null(debug_window)) target else debug_window
  )
  invisible(target)
}

#' Wait for a Top-Level Window or Child Control to Appear
#'
#' Convenience wrappers around [rpa_wait_for()] that poll [rpa_window()] or
#' [rpa_find()] while swallowing expected not-found errors until `timeout`
#' expires. Useful in replayed and recorded scripts where targets may open
#' asynchronously.
#'
#' @param title,class_name,pid,regex Window criteria passed to [rpa_window()].
#' @param parent,selector,name,automation_id,control_type,exact,max_depth
#'   Element search criteria passed to [rpa_find()].
#' @param timeout Numeric maximum wait duration in seconds.
#' @param interval Polling interval in seconds.
#'
#' @return A `"winrpa_element"` object once located.
#' @export
#' @examplesIf interactive() && .Platform$OS.type == "windows"
#' wins <- rpa_windows()
#' if (nrow(wins) > 0L) {
#'   rpa_wait_window(pid = wins$pid[1L], timeout = 2)
#' }
rpa_wait_window <- function(title = NULL,
                            class_name = NULL,
                            pid = NULL,
                            regex = FALSE,
                            timeout = 10,
                            interval = 0.1) {
  title <- assert_optional_scalar_string(title, "title")
  class_name <- assert_optional_scalar_string(class_name, "class_name")
  if (!is.null(pid) &&
      (!is.numeric(pid) || length(pid) != 1L || is.na(pid) || pid <= 0)) {
    rlang::abort("'pid' must be NULL or a positive integer.")
  }
  if (!is.logical(regex) || length(regex) != 1L || is.na(regex)) {
    rlang::abort("'regex' must be a single non-NA logical value.")
  }
  rpa_session_check()

  label <- if (!is.null(title) && nzchar(title)) {
    sprintf('window "%s"', title)
  } else if (!is.null(class_name) && nzchar(class_name)) {
    sprintf('window class "%s"', class_name)
  } else {
    "matching window"
  }

  rpa_wait_for(
    function() {
      tryCatch(
        rpa_window(
          title = title,
          class_name = class_name,
          pid = pid,
          regex = regex
        ),
        error = function(e) NULL
      )
    },
    timeout = timeout,
    interval = interval,
    label = label
  )
}

#' @rdname rpa_wait_window
#' @export
rpa_wait_find <- function(parent = NULL,
                          selector = NULL,
                          name = NULL,
                          automation_id = NULL,
                          control_type = NULL,
                          class_name = NULL,
                          regex = FALSE,
                          exact = FALSE,
                          max_depth = NULL,
                          timeout = 10,
                          interval = 0.1) {
  if (inherits(parent, "winrpa_selector") && is.null(selector)) {
    selector <- parent
    parent <- NULL
  }
  if (!is.null(parent)) {
    assert_element(parent, "parent")
  }
  if (!is.null(max_depth)) {
    if (!is.numeric(max_depth) || length(max_depth) != 1L || is.na(max_depth) ||
        !is.finite(max_depth) || max_depth < 0) {
      rlang::abort("'max_depth' must be NULL or a non-negative integer.")
    }
  }
  sel <- resolve_find_criteria(
    selector, name, automation_id, control_type, class_name, regex, exact
  )
  rpa_session_check()

  rpa_wait_for(
    function() {
      tryCatch(
        rpa_find(
          parent = parent,
          selector = sel,
          max_depth = max_depth
        ),
        error = function(e) NULL
      )
    },
    timeout = timeout,
    interval = interval,
    label = format(sel),
    debug_window = parent
  )
}
