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

#' Verify That an Active Interactive Windows Desktop Session Is Ready
#'
#' Checks that the current platform is Windows, an interactive input desktop
#' is unlocked and attached, and the emergency abort flag is clear.
#'
#' @param allow_non_windows Logical scalar; if `TRUE`, skips the Windows OS
#'   check (used internally by platform-independent helpers).
#'
#' @return Invisibly returns `TRUE` when the session is ready; otherwise throws
#'   an informative error.
#' @export
#' @examplesIf interactive() && .Platform$OS.type == "windows"
#' rpa_session_check()
rpa_session_check <- function(allow_non_windows = FALSE) {
  if (!isTRUE(allow_non_windows) && .Platform$OS.type != "windows") {
    rlang::abort("winrpa desktop automation requires Windows.")
  }
  if (.Platform$OS.type == "windows" && !rpa_is_interactive_desktop()) {
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
