"""Local FastMCP server for verifying Rtools45/MinGW-w64 UIA builds, C++ CRAN hygiene,
Windows DPI/VirtualDesk geometry, and R CMD check for the winrpa package.

Run via:
    python mcp_server/rpa_dev_mcp.py

Requirements:
    pip install mcp  (or fastmcp)
"""
from __future__ import annotations

import json
import os
import platform
import re
import shutil
import subprocess
import tempfile
from pathlib import Path
from typing import Any

try:
    from mcp.server.fastmcp import FastMCP
except ImportError:
    from fastmcp import FastMCP  # type: ignore

mcp = FastMCP("rpa-dev-mcp")

CRAN_FORBIDDEN_CPP = [
    (r"\bstd::cout\b|\bstd::cerr\b", "Avoid std::cout/std::cerr; use cpp11::message or Rprintf."),
    (r"\bprintf\s*\(|\bfprintf\s*\(", "Avoid printf/fprintf; use Rprintf or REprintf."),
    (r"\bexit\s*\(|\babort\s*\(|\bstd::terminate\b", "Never call exit()/abort() in an R package."),
    (r"#pragma\s+comment\s*\(\s*lib", "MSVC-only #pragma comment(lib) is ignored by MinGW-w64; link in Makevars.win."),
    (r"\bkeybd_event\s*\(|\bmouse_event\s*\(", "Deprecated Win32 API; use SendInput() with MOUSEEVENTF_VIRTUALDESK."),
]

UIA_PROBE_CPP = r"""
#define _WIN32_WINNT 0x0A00
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <uiautomation.h>
#include <shellscalingapi.h>

int main() {
    HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (FAILED(hr)) return 1;
    IUIAutomation* uia = nullptr;
    hr = CoCreateInstance(CLSID_CUIAutomation, NULL, CLSCTX_INPROC_SERVER,
                          IID_IUIAutomation, reinterpret_cast<void**>(&uia));
    if (SUCCEEDED(hr) && uia) {
        uia->Release();
        CoUninitialize();
        return 0;
    }
    CoUninitialize();
    return 2;
}
"""


def _resolve_toolchain_env() -> tuple[dict[str, str], str | None, str | None, str | None]:
    """Resolve Windows Rtools45/R-4.5.x toolchain paths and return (env, gxx, r_bin, rscript)."""
    env = os.environ.copy()
    extra_bins: list[str] = []

    if platform.system() == "Windows":
        rtools_home = Path(env.get("RTOOLS45_HOME", r"C:\rtools45"))
        for candidate in (
            rtools_home / "x86_64-w64-mingw32.static.posix" / "bin",
            rtools_home / "ucrt64" / "bin",
            rtools_home / "usr" / "bin",
        ):
            if candidate.is_dir():
                extra_bins.append(str(candidate))

        r_roots = sorted(Path(r"C:\Program Files\R").glob("R-*"), reverse=True)
        for r_root in [Path(r"C:\Program Files\R\R-4.5.2"), *r_roots]:
            r_x64 = r_root / "bin" / "x64"
            if r_x64.is_dir() and str(r_x64) not in extra_bins:
                extra_bins.append(str(r_x64))
                break

        if extra_bins:
            env["PATH"] = os.pathsep.join(extra_bins) + os.pathsep + env.get("PATH", "")

    path_str = env.get("PATH", "")
    gxx = shutil.which("g++", path=path_str)
    r_bin = shutil.which("R", path=path_str)
    rscript = shutil.which("Rscript", path=path_str)
    return env, gxx, r_bin, rscript


