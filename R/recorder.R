parse_stop_hotkey_spec <- function(stop_hotkey) {
  if (is.null(stop_hotkey) || length(stop_hotkey) == 0L) {
    return(list(enabled = FALSE, mods = 0L, vk = 0L, display = "none"))
  }
  if (!is.character(stop_hotkey) || anyNA(stop_hotkey) || any(!nzchar(trimws(stop_hotkey)))) {
    rlang::abort("'stop_hotkey' must be NULL or a character vector like c('ctrl', 'shift', 'f9').")
  }
  parts <- tolower(trimws(stop_hotkey))
  if (length(parts) < 2L) {
    rlang::abort("'stop_hotkey' must include at least one modifier and a trigger key (e.g. c('ctrl', 'shift', 'f9')).")
  }
  mod_map <- c(alt = 1L, ctrl = 2L, control = 2L, shift = 4L, win = 8L)
  mods <- parts[seq_len(length(parts) - 1L)]
  unknown <- setdiff(mods, names(mod_map))
  if (length(unknown) > 0L) {
    rlang::abort(sprintf("Unknown stop_hotkey modifier(s): %s.", paste(unknown, collapse = ", ")))
  }
  key <- parts[length(parts)]
  vk <- resolve_hotkey_vk(key)
  list(
    enabled = TRUE,
    mods = as.integer(sum(unique(unname(mod_map[mods])))),
    vk = as.integer(vk),
    display = paste(parts, collapse = "+")
  )
}

r_quote_str <- function(x) {
  s <- as.character(x)
  s <- gsub("\\", "\\\\", s, fixed = TRUE)
  s <- gsub("\"", "\\\"", s, fixed = TRUE)
  s <- gsub("\n", "\\n", s, fixed = TRUE)
  s <- gsub("\r", "\\r", s, fixed = TRUE)
  s <- gsub("\t", "\\t", s, fixed = TRUE)
  paste0("\"", s, "\"")
}

is_stable_automation_id <- function(aid) {
  if (!is.character(aid) || length(aid) != 1L || is.na(aid)) {
    return(FALSE)
  }
  s <- trimws(aid)
  if (!nzchar(s)) {
    return(FALSE)
  }
  if (grepl("^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}$", s)) {
    return(FALSE)
  }
  if (grepl("^[0-9]{5,}$", s)) {
    return(FALSE)
  }
  if (grepl("^(id_)?[0-9a-fA-F]{12,}$", s)) {
    return(FALSE)
  }
  TRUE
}

clean_window_title <- function(title) {
  if (!is.character(title) || length(title) != 1L || is.na(title)) {
    return("")
  }
  sub("^\\*+\\s*", "", trimws(title))
}

escape_regex_literal <- function(s) {
  gsub("([.^$|?*+()\\[\\]{}\\\\])", "\\\\\\1", as.character(s), perl = TRUE)
}

is_shell_flyout_window <- function(wtitle, wclass) {
  cls <- as.character(wclass %||% "")
  ttl <- tolower(clean_window_title(wtitle))
  (cls %in% c("Windows.UI.Core.CoreWindow", "Shell_TrayWnd", "Shell_SecondaryTrayWnd", "XamlExplorerHostIslandWindow")) ||
    (ttl %in% c("start", "search"))
}

is_start_trigger_step <- function(st) {
  if (identical(st$type, "key_chord") && identical(tolower(as.character(st$chord)), "win")) {
    return(TRUE)
  }
  identical(st$type, "click") &&
    (as.character(st$win_class %||% "") %in% c("Shell_TrayWnd", "Shell_SecondaryTrayWnd")) &&
    identical(tolower(as.character(st$el_name %||% "")), "start")
}

is_search_textbox_step <- function(st) {
  identical(as.character(st$el_automation_id %||% ""), "SearchTextBox")
}

pick_desktop_selector <- function(step) {
  aid <- as.character(step$el_automation_id %||% "")
  nm <- as.character(step$el_name %||% "")
  ctype <- as.character(step$el_control_type %||% "")
  cls <- as.character(step$el_class_name %||% "")
  has_ctype <- nzchar(ctype) && !identical(ctype, "Unknown")

  if (is_stable_automation_id(aid)) {
    args <- list(automation_id = aid)
    if (has_ctype) {
      args$control_type <- ctype
    }
    return(list(tier = "automation_id", args = args))
  }
  if (nzchar(nm) && nchar(nm) <= 80L) {
    args <- list(name = nm)
    if (has_ctype) {
      args$control_type <- ctype
    }
    args$exact <- TRUE
    return(list(tier = "name", args = args))
  }
  if (nzchar(cls)) {
    args <- list(class_name = cls)
    if (has_ctype) {
      args$control_type <- ctype
    }
    args$exact <- TRUE
    return(list(tier = "class_name", args = args))
  }
  NULL
}

`%||%` <- function(a, b) {
  if (is.null(a) || length(a) == 0L || (is.character(a) && is.na(a[1L]))) b else a
}

