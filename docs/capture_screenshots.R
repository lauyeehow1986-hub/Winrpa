# Live Windows 11 Screenshot Capture Script for winrpa GitHub Pages
# Run from the repository root in an interactive R/RStudio session:
#   source("docs/capture_screenshots.R")

if (.Platform$OS.type != "windows") {
  stop("docs/capture_screenshots.R requires an active Windows desktop session.")
}

if (requireNamespace("pkgload", quietly = TRUE) && file.exists("DESCRIPTION")) {
  pkgload::load_all(quiet = TRUE)
} else {
  library(winrpa)
}

if (!rpa_is_interactive_desktop()) {
  stop("No interactive Windows desktop session detected (workstation locked or headless).")
}

out_dir <- file.path("docs", "assets")
dir.create(out_dir, recursive = TRUE, showWarnings = FALSE)
out_png <- normalizePath(file.path(out_dir, "live-notepad-automation.png"), mustWork = FALSE)

bmp_to_png <- function(bmp_path, png_path) {
  bmp_norm <- normalizePath(bmp_path, winslash = "\\", mustWork = TRUE)
  png_norm <- normalizePath(png_path, winslash = "\\", mustWork = FALSE)
  ps_cmd <- paste0(
    "Add-Type -AssemblyName System.Drawing; ",
    "$bmp = [System.Drawing.Bitmap]::FromFile('", gsub("'", "''", bmp_norm), "'); ",
    "$w = $bmp.Width; $h = $bmp.Height; ",
    "$bmp.Save('", gsub("'", "''", png_norm), "', [System.Drawing.Imaging.ImageFormat]::Png); ",
    "$bmp.Dispose(); ",
    "Write-Output ($w.ToString() + 'x' + $h.ToString())"
  )
  dims <- system2(
    "powershell",
    args = c("-NoProfile", "-Command", ps_cmd),
    stdout = TRUE,
    stderr = TRUE
  )
  dims
}

message("1/4 Launching Windows 11 Notepad via rpa_launch()...")
rpa_launch("notepad.exe")

win <- rpa_wait_for(
  function() tryCatch(rpa_window(class_name = "Notepad"), error = function(e) NULL),
  timeout = 8,
  interval = 0.05,
  label = "Notepad window"
)

editor <- tryCatch(
  rpa_find(win, control_type = "Document"),
  error = function(e) rpa_find(win, control_type = "Edit")
)

message("2/4 Clearing restored tab contents and typing across Unicode / Scancode / Paste modes...")
rpa_click(editor)
Sys.sleep(0.08)
rpa_key_chord(c("ctrl", "a"))
rpa_key_chord("backspace")
Sys.sleep(0.05)

editor |>
  rpa_type("Hello from winrpa! (Unicode: caf\u00e9 \u65e5\u672c\u8a9e)\n", mode = "unicode") |>
  rpa_type("Hardware scancode test 123\n", mode = "scancode", delay_ms = 5L) |>
  rpa_type("Transactional paste mode: OK (original clipboard restored)\n", mode = "paste")

Sys.sleep(0.25)

message("3/4 Capturing window BMP via rpa_screenshot()...")
tmp_bmp <- tempfile(pattern = "winrpa_notepad_", fileext = ".bmp")
on.exit(unlink(tmp_bmp), add = TRUE)
rpa_screenshot(win, path = tmp_bmp)

message("4/4 Converting BMP -> PNG at: ", out_png)
dims <- bmp_to_png(tmp_bmp, out_png)
info <- file.info(out_png)
message(sprintf(
  "Saved live screenshot: %s (%s px, %s bytes)",
  out_png,
  paste(dims, collapse = " "),
  format(info$size, big.mark = ",")
))

if (interactive()) {
  shell.exec(out_png)
}
