#' Click a UI Automation Element
#'
#' Activates a control via `InvokePattern` when available in `"auto"` or
#' `"invoke"` mode, or falls back to mouse click injection at the control's
#' bounding-rectangle center.
#'
#' @param target A `winrpa_element` object.
#' @param button Mouse button to use when clicking (`"left"`, `"right"`, or
#'   `"middle"`).
#' @param clicks Integer click count (`>= 1`).
#' @param method Click dispatch strategy: `"auto"`, `"invoke"`, or `"mouse"`.
#'
#' @return Invisibly returns `target` for pipe chaining.
#' @export
#' @examplesIf interactive() && .Platform$OS.type == "windows"
#' wins <- rpa_windows()
#' nrow(wins)
rpa_click <- function(target,
                      button = c("left", "right", "middle"),
                      clicks = 1L,
                      method = c("auto", "invoke", "mouse")) {
  assert_element(target)
  button <- match.arg(button)
  method <- match.arg(method)
  if (!is.numeric(clicks) || length(clicks) != 1L || is.na(clicks) || clicks < 1) {
    rlang::abort("'clicks' must be a positive integer.")
  }
  clicks_int <- as.integer(clicks)
  rpa_session_check()

  if (method == "invoke") {
    cpp_uia_invoke(target$ptr)
    return(invisible(target))
  }

  info <- cpp_uia_element_info(target$ptr)
  if (method == "auto" && button == "left" && clicks_int == 1L &&
      "Invoke" %in% info$patterns) {
    invoked <- tryCatch(
      {
        cpp_uia_invoke(target$ptr)
        TRUE
      },
      error = function(e) FALSE
    )
    if (invoked) {
      return(invisible(target))
    }
  }

  if (info$right <= info$left || info$bottom <= info$top) {
    rlang::abort("Target element has an empty bounding rectangle and cannot be clicked.")
  }
  try(cpp_uia_scroll_into_view(target$ptr), silent = TRUE)
  cx <- as.integer((info$left + info$right) %/% 2L)
  cy <- as.integer((info$top + info$bottom) %/% 2L)
  cpp_mouse_click(cx, cy, button, clicks_int, TRUE)
  invisible(target)
}

#' Type Text Into a UI Element
#'
#' Sets control text via UI Automation `ValuePattern` or sends keyboard input
#' using Unicode or hardware scan codes. Secret text is never echoed in error
#' messages.
#'
#' @param target A `winrpa_element` object.
#' @param text Character scalar to type.
#' @param mode Input strategy: `"auto"`, `"value"`, `"unicode"`, or `"scancode"`.
#' @param delay_ms Non-negative integer delay in milliseconds between
#'   keystrokes.
#' @param secret Logical scalar; when `TRUE`, masks text from any error
#'   diagnostics.
#'
#' @return Invisibly returns `target` for pipe chaining.
#' @export
#' @examplesIf interactive() && .Platform$OS.type == "windows"
#' wins <- rpa_windows()
#' nrow(wins)
rpa_type <- function(target,
                     text,
                     mode = c("auto", "value", "unicode", "scancode"),
                     delay_ms = 0L,
                     secret = FALSE) {
  assert_element(target)
  if (!is.character(text) || length(text) != 1L || is.na(text)) {
    rlang::abort("'text' must be a single non-NA character string.")
  }
  mode <- match.arg(mode)
  if (!is.numeric(delay_ms) || length(delay_ms) != 1L || is.na(delay_ms) || delay_ms < 0) {
    rlang::abort("'delay_ms' must be a non-negative integer.")
  }
  if (!is.logical(secret) || length(secret) != 1L || is.na(secret)) {
    rlang::abort("'secret' must be a single non-NA logical value.")
  }

  rpa_session_check()

  run_type <- function() {
    if (mode == "value") {
      cpp_uia_set_value(target$ptr, text)
      return(invisible(target))
    }
    if (mode == "auto") {
      info <- cpp_uia_element_info(target$ptr)
      if ("Value" %in% info$patterns) {
        used_value <- tryCatch(
          {
            cpp_uia_set_value(target$ptr, text)
            TRUE
          },
          error = function(e) FALSE
        )
        if (used_value) {
          return(invisible(target))
        }
      }
      cpp_uia_set_focus(target$ptr)
      cpp_type_text(text, "unicode", as.integer(delay_ms))
      return(invisible(target))
    }
    cpp_uia_set_focus(target$ptr)
    cpp_type_text(text, mode, as.integer(delay_ms))
    invisible(target)
  }

  if (isTRUE(secret)) {
    tryCatch(
      run_type(),
      error = function(e) {
        rlang::abort("Failed to input secret text into target element.")
      }
    )
  } else {
    run_type()
  }
  invisible(target)
}