format_named_call_args <- function(named_list) {
  parts <- vapply(names(named_list), function(nm) {
    val <- named_list[[nm]]
    rhs <- if (is.logical(val)) {
      if (isTRUE(val)) "TRUE" else "FALSE"
    } else if (is.integer(val)) {
      sprintf("%dL", val)
    } else if (is.numeric(val)) {
      as.character(val)
    } else {
      r_quote_str(val)
    }
    sprintf("%s = %s", nm, rhs)
  }, character(1L))
  paste(parts, collapse = ", ")
}

is_modifier_row <- function(vk, key_name = "") {
  if (!is.na(vk) && vk %in% c(16L, 17L, 18L, 91L, 92L, 160L:165L)) {
    return(TRUE)
  }
  tolower(as.character(key_name %||% "")) %in%
    c("ctrl", "lctrl", "rctrl", "shift", "lshift", "rshift", "alt", "lalt", "ralt", "win", "lwin", "rwin")
}

special_chord_keys <- c(
  "enter", "tab", "esc", "escape", "delete", "insert",
  "home", "end", "pageup", "pagedown",
  "left", "right", "up", "down",
  "capslock", "printscreen",
  paste0("f", 1:24)
)

build_chord_vec <- function(modifiers_mask, key_name) {
  m <- as.integer(modifiers_mask %||% 0L)
  keys <- character()
  if (bitwAnd(m, 1L) != 0L) keys <- c(keys, "ctrl")
  if (bitwAnd(m, 4L) != 0L) keys <- c(keys, "alt")
  if (bitwAnd(m, 2L) != 0L) keys <- c(keys, "shift")
  if (bitwAnd(m, 8L) != 0L) keys <- c(keys, "win")
  k <- tolower(as.character(key_name %||% ""))
  if (nzchar(k)) {
    keys <- c(keys, k)
  }
  unique(keys)
}

win_key_for_row <- function(row) {
  hwnd <- as.character(row$win_hwnd %||% "")
  if (nzchar(hwnd)) {
    return(hwnd)
  }
  paste(
    clean_window_title(row$win_title %||% ""),
    as.character(row$win_class %||% ""),
    as.character(row$win_pid %||% 0L),
    sep = "|"
  )
}

el_key_for_step <- function(step, win_key) {
  paste(
    win_key,
    as.character(step$el_automation_id %||% ""),
    as.character(step$el_name %||% ""),
    as.character(step$el_control_type %||% ""),
    as.character(step$el_class_name %||% ""),
    as.character(step$el_hwnd %||% ""),
    sep = "|"
  )
}

ensure_df_col <- function(df, name, default) {
  if (name %in% names(df)) df[[name]] else rep(default, nrow(df))
}

