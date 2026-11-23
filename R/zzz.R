#' @keywords internal
#' @useDynLib winrpa, .registration = TRUE
"_PACKAGE"

.winrpa_env <- new.env(parent = emptyenv())

.onLoad <- function(libname, pkgname) {
  reg.finalizer(
    .winrpa_env,
    function(e) {
      try(cpp_shutdown_runtime(), silent = TRUE)
    },
    onexit = TRUE
  )
  invisible(NULL)
}

.onUnload <- function(libpath) {
  try(cpp_shutdown_runtime(), silent = TRUE)
  invisible(NULL)
}

#' Verify That an Active Desktop or Runtime Session Is Ready
#'
#' Checks that the current platform is Windows (unless `allow_non_windows` is
#' `TRUE`), the emergency abort flag is clear, and -- when `require_interactive`
#' is `TRUE` -- an unlocked interactive Windows input desktop is attached. Headless
#' Web CDP (`rpa_web_*`) and background `PostMessageW` (`rpa_post_*`) helpers
#' set `require_interactive = FALSE` so they can run without claiming the
#' active console.
#'
#' @param allow_non_windows Logical scalar; if `TRUE`, skips the Windows OS
#'   check (used internally by platform-independent helpers).
#' @param require_interactive Logical scalar; if `TRUE`, verifies an unlocked
#'   interactive desktop via `rpa_is_interactive_desktop()`.
#'
#' @return Invisibly returns `TRUE` when the session is ready; otherwise throws
#'   an informative error.
#' @export
#' @examplesIf interactive() && .Platform$OS.type == "windows"
#' rpa_session_check()
rpa_session_check <- function(allow_non_windows = FALSE,
                              require_interactive = TRUE) {
  if (!isTRUE(allow_non_windows) && .Platform$OS.type != "windows") {
    rlang::abort("winrpa desktop automation requires Windows.")
  }
  if (isTRUE(require_interactive) &&
      .Platform$OS.type == "windows" &&
      !rpa_is_interactive_desktop()) {
    rlang::abort(
      paste0(
        "No interactive Windows desktop session detected ",
        "(workstation locked or session disconnected)."
      )
    )
  }
  if (rpa_abort_triggered()) {
    rlang::abort(
      paste0(
        "winrpa emergency abort flag is active. ",
        "Call rpa_reset_abort() before running further actions."
      )
    )
  }
  invisible(TRUE)
}