def _run_cmd(cmd: list[str], cwd: str | None = None, timeout: int = 120,
             env: dict[str, str] | None = None) -> dict[str, Any]:
    try:
        proc = subprocess.run(
            cmd,
            cwd=cwd,
            capture_output=True,
            text=True,
            timeout=timeout,
            env=env,
        )
        return {
            "returncode": proc.returncode,
            "stdout": proc.stdout[-8000:],
            "stderr": proc.stderr[-8000:],
        }
    except FileNotFoundError:
        return {"returncode": -1, "stdout": "", "stderr": f"Executable not found: {cmd[0]}"}
    except subprocess.TimeoutExpired:
        return {"returncode": -2, "stdout": "", "stderr": f"Command timed out after {timeout}s"}


@mcp.tool()
def probe_rtools_uia_toolchain() -> str:
    """Verify R, Rtools/MinGW-w64 g++, and whether uiautomation.h + CLSID_CUIAutomation
    compile and link cleanly on this machine."""
    env, gxx, _r_bin, rscript = _resolve_toolchain_env()
    report: dict[str, Any] = {
        "os": platform.system(),
        "release": platform.release(),
        "rscript": rscript,
        "gxx": gxx,
        "uia_compile_ok": False,
        "notes": [],
    }
    if rscript:
        report["r_version"] = _run_cmd([rscript, "--version"], timeout=15, env=env)
    if gxx:
        report["gxx_version"] = _run_cmd([gxx, "--version"], timeout=15, env=env)

    if platform.system() != "Windows":
        report["notes"].append(
            "Non-Windows host detected: Windows UIA probe skipped; verify #ifdef _WIN32 stubs compile cleanly."
        )
        return json.dumps(report, indent=2)

    if not gxx:
        report["notes"].append("g++ not found on PATH. Ensure Rtools44/45 toolchain bin is on PATH.")
        return json.dumps(report, indent=2)

    with tempfile.TemporaryDirectory(prefix="winrpa_probe_") as tmp:
        src = Path(tmp) / "probe.cpp"
        exe = Path(tmp) / "probe.exe"
        src.write_text(UIA_PROBE_CPP, encoding="utf-8")
        res = _run_cmd(
            [
                gxx,
                "-std=c++17",
                str(src),
                "-o",
                str(exe),
                "-lole32",
                "-loleaut32",
                "-luuid",
                "-loleacc",
                "-luser32",
                "-lgdi32",
                "-lshcore",
            ],
            timeout=30,
            env=env,
        )
        report["compile_result"] = res
        report["uia_compile_ok"] = res["returncode"] == 0
        if not report["uia_compile_ok"]:
            report["notes"].append(
                "MinGW-w64 failed to compile/link CLSID_CUIAutomation directly. "
                "Define CLSID_CUIAutomation and IID_IUIAutomation explicitly in src/uia_guids.h."
            )
    return json.dumps(report, indent=2)


@mcp.tool()
def audit_cpp_cran_hygiene(src_dir: str = "src") -> str:
    """Scan C/C++ source files for CRAN policy violations and winrpa architectural anti-patterns."""
    base = Path(src_dir)
    if not base.exists():
        return json.dumps({"ok": False, "error": f"Directory not found: {src_dir}"})

    issues: list[dict[str, Any]] = []
    files_scanned = 0
    for ext in ("*.cpp", "*.cc", "*.c", "*.h", "*.hpp"):
        for path in sorted(base.rglob(ext)):
            files_scanned += 1
            text = path.read_text(encoding="utf-8", errors="replace")
            for idx, line in enumerate(text.splitlines(), start=1):
                for pattern, message in CRAN_FORBIDDEN_CPP:
                    if re.search(pattern, line):
                        issues.append({
                            "file": str(path),
                            "line": idx,
                            "severity": "error",
                            "message": message,
                            "snippet": line.strip()[:120],
                        })
            if "SendInput" in text and "MOUSEEVENTF_ABSOLUTE" in text and "MOUSEEVENTF_VIRTUALDESK" not in text:
                issues.append({
                    "file": str(path),
                    "line": 0,
                    "severity": "warning",
                    "message": "SendInput uses MOUSEEVENTF_ABSOLUTE without MOUSEEVENTF_VIRTUALDESK (breaks multi-monitor).",
                })
            if "SetProcessDpiAwareness" in text and "E_ACCESSDENIED" not in text and "already" not in text.lower():
                issues.append({
                    "file": str(path),
                    "line": 0,
                    "severity": "warning",
                    "message": "SetProcessDpiAwareness* may fail with E_ACCESSDENIED under RStudio/Rgui; handle gracefully.",
                })

    return json.dumps({
        "ok": len([i for i in issues if i["severity"] == "error"]) == 0,
        "files_scanned": files_scanned,
        "issue_count": len(issues),
        "issues": issues,
    }, indent=2)