normalize_desktop_steps <- function(df,
                                    coalesce_typing,
                                    redact_passwords,
                                    include_wheel,
                                    double_click_ms,
                                    drag_px) {
  n <- nrow(df)
  if (n == 0L) {
    return(list())
  }

  kind <- as.character(ensure_df_col(df, "kind", ""))
  ts <- as.numeric(ensure_df_col(df, "timestamp_ms", 0))
  px <- as.integer(ensure_df_col(df, "x", 0L))
  py <- as.integer(ensure_df_col(df, "y", 0L))
  rel_x <- as.integer(ensure_df_col(df, "rel_x", 0L))
  rel_y <- as.integer(ensure_df_col(df, "rel_y", 0L))
  wheel_delta <- as.integer(ensure_df_col(df, "wheel_delta", 0L))
  horizontal <- as.logical(ensure_df_col(df, "horizontal", FALSE))
  button <- as.character(ensure_df_col(df, "button", ""))
  vk_code <- as.integer(ensure_df_col(df, "vk_code", 0L))
  modifiers <- as.integer(ensure_df_col(df, "modifiers", 0L))
  key_name <- as.character(ensure_df_col(df, "key_name", ""))
  unicode_text <- as.character(ensure_df_col(df, "unicode_text", ""))
  win_title <- as.character(ensure_df_col(df, "win_title", ""))
  win_class <- as.character(ensure_df_col(df, "win_class", ""))
  win_hwnd <- as.character(ensure_df_col(df, "win_hwnd", ""))
  win_pid <- as.integer(ensure_df_col(df, "win_pid", 0L))
  win_left <- as.integer(ensure_df_col(df, "win_left", 0L))
  win_right <- as.integer(ensure_df_col(df, "win_right", 0L))
  win_top <- as.integer(ensure_df_col(df, "win_top", 0L))
  win_bottom <- as.integer(ensure_df_col(df, "win_bottom", 0L))
  el_name <- as.character(ensure_df_col(df, "el_name", ""))
  el_aid <- as.character(ensure_df_col(df, "el_automation_id", ""))
  el_ctype <- as.character(ensure_df_col(df, "el_control_type", ""))
  el_class <- as.character(ensure_df_col(df, "el_class_name", ""))
  el_hwnd <- as.character(ensure_df_col(df, "el_hwnd", ""))
  is_pwd <- as.logical(ensure_df_col(df, "is_password", FALSE))
  has_toggle <- as.logical(ensure_df_col(df, "has_toggle", FALSE))
  has_select <- as.logical(ensure_df_col(df, "has_select", FALSE))

  row_snapshot <- function(i) {
    list(
      timestamp_ms = ts[i],
      x = px[i],
      y = py[i],
      rel_x = rel_x[i],
      rel_y = rel_y[i],
      wheel_delta = wheel_delta[i],
      horizontal = isTRUE(horizontal[i]),
      button = button[i],
      vk_code = vk_code[i],
      modifiers = modifiers[i],
      key_name = key_name[i],
      unicode_text = unicode_text[i],
      win_title = win_title[i],
      win_class = win_class[i],
      win_hwnd = win_hwnd[i],
      win_pid = win_pid[i],
      win_has_rect = isTRUE(win_right[i] > win_left[i] && win_bottom[i] > win_top[i]),
      el_name = el_name[i],
      el_automation_id = el_aid[i],
      el_control_type = el_ctype[i],
      el_class_name = el_class[i],
      el_hwnd = el_hwnd[i],
      is_password = isTRUE(is_pwd[i]),
      has_toggle = isTRUE(has_toggle[i]),
      has_select = isTRUE(has_select[i])
    )
  }

  raw_steps <- list()
  pending_down <- list(left = NULL, right = NULL, middle = NULL)

  append_raw <- function(st) {
    raw_steps[[length(raw_steps) + 1L]] <<- st
  }

  for (i in seq_len(n)) {
    k <- kind[i]
    if (k == "mouse_down") {
      btn <- if (button[i] %in% c("left", "right", "middle")) button[i] else "left"
      pending_down[[btn]] <- row_snapshot(i)
    } else if (k == "mouse_up") {
      btn <- if (button[i] %in% c("left", "right", "middle")) button[i] else "left"
      down <- pending_down[[btn]]
      pending_down[[btn]] <- NULL
      if (is.null(down)) {
        next
      }
      dx <- abs(px[i] - down$x)
      dy <- abs(py[i] - down$y)
      if (dx > drag_px || dy > drag_px) {
        st <- down
        st$type <- "drag"
        st$button <- btn
        st$to_x <- px[i]
        st$to_y <- py[i]
        st$to_rel_x <- rel_x[i]
        st$to_rel_y <- rel_y[i]
        st$timestamp_ms <- ts[i]
        append_raw(st)
      } else {
        m_cnt <- length(raw_steps)
        collapsed <- FALSE
        if (m_cnt > 0L) {
          prev <- raw_steps[[m_cnt]]
          dt <- down$timestamp_ms - (prev$last_down_ts %||% prev$timestamp_ms)
          if (identical(prev$type, "click") &&
              identical(prev$button, btn) &&
              dt >= 0 && dt <= double_click_ms &&
              abs(down$x - prev$x) <= drag_px &&
              abs(down$y - prev$y) <= drag_px) {
            prev$clicks <- as.integer(prev$clicks) + 1L
            prev$timestamp_ms <- ts[i]
            prev$last_down_ts <- down$timestamp_ms
            raw_steps[[m_cnt]] <- prev
            collapsed <- TRUE
          }
        }
        if (!collapsed) {
          st <- down
          st$type <- "click"
          st$button <- btn
          st$clicks <- 1L
          st$last_down_ts <- down$timestamp_ms
          st$timestamp_ms <- ts[i]
          append_raw(st)
        }
      }
    } else if (k == "mouse_wheel") {
      if (!isTRUE(include_wheel)) {
        next
      }
      st <- row_snapshot(i)
      wkey <- win_key_for_row(st)
      m_cnt <- length(raw_steps)
      coalesced <- FALSE
      if (m_cnt > 0L) {
        prev <- raw_steps[[m_cnt]]
        if (identical(prev$type, "wheel") &&
            identical(prev$horizontal, st$horizontal) &&
            identical(win_key_for_row(prev), wkey) &&
            (st$timestamp_ms - prev$timestamp_ms) >= 0 &&
            (st$timestamp_ms - prev$timestamp_ms) <= 400) {
          prev$wheel_delta <- as.integer(prev$wheel_delta) + as.integer(st$wheel_delta)
          prev$timestamp_ms <- st$timestamp_ms
          raw_steps[[m_cnt]] <- prev
          coalesced <- TRUE
        }
      }
      if (!coalesced) {
        st$type <- "wheel"
        append_raw(st)
      }
    } else if (k == "key_down") {
      if (is_modifier_row(vk_code[i], key_name[i])) {
        next
      }
      st <- row_snapshot(i)
      m <- as.integer(modifiers[i])
      has_ctrl <- bitwAnd(m, 1L) != 0L
      has_alt <- bitwAnd(m, 4L) != 0L
      has_win <- bitwAnd(m, 8L) != 0L
      has_non_shift <- (has_ctrl && !has_alt) || (has_alt && !has_ctrl) || has_win

      kn <- tolower(key_name[i])
      ut <- unicode_text[i]
      if (!nzchar(ut) && identical(kn, "space") && !has_non_shift) {
        ut <- " "
      }

      if (isTRUE(redact_passwords) && isTRUE(st$is_password)) {
        if (!has_non_shift && !(kn %in% c("enter", "tab", "esc", "escape"))) {
          st$type <- "secret_char"
          st$unicode_text <- ""
          st$key_name <- ""
          append_raw(st)
          next
        }
      }

      if (has_non_shift) {
        chord <- build_chord_vec(m, kn)
        if (length(chord) > 0L) {
          st$type <- "key_chord"
          st$chord <- chord
          append_raw(st)
        }
      } else if (identical(kn, "backspace")) {
        st$type <- "backspace"
        append_raw(st)
      } else if (kn %in% special_chord_keys || (!nzchar(ut) && nzchar(kn))) {
        chord <- build_chord_vec(m, kn)
        if (length(chord) > 0L) {
          st$type <- "key_chord"
          st$chord <- chord
          append_raw(st)
        }
      } else if (nzchar(ut)) {
        st$type <- "char"
        st$text <- ut
        append_raw(st)
      }
    } else if (k == "key_tap") {
      st <- row_snapshot(i)
      kn <- tolower(key_name[i])
      if (nzchar(kn)) {
        st$type <- "key_chord"
        st$chord <- kn
        st$win_title <- ""
        st$win_class <- ""
        st$win_hwnd <- ""
        append_raw(st)
      }
    }
  }

  # Normalize Start/Search handoff targets before typing coalescing so
  # SearchTextBox keystrokes spanning Start -> Search merge into one rpa_type().
  for (idx in seq_along(raw_steps)) {
    if (is_search_textbox_step(raw_steps[[idx]]) &&
        tolower(clean_window_title(raw_steps[[idx]]$win_title)) %in% c("", "start", "search")) {
      raw_steps[[idx]]$win_title <- "Search"
      raw_steps[[idx]]$win_class <- "Windows.UI.Core.CoreWindow"
      raw_steps[[idx]]$win_hwnd <- ""
      raw_steps[[idx]]$win_pid <- 0L
    }
  }

  # Coalesce typing bursts and fold in-burst Backspace keystrokes.
  merged <- list()
  for (st in raw_steps) {
    m_cnt <- length(merged)
    wkey <- win_key_for_row(st)
    ekey <- el_key_for_step(st, wkey)

    if (identical(st$type, "secret_char")) {
      if (m_cnt > 0L && identical(merged[[m_cnt]]$type, "type_secret") &&
          identical(merged[[m_cnt]]$el_key, ekey)) {
        merged[[m_cnt]]$timestamp_ms <- st$timestamp_ms
      } else {
        st$type <- "type_secret"
        st$el_key <- ekey
        merged[[m_cnt + 1L]] <- st
      }
      next
    }

    if (identical(st$type, "char")) {
      if (isTRUE(coalesce_typing) && m_cnt > 0L &&
          identical(merged[[m_cnt]]$type, "type") &&
          identical(merged[[m_cnt]]$el_key, ekey)) {
        merged[[m_cnt]]$text <- paste0(merged[[m_cnt]]$text, st$text)
        merged[[m_cnt]]$timestamp_ms <- st$timestamp_ms
      } else {
        st$type <- "type"
        st$text <- st$text
        st$el_key <- ekey
        merged[[m_cnt + 1L]] <- st
      }
      next
    }

    if (identical(st$type, "backspace")) {
      if (isTRUE(coalesce_typing) && m_cnt > 0L &&
          identical(merged[[m_cnt]]$type, "type") &&
          identical(merged[[m_cnt]]$el_key, ekey)) {
        cur_txt <- merged[[m_cnt]]$text
        cur_len <- nchar(cur_txt)
        if (cur_len > 1L) {
          merged[[m_cnt]]$text <- substr(cur_txt, 1L, cur_len - 1L)
          merged[[m_cnt]]$timestamp_ms <- st$timestamp_ms
        } else {
          merged <- merged[seq_len(m_cnt - 1L)]
        }
      } else if (m_cnt > 0L && identical(merged[[m_cnt]]$type, "type_secret") &&
                 identical(merged[[m_cnt]]$el_key, ekey)) {
        merged[[m_cnt]]$timestamp_ms <- st$timestamp_ms
      } else {
        st$type <- "key_chord"
        st$chord <- "backspace"
        merged[[m_cnt + 1L]] <- st
      }
      next
    }

    merged[[m_cnt + 1L]] <- st
  }

  if (length(merged) >= 2L) {
    for (j in seq_len(length(merged) - 1L)) {
      if (is_start_trigger_step(merged[[j]]) &&
          is_search_textbox_step(merged[[j + 1L]])) {
        merged[[j]]$type <- "key_chord"
        merged[[j]]$chord <- c("win", "s")
        merged[[j]]$win_title <- ""
        merged[[j]]$win_class <- ""
        merged[[j]]$win_hwnd <- ""
      }
    }
  }

  merged
}

