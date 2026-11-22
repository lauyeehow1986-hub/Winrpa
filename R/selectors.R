assert_optional_scalar_string <- function(x, arg_name) {
  if (is.null(x)) {
    return(NULL)
  }
  if (!is.character(x) || length(x) != 1L || is.na(x)) {
    rlang::abort(sprintf("'%s' must be NULL or a single non-NA character string.", arg_name))
  }
  x
}

assert_element <- function(target, arg_name = "target") {
  if (!inherits(target, "winrpa_element")) {
    rlang::abort(sprintf("'%s' must be a 'winrpa_element' object.", arg_name))
  }
  target
}

as_winrpa_element <- function(ptr) {
  if (is.null(ptr)) {
    return(NULL)
  }
  structure(list(ptr = ptr), class = "winrpa_element")
}

#' Construct a UI Automation Element Selector
#'
#' Creates a pipe-friendly selector specification for matching controls in the
#' Windows UI Automation tree.
#'
#' @param name Optional character scalar matching control `Name`.
#' @param automation_id Optional character scalar matching `AutomationId`.
#' @param control_type Optional character scalar matching UIA control type
#'   (for example `"Button"`, `"Edit"`, `"MenuItem"`, `"DataGrid"`).
#' @param class_name Optional character scalar matching Win32/UIA `ClassName`.
#' @param regex Logical scalar; if `TRUE`, string criteria are treated as
#'   regular expressions.
#'
#' @return An object of class `"winrpa_selector"`.
#' @export
#' @examples
#' sel <- rpa_selector(name = "Save", control_type = "Button")
#' format(sel)
rpa_selector <- function(name = NULL,
                         automation_id = NULL,
                         control_type = NULL,
                         class_name = NULL,
                         regex = FALSE) {
  name <- assert_optional_scalar_string(name, "name")
  automation_id <- assert_optional_scalar_string(automation_id, "automation_id")
  control_type <- assert_optional_scalar_string(control_type, "control_type")
  class_name <- assert_optional_scalar_string(class_name, "class_name")
  if (!is.logical(regex) || length(regex) != 1L || is.na(regex)) {
    rlang::abort("'regex' must be a single non-NA logical value.")
  }

  structure(
    list(
      name = name,
      automation_id = automation_id,
      control_type = control_type,
      class_name = class_name,
      regex = regex
    ),
    class = "winrpa_selector"
  )
}

#' Format a `winrpa_selector` Object
#'
#' @param x A `winrpa_selector` object.
#' @param ... Unused additional arguments.
#'
#' @return A character scalar summarizing the selector criteria.
#' @export
#' @examples
#' sel <- rpa_selector(name = "OK", control_type = "Button")
#' format(sel)
format.winrpa_selector <- function(x, ...) {
  parts <- character()
  for (field in c("name", "automation_id", "control_type", "class_name")) {
    val <- x[[field]]
    if (!is.null(val) && nzchar(val)) {
      parts <- c(parts, sprintf('%s="%s"', field, val))
    }
  }
  if (isTRUE(x$regex)) {
    parts <- c(parts, "regex=TRUE")
  }
  if (length(parts) == 0L) {
    return("<winrpa_selector: (any)>")
  }
  paste0("<winrpa_selector: ", paste(parts, collapse = ", "), ">")
}

#' Print a `winrpa_selector` Object
#'
#' @param x A `winrpa_selector` object.
#' @param ... Unused additional arguments.
#'
#' @return Invisibly returns `x`.
#' @export
#' @examples
#' sel <- rpa_selector(name = "OK", control_type = "Button")
#' print(sel)
print.winrpa_selector <- function(x, ...) {
  cat(format(x), "\n", sep = "")
  invisible(x)
}

#' Format a `winrpa_element` Handle
#'
#' @param x A `winrpa_element` object.
#' @param ... Unused additional arguments.
#'
#' @return A character scalar describing the element.
#' @export
#' @examplesIf interactive() && .Platform$OS.type == "windows"
#' wins <- rpa_windows()
#' if (nrow(wins) > 0L) {
#'   w <- rpa_window(pid = wins$pid[1L])
#'   format(w)
#' }
format.winrpa_element <- function(x, ...) {
  info <- tryCatch(cpp_uia_element_info(x$ptr), error = function(e) NULL)
  if (is.null(info)) {
    return("<winrpa_element: (detached or unavailable)>")
  }
  sprintf(
    '<winrpa_element: [%s] "%s" (id="%s", hwnd=%s)>',
    info$control_type,
    info$name,
    info$automation_id,
    if (nzchar(info$hwnd)) info$hwnd else "none"
  )
}