#' Send a Keyboard Modifier Chord
#'
#' Presses a chord of keys (for example `c("ctrl", "s")`) while guarding all
#' modifier keys with an RAII cleanup scope so modifiers release even if an
#' error or interrupt occurs.
#'
#' @param keys Non-empty character vector of key names.
#' @param use_scancode Logical scalar; if `TRUE`, emit hardware scan codes.
#'
#' @return Invisibly returns `NULL`.
#' @export
#' @examplesIf interactive() && .Platform$OS.type == "windows"
#' rpa_is_interactive_desktop()
rpa_key_chord <- function(keys, use_scancode = FALSE) {
  if (!is.character(keys) || length(keys) == 0L || anyNA(keys) || any(!nzchar(keys))) {
    rlang::abort("'keys' must be a non-empty character vector of non-empty key names.")
  }
  if (!is.logical(use_scancode) || length(use_scancode) != 1L || is.na(use_scancode)) {
    rlang::abort("'use_scancode' must be a single non-NA logical value.")
  }
  rpa_session_check()
  cpp_key_chord(keys, isTRUE(use_scancode))
  invisible(NULL)
}

#' Read or Write a Control's Value Pattern
#'
#' @param target A `winrpa_element` object.
#' @param value Character scalar to assign via `ValuePattern`.
#' @param secret Logical scalar; when `TRUE`, masks `value` from any error
#'   messages.
#'
#' @return `rpa_get_value()` returns a character scalar; `rpa_set_value()`
#'   invisibly returns `target`.
#' @export
#' @examplesIf interactive() && .Platform$OS.type == "windows"
#' wins <- rpa_windows()
#' nrow(wins)
rpa_get_value <- function(target) {
  assert_element(target)
  rpa_session_check()
  cpp_uia_get_value(target$ptr)
}

#' @rdname rpa_get_value
#' @export
rpa_set_value <- function(target, value, secret = FALSE) {
  assert_element(target)
  if (!is.character(value) || length(value) != 1L || is.na(value)) {
    rlang::abort("'value' must be a single non-NA character string.")
  }
  if (!is.logical(secret) || length(secret) != 1L || is.na(secret)) {
    rlang::abort("'secret' must be a single non-NA logical value.")
  }
  rpa_session_check()
  if (isTRUE(secret)) {
    tryCatch(
      cpp_uia_set_value(target$ptr, value),
      error = function(e) {
        rlang::abort("Failed to assign secret value on target element.")
      }
    )
  } else {
    cpp_uia_set_value(target$ptr, value)
  }
  invisible(target)
}

#' Toggle, Select, Expand, Collapse, or Scroll a UI Element
#'
#' @param target A `winrpa_element` object.
#'
#' @return `rpa_toggle()` returns the updated toggle state (`"off"`, `"on"`, or
#'   `"indeterminate"`); the other actions invisibly return `target`.
#' @export
#' @examplesIf interactive() && .Platform$OS.type == "windows"
#' wins <- rpa_windows()
#' nrow(wins)
rpa_toggle <- function(target) {
  assert_element(target)
  rpa_session_check()
  cpp_uia_toggle(target$ptr)
}