#' Format or Print a Recorded `winrpa` Automation Script
#'
#' @param x A `"winrpa_recording"` object returned by [rpa_record_stop()],
#'   [rpa_record_emit()], [rpa_web_record_stop()], or [rpa_web_record_emit()].
#' @param ... Unused additional arguments.
#'
#' @return `format()` returns a character scalar summary; `print()` prints
#'   the summary and generated script lines and invisibly returns `x`.
#' @export
#' @examples
#' rec <- rpa_record_emit(
#'   data.frame(
#'     kind = c("mouse_down", "mouse_up"),
#'     timestamp_ms = c(100, 140),
#'     x = c(200L, 200L),
#'     y = c(150L, 150L),
#'     button = c("left", "left"),
#'     win_title = c("Untitled - Notepad", "Untitled - Notepad"),
#'     el_automation_id = c("SaveBtn", "SaveBtn"),
#'     el_control_type = c("Button", "Button"),
#'     stringsAsFactors = FALSE
#'   )
#' )
#' format(rec)
format.winrpa_recording <- function(x, ...) {
  n_ev <- if (is.data.frame(x$events)) nrow(x$events) else 0L
  n_lines <- length(x$script)
  mode <- as.character(x$mode %||% "desktop")
  sprintf(
    "<winrpa_recording: mode=%s, events=%d, lines=%d, dropped=%s>",
    mode,
    n_ev,
    n_lines,
    as.character(x$dropped %||% 0L)
  )
}