#' Print a `winrpa_element` Handle
#'
#' @param x A `winrpa_element` object.
#' @param ... Unused additional arguments.
#'
#' @return Invisibly returns `x`.
#' @export
#' @examplesIf interactive() && .Platform$OS.type == "windows"
#' wins <- rpa_windows()
#' if (nrow(wins) > 0L) {
#'   w <- rpa_window(pid = wins$pid[1L])
#'   print(w)
#' }
print.winrpa_element <- function(x, ...) {
  cat(format(x), "\n", sep = "")
  invisible(x)
}

#' List Top-Level Desktop Windows
#'
#' Enumerates top-level windows visible to Microsoft UI Automation on the
#' active desktop.
#'
#' @return A `data.frame` with columns `name`, `class_name`, `automation_id`,
#'   `control_type`, `pid`, `hwnd`, `left`, `top`, `right`, `bottom`, and
#'   `enabled`.
#' @export
#' @examplesIf interactive() && .Platform$OS.type == "windows"
#' wins <- rpa_windows()
#' head(wins[, c("name", "pid", "hwnd")])
rpa_windows <- function() {
  rpa_session_check()
  cpp_uia_list_windows()
}

#' Find a Top-Level Desktop Window
#'
#' Locates the first top-level window matching the supplied title, Win32 class
#' name, or process ID.
#'
#' @param title Optional character scalar matching window title.
#' @param class_name Optional character scalar matching window class name.
#' @param pid Optional integer process identifier.
#' @param regex Logical scalar; if `TRUE`, match `title` and `class_name` using
#'   regular expressions.
#'
#' @return An object of class `"winrpa_element"`.
#' @export
#' @examplesIf interactive() && .Platform$OS.type == "windows"
#' wins <- rpa_windows()
#' if (nrow(wins) > 0L) {
#'   w <- rpa_window(pid = wins$pid[1L])
#'   w
#' }
rpa_window <- function(title = NULL, class_name = NULL, pid = NULL, regex = FALSE) {
  title <- assert_optional_scalar_string(title, "title")
  class_name <- assert_optional_scalar_string(class_name, "class_name")
  if (!is.null(pid)) {
    if (!is.numeric(pid) || length(pid) != 1L || is.na(pid) || pid <= 0) {
      rlang::abort("'pid' must be NULL or a positive integer.")
    }
    pid_int <- as.integer(pid)
  } else {
    pid_int <- 0L
  }
  if (!is.logical(regex) || length(regex) != 1L || is.na(regex)) {
    rlang::abort("'regex' must be a single non-NA logical value.")
  }

  rpa_session_check()
  ptr <- cpp_uia_find_window(
    if (is.null(title)) "" else title,
    if (is.null(class_name)) "" else class_name,
    pid_int,
    isTRUE(regex)
  )
  if (is.null(ptr)) {
    rlang::abort("No matching top-level window was found.")
  }
  as_winrpa_element(ptr)
}

resolve_find_criteria <- function(selector,
                                  name,
                                  automation_id,
                                  control_type,
                                  class_name,
                                  regex) {
  if (!is.null(selector)) {
    if (!inherits(selector, "winrpa_selector")) {
      rlang::abort("'selector' must be a 'winrpa_selector' object.")
    }
    return(selector)
  }
  rpa_selector(
    name = name,
    automation_id = automation_id,
    control_type = control_type,
    class_name = class_name,
    regex = regex
  )
}