@mcp.tool()
def inspect_active_windows_and_dpi() -> str:
    """Inspect virtual desktop bounds, monitor metrics, and interactive desktop status on Windows."""
    if platform.system() != "Windows":
        return json.dumps({"os": platform.system(), "interactive_desktop": False, "note": "Windows-only inspection."})

    import ctypes  # local import

    user32 = ctypes.windll.user32
    metrics = {
        "virtual_x": user32.GetSystemMetrics(76),   # SM_XVIRTUALSCREEN
        "virtual_y": user32.GetSystemMetrics(77),   # SM_YVIRTUALSCREEN
        "virtual_w": user32.GetSystemMetrics(78),   # SM_CXVIRTUALSCREEN
        "virtual_h": user32.GetSystemMetrics(79),   # SM_CYVIRTUALSCREEN
        "monitor_count": user32.GetSystemMetrics(80),  # SM_CMONITORS
        "primary_w": user32.GetSystemMetrics(0),
        "primary_h": user32.GetSystemMetrics(1),
    }
    hdesk = user32.OpenInputDesktop(0, False, 0x0001)
    interactive = bool(hdesk)
    if hdesk:
        user32.CloseDesktop(hdesk)

    return json.dumps({
        "os": "Windows",
        "interactive_desktop": interactive,
        "metrics": metrics,
    }, indent=2)


@mcp.tool()
def run_r_package_check(pkg_path: str = ".", as_cran: bool = True) -> str:
    """Build source tarball and run R CMD check (--as-cran) with CRAN core limits enabled."""
    env, _gxx, r_bin, _rscript = _resolve_toolchain_env()
    if not r_bin:
        return json.dumps({"ok": False, "error": "R executable not found on PATH."})

    pkg = Path(pkg_path).resolve()
    if not (pkg / "DESCRIPTION").exists():
        return json.dumps({"ok": False, "error": f"No DESCRIPTION file found in {pkg}"})

    env["_R_CHECK_LIMIT_CORES_"] = "TRUE"
    env["_R_CHECK_FORCE_SUGGESTS_"] = "FALSE"

    with tempfile.TemporaryDirectory(prefix="winrpa_check_") as tmp:
        build_res = _run_cmd([r_bin, "CMD", "build", "--no-manual", str(pkg)], cwd=tmp, timeout=180, env=env)
        if build_res["returncode"] != 0:
            return json.dumps({"ok": False, "stage": "build", "result": build_res}, indent=2)

        tarballs = list(Path(tmp).glob("*.tar.gz"))
        if not tarballs:
            return json.dumps({"ok": False, "stage": "build", "error": "No tarball produced."})

        check_cmd = [r_bin, "CMD", "check", "--no-manual"]
        if as_cran:
            check_cmd.append("--as-cran")
        check_cmd.append(str(tarballs[0]))

        check_res = _run_cmd(check_cmd, cwd=tmp, timeout=300, env=env)
        check_log = ""
        for log_file in Path(tmp).glob("*.Rcheck/00check.log"):
            check_log = log_file.read_text(encoding="utf-8", errors="replace")[-8000:]
            break
        return json.dumps({
            "ok": check_res["returncode"] == 0,
            "stage": "check",
            "tarball": tarballs[0].name,
            "result": check_res,
            "check_log": check_log,
        }, indent=2)


if __name__ == "__main__":
    mcp.run()