#' @rdname format.winrpa_recording
#' @export
print.winrpa_recording <- function(x, ...) {
  cat(format(x), "\n", sep = "")
  if (is.character(x$script) && length(x$script) > 0L) {
    cat(paste(x$script, collapse = "\n"), "\n", sep = "")
  }
  invisible(x)
}

#' Record Desktop Mouse and Keyboard Actions Into a Replayable R Script
#'
#' Uses a two-stage asynchronous pipeline: a lock-free, COM-free low-level
#' `WH_MOUSE_LL` + `WH_KEYBOARD_LL` hook thread paired with a dedicated MTA UI
#' Automation resolver thread that resolves `ElementFromPoint` / focused
#' controls before emitting normalized R calls (`rpa_wait_window()`,
#' `rpa_wait_find()`, `rpa_click()`, `rpa_type()`, `rpa_key_chord()`).
#'
#' @param ignore_self Logical scalar; when `TRUE`, omits events targeting the
#'   active R session process and its immediate parent host (for example RStudio
#'   UI clicks on "Stop").
#' @param redact_passwords Logical scalar; when `TRUE`, masks captured
#'   characters in UIA password controls (`IsPassword == TRUE`) inside C++ and
#'   emits `rpa_type(..., Sys.getenv("WINRPA_SECRET"), secret = TRUE)`.
#' @param include_wheel Logical scalar; whether to record mouse wheel scroll
#'   events.
#' @param stop_hotkey Character vector specifying a global hotkey that stops
#'   recording from any application (default `c("ctrl", "shift", "f9")`), or
#'   `NULL` to disable.
#' @param ignore_pids Optional integer vector of extra process IDs to exclude
#'   when emitting scripts.
#' @param file Optional output `.R` script path written by [rpa_record_stop()]
#'   or [rpa_record_save()].
#' @param coalesce_typing Logical scalar; merge contiguous printable keystrokes
#'   on the same control into a single `rpa_type()` call (folding in-burst
#'   `Backspace` typos).
#' @param wait_threshold_ms Numeric pause threshold in milliseconds above which
#'   the next element lookup uses `rpa_wait_find()` instead of `rpa_find()`.
#' @param recording A `"winrpa_recording"` object or raw events `data.frame`.
#' @param double_click_ms Optional override for double-click pairing interval in
#'   milliseconds (`NULL` queries Windows `GetDoubleClickTime()`).
#' @param drag_px Optional pixel movement threshold distinguishing a drag from a
#'   click (`NULL` uses Windows `SM_CXDRAG` / `SM_CYDRAG`).
#'
#' @return
#'   * `rpa_record_start()` invisibly returns `TRUE`.
#'   * `rpa_record_status()` returns a named list (`active`, `stopped_by_hotkey`,
#'     `elapsed_sec`, `captured_events`, `dropped_events`).
#'   * `rpa_record_stop()` and `rpa_record_emit()` return an object of class
#'     `"winrpa_recording"` with elements `script`, `events`, `mode`, `dropped`,
#'     and `elapsed_sec`.
#'   * `rpa_record_save()` invisibly returns the normalized output file path.
#' @export
#' @examplesIf interactive() && .Platform$OS.type == "windows"
#' rpa_record_start()
#' rpa_record_status()
#' rec <- rpa_record_stop()
rpa_record_start <- function(ignore_self = TRUE,
                             redact_passwords = TRUE,
                             include_wheel = TRUE,
                             stop_hotkey = c("ctrl", "shift", "f9"),
                             ignore_pids = integer()) {
  if (!is.logical(ignore_self) || length(ignore_self) != 1L || is.na(ignore_self)) {
    rlang::abort("'ignore_self' must be a single non-NA logical value.")
  }
  if (!is.logical(redact_passwords) || length(redact_passwords) != 1L || is.na(redact_passwords)) {
    rlang::abort("'redact_passwords' must be a single non-NA logical value.")
  }
  if (!is.logical(include_wheel) || length(include_wheel) != 1L || is.na(include_wheel)) {
    rlang::abort("'include_wheel' must be a single non-NA logical value.")
  }
  if (!is.numeric(ignore_pids) || anyNA(ignore_pids)) {
    rlang::abort("'ignore_pids' must be a numeric/integer vector with no NA values.")
  }

  hk <- parse_stop_hotkey_spec(stop_hotkey)
  rpa_session_check(require_interactive = TRUE)
  try(rpa_try_enable_dpi_awareness(), silent = TRUE)

  .winrpa_env$desktop_recorder_opts <- list(
    ignore_self = isTRUE(ignore_self),
    redact_passwords = isTRUE(redact_passwords),
    include_wheel = isTRUE(include_wheel),
    ignore_pids = as.integer(ignore_pids),
    stop_hotkey = stop_hotkey
  )

  cpp_record_start(
    isTRUE(include_wheel),
    isTRUE(redact_passwords),
    isTRUE(hk$enabled),
    as.integer(hk$mods),
    as.integer(hk$vk)
  )
  invisible(TRUE)
}