#' Find Matching UI Elements Under a Parent Control
#'
#' Searches the UI Automation subtree beneath `parent` (or the desktop root
#' when `parent = NULL`) using either a `winrpa_selector` or inline criteria.
#'
#' @param parent A `winrpa_element` scope root, a `winrpa_selector`, or `NULL`
#'   for the desktop root.
#' @param selector Optional `winrpa_selector` object.
#' @param name,automation_id,control_type,class_name Optional character scalar
#'   filters used when `selector` is `NULL`.
#' @param regex Logical scalar; treat string filters as regular expressions.
#' @param max_results Maximum number of matching elements to return.
#'
#' @return `rpa_find_all()` returns a list of `"winrpa_element"` objects;
#'   `rpa_find()` returns the first matching `"winrpa_element"` or throws an
#'   error if no element matches.
#' @export
#' @examplesIf interactive() && .Platform$OS.type == "windows"
#' wins <- rpa_windows()
#' if (nrow(wins) > 0L) {
#'   w <- rpa_window(pid = wins$pid[1L])
#'   btns <- rpa_find_all(w, control_type = "Button", max_results = 5L)
#'   length(btns)
#' }
rpa_find_all <- function(parent = NULL,
                         selector = NULL,
                         name = NULL,
                         automation_id = NULL,
                         control_type = NULL,
                         class_name = NULL,
                         regex = FALSE,
                         max_results = 100L) {
  if (inherits(parent, "winrpa_selector") && is.null(selector)) {
    selector <- parent
    parent <- NULL
  }
  if (!is.null(parent)) {
    assert_element(parent, "parent")
  }
  if (!is.numeric(max_results) || length(max_results) != 1L || is.na(max_results) || max_results < 1) {
    rlang::abort("'max_results' must be a positive integer.")
  }
  sel <- resolve_find_criteria(selector, name, automation_id, control_type, class_name, regex)
  rpa_session_check()

  ptrs <- cpp_uia_find_elements(
    if (is.null(parent)) NULL else parent$ptr,
    if (is.null(sel$name)) "" else sel$name,
    if (is.null(sel$automation_id)) "" else sel$automation_id,
    if (is.null(sel$control_type)) "" else sel$control_type,
    if (is.null(sel$class_name)) "" else sel$class_name,
    isTRUE(sel$regex),
    as.integer(max_results)
  )
  lapply(ptrs, as_winrpa_element)
}

#' @rdname rpa_find_all
#' @export
rpa_find <- function(parent = NULL,
                     selector = NULL,
                     name = NULL,
                     automation_id = NULL,
                     control_type = NULL,
                     class_name = NULL,
                     regex = FALSE) {
  if (inherits(parent, "winrpa_selector") && is.null(selector)) {
    selector <- parent
    parent <- NULL
  }
  sel <- resolve_find_criteria(selector, name, automation_id, control_type, class_name, regex)
  matches <- rpa_find_all(
    parent = parent,
    selector = sel,
    max_results = 1L
  )
  if (length(matches) == 0L) {
    rlang::abort(sprintf("No UI element matched %s.", format(sel)))
  }
  matches[[1L]]
}

#' Inspect Cached Properties and Supported Patterns of a UI Element
#'
#' @param target A `winrpa_element` object.
#'
#' @return A named list with element properties (`name`, `automation_id`,
#'   `control_type`, `class_name`, `pid`, `hwnd`, `left`, `top`, `right`,
#'   `bottom`, `enabled`, `patterns`).
#' @export
#' @examplesIf interactive() && .Platform$OS.type == "windows"
#' wins <- rpa_windows()
#' if (nrow(wins) > 0L) {
#'   w <- rpa_window(pid = wins$pid[1L])
#'   rpa_element_info(w)
#' }
rpa_element_info <- function(target) {
  assert_element(target)
  rpa_session_check()
  cpp_uia_element_info(target$ptr)
}

#' Snapshot a Bounded Subtree of the UI Automation Control View
#'
#' @param target Optional `winrpa_element` root; `NULL` inspects from desktop
#'   root.
#' @param max_depth Maximum tree depth relative to `target`.
#' @param max_nodes Maximum number of nodes to collect.
#'
#' @return A `data.frame` describing the captured UI nodes.
#' @export
#' @examplesIf interactive() && .Platform$OS.type == "windows"
#' wins <- rpa_windows()
#' if (nrow(wins) > 0L) {
#'   w <- rpa_window(pid = wins$pid[1L])
#'   head(rpa_inspect_tree(w, max_depth = 2L, max_nodes = 20L))
#' }
rpa_inspect_tree <- function(target = NULL, max_depth = 3L, max_nodes = 200L) {
  if (!is.null(target)) {
    assert_element(target)
  }
  if (!is.numeric(max_depth) || length(max_depth) != 1L || is.na(max_depth) || max_depth < 0) {
    rlang::abort("'max_depth' must be a non-negative integer.")
  }
  if (!is.numeric(max_nodes) || length(max_nodes) != 1L || is.na(max_nodes) || max_nodes < 1) {
    rlang::abort("'max_nodes' must be a positive integer.")
  }
  rpa_session_check()
  cpp_uia_snapshot_tree(
    if (is.null(target)) NULL else target$ptr,
    as.integer(max_depth),
    as.integer(max_nodes)
  )
}

