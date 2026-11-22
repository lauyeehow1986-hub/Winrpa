## R CMD check results

0 errors | 0 warnings | 0 notes (`R CMD check --no-manual`)
0 errors | 0 warnings | 2 notes (`R CMD check --no-manual --as-cran`)

* Test environment:
  * Local Windows 11 x64 (build 26300), R version 4.5.2 (2025-10-31 ucrt)
  * Rtools45 MinGW-w64 `g++.exe` (GCC 14.3.0), C++17 via `cpp11`

* Note details under `--as-cran`:
  * `New submission`: First submission of `winrpa`.
  * `Found the following files/directories: ''NULL''`: Empty directory produced by base R 4.5.2 `tools:::setRlibs()` on Windows passing `"R_LIBS_USER='NULL'"` / `"R_LIBS_SITE='NULL'"` to `Rterm.exe` when `_R_CHECK_SUGGESTS_ONLY_=TRUE` runs examples; not created by `winrpa` code (`R CMD check --no-manual` finishes with `Status: OK` and zero notes).

## Additional checks

* No published academic methodology reference applies: `winrpa` is a native `C++17` interface to operating system accessibility (`Microsoft UI Automation`) and Win32 input/clipboard/capture APIs (`SendInput`, `PrintWindow`, `RegisterHotKey`), with informative unsupported-OS stubs behind `#ifdef _WIN32` guards on non-Windows platforms.
* Static and dynamic CRAN Six-Sigma Guardian gates 1–5 (`DESCRIPTION`, runtime state-purity sandbox, `.Rd` / `@examples`, URLs, and native code hygiene) passed with 0 issues.
* Live desktop examples and desktop integration tests are guarded with `@examplesIf interactive() && .Platform$OS.type == "windows"` and `testthat::skip_if_not(interactive() && .Platform$OS.type == "windows" && rpa_is_interactive_desktop())` so CRAN check servers run all unit tests headlessly.