#' @rdname rpa_record_start
#' @export
rpa_record_status <- function() {
  cpp_record_status()
}

#' @rdname rpa_record_start
#' @export
rpa_record_stop <- function(file = NULL,
                            coalesce_typing = TRUE,
                            wait_threshold_ms = 800,
                            redact_passwords = NULL,
                            include_wheel = NULL,
                            ignore_self = NULL) {
  if (!is.null(file) && (!is.character(file) || length(file) != 1L || is.na(file) || !nzchar(file))) {
    rlang::abort("'file' must be NULL or a single non-empty file path.")
  }

  opts <- .winrpa_env$desktop_recorder_opts
  if (is.null(opts)) {
    opts <- list(
      ignore_self = TRUE,
      redact_passwords = TRUE,
      include_wheel = TRUE,
      ignore_pids = integer()
    )
  }

  use_ignore_self <- if (is.null(ignore_self)) isTRUE(opts$ignore_self) else isTRUE(ignore_self)
  use_redact <- if (is.null(redact_passwords)) isTRUE(opts$redact_passwords) else isTRUE(redact_passwords)
  use_wheel <- if (is.null(include_wheel)) isTRUE(opts$include_wheel) else isTRUE(include_wheel)

  raw <- cpp_record_stop()
  rec <- rpa_record_emit(
    recording = raw$events,
    coalesce_typing = coalesce_typing,
    wait_threshold_ms = wait_threshold_ms,
    redact_passwords = use_redact,
    include_wheel = use_wheel,
    ignore_self = use_ignore_self,
    ignore_pids = opts$ignore_pids %||% integer()
  )
  rec$dropped <- as.integer(raw$dropped %||% 0L)
  rec$elapsed_sec <- as.numeric(raw$elapsed_sec %||% 0)
  rec$stopped_by_hotkey <- isTRUE(raw$stopped_by_hotkey)
  if (rec$dropped > 0L) {
    rlang::warn(sprintf("%d raw hook event(s) dropped due to ring-buffer overflow.", rec$dropped))
  }
  if (!is.null(file)) {
    rpa_record_save(rec, file)
  }
  rec
}

#' @rdname rpa_record_start
#' @export
rpa_record_save <- function(recording, file) {
  if (!inherits(recording, "winrpa_recording")) {
    rlang::abort("'recording' must be a 'winrpa_recording' object.")
  }
  if (!is.character(file) || length(file) != 1L || is.na(file) || !nzchar(file)) {
    rlang::abort("'file' must be a single non-empty output file path.")
  }
  parent_dir <- dirname(file)
  if (!dir.exists(parent_dir)) {
    dir.create(parent_dir, recursive = TRUE, showWarnings = FALSE)
  }
  writeLines(as.character(recording$script), con = file, useBytes = TRUE)
  invisible(normalizePath(file, winslash = "/", mustWork = FALSE))
}