#' Normalize Virtual Desktop Coordinates
#'
#' Maps pixel coordinates `(x, y)` (including negative coordinates on secondary
#' monitors positioned left of or above the primary monitor) onto the
#' `0..65535` range expected by `SendInput()` with `MOUSEEVENTF_VIRTUALDESK`.
#'
#' @param x,y Integer target screen pixel coordinates.
#' @param vx,vy Integer virtual desktop origin.
#' @param vw,vh Integer virtual desktop width and height.
#'
#' @return Named integer vector of length 2 (`norm_x`, `norm_y`).
#' @export
#' @examples
#' rpa_normalize_virtual_coords(-960L, 540L, vx = -1920L, vy = 0L, vw = 3840L, vh = 1080L)
rpa_normalize_virtual_coords <- function(x, y, vx = 0L, vy = 0L, vw = 1920L, vh = 1080L) {
  res <- cpp_normalize_virtual_coords(
    as.integer(x), as.integer(y),
    as.integer(vx), as.integer(vy),
    as.integer(vw), as.integer(vh)
  )
  stats::setNames(res, c("norm_x", "norm_y"))
}

#' Check Whether an Interactive Desktop Session Is Active
#'
#' Detects locked workstations or disconnected remote desktop sessions where GUI
#' automation would otherwise fail silently.
#'
#' @return Logical scalar (`TRUE` if an interactive Windows desktop is open).
#' @export
#' @examples
#' rpa_is_interactive_desktop()
rpa_is_interactive_desktop <- function() {
  cpp_is_interactive_desktop()
}

#' Query or Reset the Emergency Abort Flag
#'
#' @return `rpa_abort_triggered()` returns a logical scalar; `rpa_reset_abort()`
#'   invisibly returns `NULL`.
#' @export
#' @examples
#' rpa_reset_abort()
#' rpa_abort_triggered()
rpa_abort_triggered <- function() {
  cpp_abort_triggered()
}

#' @rdname rpa_abort_triggered
#' @export
rpa_reset_abort <- function() {
  cpp_reset_abort()
  invisible(NULL)
}

resolve_hotkey_vk <- function(key) {
  if (!is.character(key) || length(key) != 1L || is.na(key) || !nzchar(key)) {
    rlang::abort("'key' must be a single non-empty character string.")
  }
  k <- tolower(trimws(key))
  if (grepl("^f([1-9]|1[0-9]|2[0-4])$", k)) {
    num <- as.integer(sub("^f", "", k))
    return(0x70L + num - 1L)
  }
  if (k %in% c("esc", "escape")) return(0x1BL)
  if (k == "pause") return(0x13L)
  if (k %in% c("space")) return(0x20L)
  if (nchar(k) == 1L && grepl("^[a-z0-9]$", k)) {
    return(utf8ToInt(toupper(k)))
  }
  rlang::abort(sprintf("Unsupported killswitch hotkey '%s'.", key))
}

#' Register or Unregister the Global Emergency Kill-Switch Hotkey
#'
#' Registers a system-wide hotkey on a dedicated background message pump thread
#' that immediately sets the lock-free emergency abort flag when pressed.
#'
#' @param modifiers Character vector of modifier names (`"ctrl"`, `"alt"`,
#'   `"shift"`, `"win"`).
#' @param key Trigger key name (for example `"F12"`, `"esc"`, or `"pause"`).
#'
#' @return Logical scalar indicating whether the hotkey operation succeeded.
#' @export
#' @examplesIf interactive() && .Platform$OS.type == "windows"
#' rpa_enable_killswitch()
#' rpa_disable_killswitch()
rpa_enable_killswitch <- function(modifiers = c("ctrl", "alt"), key = "F12") {
  if (!is.character(modifiers) || length(modifiers) == 0L || anyNA(modifiers)) {
    rlang::abort("'modifiers' must be a non-empty character vector.")
  }
  mod_map <- c(alt = 1L, ctrl = 2L, control = 2L, shift = 4L, win = 8L)
  mods <- tolower(trimws(modifiers))
  unknown <- setdiff(mods, names(mod_map))
  if (length(unknown) > 0L) {
    rlang::abort(sprintf("Unknown hotkey modifier(s): %s.", paste(unknown, collapse = ", ")))
  }
  mod_mask <- sum(unique(unname(mod_map[mods])))
  vk <- resolve_hotkey_vk(key)

  rpa_session_check()
  cpp_enable_killswitch(as.integer(mod_mask), as.integer(vk))
}

#' @rdname rpa_enable_killswitch
#' @export
rpa_disable_killswitch <- function() {
  cpp_disable_killswitch()
}
