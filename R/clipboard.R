#' Read, Write, and Transactionally Restore the Windows Clipboard
#'
#' Provides retry-safe clipboard text access and a transactional
#' `rpa_with_clipboard()` wrapper that snapshots and restores prior clipboard
#' contents even when an error occurs.
#'
#' @param text Character scalar to write to the clipboard.
#' @param expr R expression to evaluate while the temporary clipboard is active.
#'
#' @return `rpa_clipboard_get()` returns a character scalar or `NULL`;
#'   `rpa_clipboard_set()` invisibly returns `NULL`;
#'   `rpa_clipboard_sequence()` returns an integer sequence number;
#'   `rpa_with_clipboard()` returns the value of `expr`.
#' @export
#' @examplesIf interactive() && .Platform$OS.type == "windows"
#' rpa_clipboard_sequence()
rpa_clipboard_get <- function() {
  rpa_session_check()
  cpp_clipboard_get_text()
}

#' @rdname rpa_clipboard_get
#' @export
rpa_clipboard_set <- function(text) {
  if (!is.character(text) || length(text) != 1L || is.na(text)) {
    rlang::abort("'text' must be a single non-NA character string.")
  }
  rpa_session_check()
  cpp_clipboard_set_text(text)
  invisible(NULL)
}

#' @rdname rpa_clipboard_get
#' @export
rpa_clipboard_sequence <- function() {
  rpa_session_check()
  cpp_clipboard_sequence_number()
}

#' @rdname rpa_clipboard_get
#' @export
rpa_with_clipboard <- function(expr, text = NULL) {
  rpa_session_check()
  old_text <- rpa_clipboard_get()
  on.exit(
    if (!is.null(old_text)) {
      try(cpp_clipboard_set_text(old_text), silent = TRUE)
    },
    add = TRUE
  )
  if (!is.null(text)) {
    rpa_clipboard_set(text)
  }
  force(expr)
}

#' Wait Until the Windows Clipboard Updates
#'
#' Polls the clipboard sequence counter (or non-empty text) until an update
#' occurs or `timeout` seconds elapse.
#'
#' @param timeout Numeric timeout in seconds.
#' @param interval Polling interval in seconds.
#' @param prev_seq Optional integer sequence number captured via
#'   `rpa_clipboard_sequence()` before triggering a copy action.
#'
#' @return Character scalar containing the clipboard text.
#' @export
#' @examplesIf interactive() && .Platform$OS.type == "windows"
#' seq_id <- rpa_clipboard_sequence()
#' seq_id
rpa_wait_for_clipboard <- function(timeout = 5, interval = 0.05, prev_seq = NULL) {
  rpa_session_check()
  baseline <- if (is.null(prev_seq)) rpa_clipboard_sequence() else as.integer(prev_seq)
  rpa_wait_for(
    function() {
      cur_seq <- rpa_clipboard_sequence()
      if (cur_seq != baseline) {
        txt <- rpa_clipboard_get()
        if (!is.null(txt)) {
          return(txt)
        }
      }
      NULL
    },
    timeout = timeout,
    interval = interval,
    label = "clipboard update"
  )
}