#' @rdname rpa_record_start
#' @export
rpa_record_emit <- function(recording,
                            coalesce_typing = TRUE,
                            wait_threshold_ms = 800,
                            redact_passwords = TRUE,
                            include_wheel = TRUE,
                            ignore_self = TRUE,
                            ignore_pids = integer(),
                            double_click_ms = NULL,
                            drag_px = NULL) {
  if (!is.logical(coalesce_typing) || length(coalesce_typing) != 1L || is.na(coalesce_typing)) {
    rlang::abort("'coalesce_typing' must be a single non-NA logical value.")
  }
  if (!is.numeric(wait_threshold_ms) || length(wait_threshold_ms) != 1L ||
      is.na(wait_threshold_ms) || wait_threshold_ms < 0) {
    rlang::abort("'wait_threshold_ms' must be a non-negative number.")
  }
  if (!is.logical(redact_passwords) || length(redact_passwords) != 1L || is.na(redact_passwords)) {
    rlang::abort("'redact_passwords' must be a single non-NA logical value.")
  }
  if (!is.logical(include_wheel) || length(include_wheel) != 1L || is.na(include_wheel)) {
    rlang::abort("'include_wheel' must be a single non-NA logical value.")
  }
  if (!is.logical(ignore_self) || length(ignore_self) != 1L || is.na(ignore_self)) {
    rlang::abort("'ignore_self' must be a single non-NA logical value.")
  }
  if (!is.numeric(ignore_pids) || anyNA(ignore_pids)) {
    rlang::abort("'ignore_pids' must be an integer/numeric vector with no NA values.")
  }

  df <- if (inherits(recording, "winrpa_recording")) {
    recording$events
  } else if (is.data.frame(recording)) {
    recording
  } else {
    rlang::abort("'recording' must be a 'winrpa_recording' object or an events data.frame.")
  }

  metrics <- cpp_record_system_metrics()
  dbl_ms <- if (is.null(double_click_ms)) as.numeric(metrics$double_click_ms) else as.numeric(double_click_ms)
  dpx <- if (is.null(drag_px)) max(as.integer(metrics$drag_cx), as.integer(metrics$drag_cy), 4L) else as.integer(drag_px)

  excluded_pids <- unique(as.integer(c(
    if (isTRUE(ignore_self)) c(metrics$self_pid, metrics$parent_pid) else integer(),
    ignore_pids
  )))
  excluded_pids <- excluded_pids[!is.na(excluded_pids) & excluded_pids > 0L]

  filtered_df <- df
  if (nrow(filtered_df) > 0L && length(excluded_pids) > 0L) {
    wp <- as.integer(ensure_df_col(filtered_df, "win_pid", 0L))
    ep <- as.integer(ensure_df_col(filtered_df, "el_pid", 0L))
    drop_mask <- (wp %in% excluded_pids) | (ep > 0L & ep %in% excluded_pids)
    filtered_df <- filtered_df[!drop_mask, , drop = FALSE]
  }
  rownames(filtered_df) <- NULL

  steps <- normalize_desktop_steps(
    df = filtered_df,
    coalesce_typing = coalesce_typing,
    redact_passwords = redact_passwords,
    include_wheel = include_wheel,
    double_click_ms = dbl_ms,
    drag_px = dpx
  )

  lines <- c(
    "library(winrpa)",
    "rpa_try_enable_dpi_awareness()",
    "rpa_enable_killswitch()"
  )

  current_win_key <- NULL
  current_el_key <- NULL
  current_el_var <- NULL
  el_counter <- 0L
  prev_ts <- NA_real_
  win_info_emitted <- FALSE

  emit_window_if_needed <- function(st) {
    wtitle <- clean_window_title(st$win_title)
    wclass <- as.character(st$win_class %||% "")
    if (!nzchar(wtitle) && !nzchar(wclass)) {
      return(FALSE)
    }
    wkey <- win_key_for_row(st)
    if (identical(wkey, current_win_key)) {
      return(FALSE)
    }
    flyout <- is_shell_flyout_window(wtitle, wclass)
    wargs <- list()
    if (flyout) {
      if (nzchar(wtitle)) {
        wargs$title <- paste0("^", escape_regex_literal(wtitle), "$")
        if (nzchar(wclass)) {
          wargs$class_name <- wclass
        }
        wargs$regex <- TRUE
      } else {
        wargs$class_name <- wclass
      }
    } else {
      if (nzchar(wtitle)) {
        wargs$title <- wtitle
      } else if (nzchar(wclass)) {
        wargs$class_name <- wclass
      }
    }
    if (length(wargs) > 0L) {
      lines <<- c(
        lines,
        sprintf("win <- rpa_wait_window(%s)", format_named_call_args(wargs))
      )
      if (!flyout) {
        lines <<- c(lines, "rpa_window_focus(win)")
      }
    }
    current_win_key <<- wkey
    current_el_key <<- NULL
    current_el_var <<- NULL
    win_info_emitted <<- FALSE
    TRUE
  }

  for (st in steps) {
    win_changed <- emit_window_if_needed(st)
    delta_ms <- if (is.na(prev_ts)) 0 else (st$timestamp_ms - prev_ts)
    use_wait <- isTRUE(win_changed) || (delta_ms > wait_threshold_ms)

    if (identical(st$type, "wheel")) {
      if (isTRUE(st$win_has_rect)) {
        if (!win_info_emitted) {
          lines <- c(lines, "win_info <- rpa_element_info(win)")
          win_info_emitted <- TRUE
        }
        lines <- c(
          lines,
          sprintf("rpa_mouse_move(win_info$left + %dL, win_info$top + %dL)", st$rel_x, st$rel_y)
        )
      } else {
        lines <- c(lines, sprintf("rpa_mouse_move(%dL, %dL)", st$x, st$y))
      }
      if (isTRUE(st$horizontal)) {
        lines <- c(lines, sprintf("rpa_mouse_scroll(delta = %dL, horizontal = TRUE)", st$wheel_delta))
      } else {
        lines <- c(lines, sprintf("rpa_mouse_scroll(delta = %dL)", st$wheel_delta))
      }
      prev_ts <- st$timestamp_ms
      next
    }

    if (identical(st$type, "key_chord")) {
      chord <- st$chord
      if (length(chord) == 1L) {
        lines <- c(lines, sprintf("rpa_key_chord(%s)", r_quote_str(chord)))
      } else {
        quoted <- paste(vapply(chord, r_quote_str, character(1L)), collapse = ", ")
        lines <- c(lines, sprintf("rpa_key_chord(c(%s))", quoted))
      }
      if (identical(tolower(chord), "win") || identical(tolower(chord), c("win", "s"))) {
        lines <- c(lines, "Sys.sleep(0.4)")
      }
      prev_ts <- st$timestamp_ms
      next
    }

    if (identical(st$type, "drag")) {
      if (delta_ms > wait_threshold_ms) {
        lines <- c(lines, sprintf("Sys.sleep(%.2f)", delta_ms / 1000))
      }
      if (isTRUE(st$win_has_rect)) {
        if (!win_info_emitted) {
          lines <- c(lines, "win_info <- rpa_element_info(win)")
          win_info_emitted <- TRUE
        }
        lines <- c(
          lines,
          sprintf(
            "# Drag %s from (win_info$left + %dL, win_info$top + %dL) to (win_info$left + %dL, win_info$top + %dL)",
            st$button, st$rel_x, st$rel_y, st$to_rel_x, st$to_rel_y
          ),
          sprintf("rpa_mouse_move(win_info$left + %dL, win_info$top + %dL)", st$rel_x, st$rel_y),
          sprintf("rpa_mouse_click(win_info$left + %dL, win_info$top + %dL, button = %s)", st$to_rel_x, st$to_rel_y, r_quote_str(st$button))
        )
      } else {
        lines <- c(
          lines,
          sprintf("rpa_mouse_move(%dL, %dL)", st$x, st$y),
          sprintf("rpa_mouse_click(%dL, %dL, button = %s)", st$to_x, st$to_y, r_quote_str(st$button))
        )
      }
      prev_ts <- st$timestamp_ms
      next
    }

    sel <- pick_desktop_selector(st)
    if (is.null(sel)) {
      if (identical(st$type, "click")) {
        if (delta_ms > wait_threshold_ms) {
          lines <- c(lines, sprintf("Sys.sleep(%.2f)", delta_ms / 1000))
        }
        extra <- character()
        if (!identical(st$button, "left")) {
          extra <- c(extra, sprintf("button = %s", r_quote_str(st$button)))
        }
        if (as.integer(st$clicks) > 1L) {
          extra <- c(extra, sprintf("clicks = %dL", as.integer(st$clicks)))
        }
        suffix <- if (length(extra) > 0L) paste0(", ", paste(extra, collapse = ", ")) else ""
        if (isTRUE(st$win_has_rect)) {
          if (!win_info_emitted) {
            lines <- c(lines, "win_info <- rpa_element_info(win)")
            win_info_emitted <- TRUE
          }
          lines <- c(lines, sprintf(
            "rpa_mouse_click(x = win_info$left + %dL, y = win_info$top + %dL%s)",
            st$rel_x, st$rel_y, suffix
          ))
        } else {
          lines <- c(lines, sprintf("rpa_mouse_click(x = %dL, y = %dL%s)", st$x, st$y, suffix))
        }
      }
      prev_ts <- st$timestamp_ms
      next
    }

    wkey <- win_key_for_row(st)
    ekey <- el_key_for_step(st, wkey)
    if (!identical(ekey, current_el_key)) {
      el_counter <- el_counter + 1L
      current_el_var <- sprintf("el_%d", el_counter)
      current_el_key <- ekey
      find_fn <- if (use_wait) "rpa_wait_find" else "rpa_find"
      lines <- c(
        lines,
        sprintf("%s <- %s(win, %s)", current_el_var, find_fn, format_named_call_args(sel$args))
      )
    }

    if (identical(st$type, "click")) {
      if (isTRUE(st$has_toggle) && identical(st$el_control_type, "CheckBox") &&
          identical(st$button, "left") && as.integer(st$clicks) == 1L) {
        lines <- c(lines, sprintf("rpa_toggle(%s)", current_el_var))
      } else if (isTRUE(st$has_select) && identical(st$el_control_type, "RadioButton") &&
                 identical(st$button, "left") && as.integer(st$clicks) == 1L) {
        lines <- c(lines, sprintf("rpa_select(%s)", current_el_var))
      } else {
        c_args <- character()
        if (!identical(st$button, "left")) {
          c_args <- c(c_args, sprintf("button = %s", r_quote_str(st$button)))
        }
        if (as.integer(st$clicks) > 1L) {
          c_args <- c(c_args, sprintf("clicks = %dL", as.integer(st$clicks)))
        }
        if (length(c_args) == 0L) {
          lines <- c(lines, sprintf("rpa_click(%s)", current_el_var))
        } else {
          lines <- c(lines, sprintf("rpa_click(%s, %s)", current_el_var, paste(c_args, collapse = ", ")))
        }
      }
    } else if (identical(st$type, "type_secret")) {
      lines <- c(lines, sprintf("rpa_type(%s, Sys.getenv(\"WINRPA_SECRET\"), secret = TRUE)", current_el_var))
    } else if (identical(st$type, "type")) {
      lines <- c(lines, sprintf("rpa_type(%s, %s)", current_el_var, r_quote_str(st$text)))
    }

    prev_ts <- st$timestamp_ms
  }

  structure(
    list(
      script = lines,
      events = filtered_df,
      mode = "desktop",
      dropped = if (inherits(recording, "winrpa_recording")) (recording$dropped %||% 0L) else 0L,
      elapsed_sec = if (inherits(recording, "winrpa_recording")) (recording$elapsed_sec %||% 0) else 0,
      stopped_by_hotkey = if (inherits(recording, "winrpa_recording")) isTRUE(recording$stopped_by_hotkey) else FALSE,
      created_at = Sys.time()
    ),
    class = "winrpa_recording"
  )
}