#' @rdname rpa_toggle
#' @export
rpa_select <- function(target) {
  assert_element(target)
  rpa_session_check()
  cpp_uia_select(target$ptr)
  invisible(target)
}

#' @rdname rpa_toggle
#' @export
rpa_expand <- function(target) {
  assert_element(target)
  rpa_session_check()
  cpp_uia_expand_collapse(target$ptr, TRUE)
  invisible(target)
}

#' @rdname rpa_toggle
#' @export
rpa_collapse <- function(target) {
  assert_element(target)
  rpa_session_check()
  cpp_uia_expand_collapse(target$ptr, FALSE)
  invisible(target)
}

#' @rdname rpa_toggle
#' @export
rpa_scroll_into_view <- function(target) {
  assert_element(target)
  rpa_session_check()
  cpp_uia_scroll_into_view(target$ptr)
  invisible(target)
}

#' Extract a UI Automation Grid or Table as a Data Frame
#'
#' @param target A `winrpa_element` supporting `GridPattern` (and optionally
#'   `TablePattern` for column headers).
#'
#' @return A `data.frame` containing the table cells as character columns.
#' @export
#' @examplesIf interactive() && .Platform$OS.type == "windows"
#' wins <- rpa_windows()
#' nrow(wins)
rpa_read_table <- function(target) {
  assert_element(target)
  rpa_session_check()
  raw_tbl <- cpp_uia_read_table(target$ptr)
  nrow_n <- as.integer(raw_tbl$nrow)
  ncol_n <- as.integer(raw_tbl$ncol)
  if (ncol_n <= 0L) {
    return(data.frame(stringsAsFactors = FALSE))
  }

  col_names <- raw_tbl$headers
  if (length(col_names) != ncol_n) {
    col_names <- paste0("V", seq_len(ncol_n))
  } else {
    empty_idx <- !nzchar(col_names)
    col_names[empty_idx] <- paste0("V", which(empty_idx))
  }
  col_names <- make.unique(col_names)

  if (nrow_n <= 0L) {
    cols <- stats::setNames(
      replicate(ncol_n, character(0), simplify = FALSE),
      col_names
    )
    return(as.data.frame(cols, stringsAsFactors = FALSE))
  }

  mat <- matrix(raw_tbl$cells, nrow = nrow_n, ncol = ncol_n, byrow = TRUE)
  cols <- lapply(seq_len(ncol_n), function(j) mat[, j])
  names(cols) <- col_names
  as.data.frame(cols, stringsAsFactors = FALSE)
}

#' Move, Click, Scroll, or Query the Desktop Mouse Cursor
#'
#' Performs VirtualDesk-aware mouse movement and clicks across multi-monitor
#' setups, including monitors with negative coordinates.
#'
#' @param x,y Integer screen pixel coordinates. For `rpa_mouse_click()`, omit
#'   both to click at the current cursor location.
#' @param button Mouse button (`"left"`, `"right"`, or `"middle"`).
#' @param clicks Positive integer click count.
#' @param delta Wheel scroll delta (typically multiples of `120` or `-120`).
#' @param horizontal Logical scalar; if `TRUE`, emit horizontal wheel scroll.
#'
#' @return `rpa_mouse_position()` returns a named integer vector `c(x, y)`;
#'   `rpa_mouse_move()`, `rpa_mouse_click()`, and `rpa_mouse_scroll()` invisibly
#'   return `NULL`.
#' @export
#' @examplesIf interactive() && .Platform$OS.type == "windows"
#' rpa_mouse_position()
rpa_mouse_move <- function(x, y) {
  if (!is.numeric(x) || length(x) != 1L || is.na(x) ||
      !is.numeric(y) || length(y) != 1L || is.na(y)) {
    rlang::abort("'x' and 'y' must be single numeric coordinates.")
  }
  rpa_session_check()
  cpp_mouse_move(as.integer(x), as.integer(y))
  invisible(NULL)
}

#' @rdname rpa_mouse_move
#' @export
rpa_mouse_click <- function(x = NULL,
                            y = NULL,
                            button = c("left", "right", "middle"),
                            clicks = 1L) {
  button <- match.arg(button)
  if (!is.numeric(clicks) || length(clicks) != 1L || is.na(clicks) || clicks < 1) {
    rlang::abort("'clicks' must be a positive integer.")
  }
  has_x <- !is.null(x)
  has_y <- !is.null(y)
  if (has_x != has_y) {
    rlang::abort("'x' and 'y' must both be provided or both be NULL.")
  }
  if (has_x && (!is.numeric(x) || length(x) != 1L || is.na(x) ||
                !is.numeric(y) || length(y) != 1L || is.na(y))) {
    rlang::abort("'x' and 'y' must be single numeric coordinates.")
  }

  rpa_session_check()
  cpp_mouse_click(
    if (has_x) as.integer(x) else 0L,
    if (has_y) as.integer(y) else 0L,
    button,
    as.integer(clicks),
    has_x
  )
  invisible(NULL)
}

#' @rdname rpa_mouse_move
#' @export
rpa_mouse_scroll <- function(delta = -120L, horizontal = FALSE) {
  if (!is.numeric(delta) || length(delta) != 1L || is.na(delta)) {
    rlang::abort("'delta' must be a single numeric value.")
  }
  if (!is.logical(horizontal) || length(horizontal) != 1L || is.na(horizontal)) {
    rlang::abort("'horizontal' must be a single non-NA logical value.")
  }
  rpa_session_check()
  cpp_mouse_scroll(as.integer(delta), isTRUE(horizontal))
  invisible(NULL)
}

#' @rdname rpa_mouse_move
#' @export
rpa_mouse_position <- function() {
  rpa_session_check()
  stats::setNames(cpp_mouse_position(), c("x", "y"))
}

#' Inspect Display Geometry and DPI Awareness
#'
#' Queries virtual screen dimensions, monitor bounds, per-monitor effective DPI,
#' and current thread DPI awareness. `rpa_try_enable_dpi_awareness()` attempts to
#' opt into Per-Monitor V2 awareness while handling `E_ACCESSDENIED` gracefully
#' when the host application manifest already locked process DPI mode.
#'
#' @return A named list with display geometry or DPI update status.
#' @export
#' @examplesIf interactive() && .Platform$OS.type == "windows"
#' m <- rpa_display_metrics()
#' m$dpi_awareness
rpa_display_metrics <- function() {
  rpa_session_check()
  cpp_get_display_metrics()
}

#' @rdname rpa_display_metrics
#' @export
rpa_try_enable_dpi_awareness <- function() {
  rpa_session_check()
  cpp_try_enable_per_monitor_dpi()
}

#' Manage the Low-Level Keyboard Hook Ring Buffer
#'
#' Starts or stops a dedicated message-loop `WH_KEYBOARD_LL` hook that pushes
#' keystrokes into a lock-free ring buffer, and drains buffered events into R.
#'
#' @return `rpa_hook_start()` and `rpa_hook_stop()` return a logical scalar;
#'   `rpa_hook_drain()` returns a `data.frame` of captured key events.
#' @export
#' @examplesIf interactive() && .Platform$OS.type == "windows"
#' rpa_hook_start()
#' rpa_hook_drain()
#' rpa_hook_stop()
rpa_hook_start <- function() {
  rpa_session_check()
  cpp_hook_start()
}

#' @rdname rpa_hook_start
#' @export
rpa_hook_stop <- function() {
  cpp_hook_stop()
}

#' @rdname rpa_hook_start
#' @export
rpa_hook_drain <- function() {
  rpa_session_check()
  cpp_hook_drain()
}
