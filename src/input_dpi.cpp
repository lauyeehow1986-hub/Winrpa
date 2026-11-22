#ifndef R_NO_REMAP
#define R_NO_REMAP
#endif
#include <cpp11.hpp>
#include "mta_worker.h"
#include "winrpa_core.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <shellscalingapi.h>

namespace winrpa {

namespace {

struct MonitorEntry {
  std::string device;
  int left = 0;
  int top = 0;
  int right = 0;
  int bottom = 0;
  int work_left = 0;
  int work_top = 0;
  int work_right = 0;
  int work_bottom = 0;
  int dpi_x = 96;
  int dpi_y = 96;
  bool primary = false;
};

BOOL CALLBACK enum_monitors_proc(
    HMONITOR hmon, HDC, LPRECT, LPARAM lparam) {
  auto* list = reinterpret_cast<std::vector<MonitorEntry>*>(lparam);
  if (list == nullptr) {
    return FALSE;
  }
  MONITORINFOEXW mi{};
  mi.cbSize = sizeof(mi);
  if (!GetMonitorInfoW(hmon, &mi)) {
    return TRUE;
  }
  UINT dx = 96U;
  UINT dy = 96U;
  if (FAILED(GetDpiForMonitor(hmon, MDT_EFFECTIVE_DPI, &dx, &dy))) {
    dx = 96U;
    dy = 96U;
  }
  MonitorEntry entry;
  entry.device = wide_to_utf8(mi.szDevice);
  entry.left = static_cast<int>(mi.rcMonitor.left);
  entry.top = static_cast<int>(mi.rcMonitor.top);
  entry.right = static_cast<int>(mi.rcMonitor.right);
  entry.bottom = static_cast<int>(mi.rcMonitor.bottom);
  entry.work_left = static_cast<int>(mi.rcWork.left);
  entry.work_top = static_cast<int>(mi.rcWork.top);
  entry.work_right = static_cast<int>(mi.rcWork.right);
  entry.work_bottom = static_cast<int>(mi.rcWork.bottom);
  entry.dpi_x = static_cast<int>(dx);
  entry.dpi_y = static_cast<int>(dy);
  entry.primary = (mi.dwFlags & MONITORINFOF_PRIMARY) != 0U;
  list->push_back(std::move(entry));
  return TRUE;
}

std::string dpi_awareness_name(DPI_AWARENESS aw) {
  switch (aw) {
    case DPI_AWARENESS_UNAWARE:
      return "unaware";
    case DPI_AWARENESS_SYSTEM_AWARE:
      return "system";
    case DPI_AWARENESS_PER_MONITOR_AWARE:
      return "per_monitor";
    default:
      return "unknown";
  }
}

void send_single_input_or_stop(const INPUT& inp) {
  INPUT copy = inp;
  const UINT sent = SendInput(1U, &copy, sizeof(INPUT));
  if (sent != 1U) {
    cpp11::stop("SendInput failed (Win32 error %lu).", GetLastError());
  }
}

void send_mouse_abs_move(int x, int y) {
  const int vx = GetSystemMetrics(SM_XVIRTUALSCREEN);
  const int vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
  const int vw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
  const int vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
  const auto coord = normalize_virtual_desktop(x, y, vx, vy, vw, vh);

  INPUT inp{};
  inp.type = INPUT_MOUSE;
  inp.mi.dx = static_cast<LONG>(coord.norm_x);
  inp.mi.dy = static_cast<LONG>(coord.norm_y);
  inp.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;
  send_single_input_or_stop(inp);
}

bool is_extended_vk(std::uint16_t vk) {
  switch (vk) {
    case VK_RCONTROL:
    case VK_RMENU:
    case VK_INSERT:
    case VK_DELETE:
    case VK_HOME:
    case VK_END:
    case VK_PRIOR:
    case VK_NEXT:
    case VK_LEFT:
    case VK_RIGHT:
    case VK_UP:
    case VK_DOWN:
    case VK_NUMLOCK:
    case VK_DIVIDE:
    case VK_LWIN:
    case VK_RWIN:
    case VK_APPS:
      return true;
    default:
      return false;
  }
}

void emit_vk_event(std::uint16_t vk, bool key_up, bool use_scancode) {
  INPUT inp{};
  inp.type = INPUT_KEYBOARD;
  const DWORD ext_flag = is_extended_vk(vk) ? KEYEVENTF_EXTENDEDKEY : 0U;
  const DWORD up_flag = key_up ? KEYEVENTF_KEYUP : 0U;

  if (use_scancode) {
    const HKL layout = GetKeyboardLayout(0);
    const UINT scan = MapVirtualKeyExW(vk, MAPVK_VK_TO_VSC, layout);
    if (scan != 0U) {
      inp.ki.wVk = 0U;
      inp.ki.wScan = static_cast<WORD>(scan);
      inp.ki.dwFlags = KEYEVENTF_SCANCODE | ext_flag | up_flag;
      send_single_input_or_stop(inp);
      return;
    }
  }
  inp.ki.wVk = static_cast<WORD>(vk);
  inp.ki.wScan = 0U;
  inp.ki.dwFlags = ext_flag | up_flag;
  send_single_input_or_stop(inp);
}

void emit_unicode_char(wchar_t ch) {
  INPUT pair[2]{};
  pair[0].type = INPUT_KEYBOARD;
  pair[0].ki.wVk = 0U;
  pair[0].ki.wScan = static_cast<WORD>(ch);
  pair[0].ki.dwFlags = KEYEVENTF_UNICODE;

  pair[1].type = INPUT_KEYBOARD;
  pair[1].ki.wVk = 0U;
  pair[1].ki.wScan = static_cast<WORD>(ch);
  pair[1].ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;

  const UINT sent = SendInput(2U, pair, sizeof(INPUT));
  if (sent != 2U) {
    cpp11::stop("SendInput(KEYEVENTF_UNICODE) failed (Win32 error %lu).", GetLastError());
  }
}

bool is_modifier_vk(std::uint16_t vk) {
  switch (vk) {
    case VK_SHIFT:
    case VK_LSHIFT:
    case VK_RSHIFT:
    case VK_CONTROL:
    case VK_LCONTROL:
    case VK_RCONTROL:
    case VK_MENU:
    case VK_LMENU:
    case VK_RMENU:
    case VK_LWIN:
    case VK_RWIN:
      return true;
    default:
      return false;
  }
}

std::string lower_ascii(std::string s) {
  for (char& ch : s) {
    ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  }
  return s;
}

std::uint16_t resolve_key_name(const std::string& raw_name) {
  const std::string k = lower_ascii(raw_name);
  if (k == "ctrl" || k == "control" || k == "lctrl") return VK_LCONTROL;
  if (k == "rctrl" || k == "rcontrol") return VK_RCONTROL;
  if (k == "alt" || k == "menu" || k == "lalt") return VK_LMENU;
  if (k == "ralt") return VK_RMENU;
  if (k == "shift" || k == "lshift") return VK_LSHIFT;
  if (k == "rshift") return VK_RSHIFT;
  if (k == "win" || k == "super" || k == "lwin") return VK_LWIN;
  if (k == "rwin") return VK_RWIN;
  if (k == "enter" || k == "return") return VK_RETURN;
  if (k == "tab") return VK_TAB;
  if (k == "esc" || k == "escape") return VK_ESCAPE;
  if (k == "space") return VK_SPACE;
  if (k == "backspace" || k == "bksp") return VK_BACK;
  if (k == "delete" || k == "del") return VK_DELETE;
  if (k == "insert" || k == "ins") return VK_INSERT;
  if (k == "home") return VK_HOME;
  if (k == "end") return VK_END;
  if (k == "pageup" || k == "pgup") return VK_PRIOR;
  if (k == "pagedown" || k == "pgdn") return VK_NEXT;
  if (k == "left") return VK_LEFT;
  if (k == "right") return VK_RIGHT;
  if (k == "up") return VK_UP;
  if (k == "down") return VK_DOWN;
  if (k == "capslock") return VK_CAPITAL;
  if (k == "printscreen" || k == "prtsc") return VK_SNAPSHOT;

  if (k.size() >= 2U && k[0] == 'f') {
    bool all_digits = true;
    for (std::size_t i = 1; i < k.size(); ++i) {
      if (!std::isdigit(static_cast<unsigned char>(k[i]))) {
        all_digits = false;
        break;
      }
    }
    if (all_digits) {
      const int fnum = std::stoi(k.substr(1));
      if (fnum >= 1 && fnum <= 24) {
        return static_cast<std::uint16_t>(VK_F1 + (fnum - 1));
      }
    }
  }

  if (raw_name.size() == 1U) {
    const unsigned char c = static_cast<unsigned char>(raw_name[0]);
    if (std::isalpha(c)) {
      return static_cast<std::uint16_t>(std::toupper(c));
    }
    if (std::isdigit(c)) {
      return static_cast<std::uint16_t>(c);
    }
    const SHORT vk_state = VkKeyScanExW(static_cast<wchar_t>(c), GetKeyboardLayout(0));
    if (vk_state != -1) {
      return static_cast<std::uint16_t>(LOBYTE(vk_state));
    }
  }

  cpp11::stop("Unknown key name in chord: '%s'.", raw_name.c_str());
  return 0U;
}

class ClipboardGuard {
 public:
  ClipboardGuard() {
    for (int attempt = 0; attempt < 5; ++attempt) {
      if (OpenClipboard(nullptr)) {
        open_ = true;
        return;
      }
      Sleep(15U);
    }
    cpp11::stop(
        "Failed to open Windows clipboard after 5 retry attempts (Win32 error %lu).",
        GetLastError());
  }
  ClipboardGuard(const ClipboardGuard&) = delete;
  ClipboardGuard& operator=(const ClipboardGuard&) = delete;
  ~ClipboardGuard() {
    if (open_) {
      CloseClipboard();
    }
  }

 private:
  bool open_ = false;
};

struct GlobalLockGuard {
  HGLOBAL handle = nullptr;
  void* ptr = nullptr;
  explicit GlobalLockGuard(HGLOBAL h) : handle(h), ptr(h ? GlobalLock(h) : nullptr) {}
  GlobalLockGuard(const GlobalLockGuard&) = delete;
  GlobalLockGuard& operator=(const GlobalLockGuard&) = delete;
  ~GlobalLockGuard() {
    if (handle != nullptr && ptr != nullptr) {
      GlobalUnlock(handle);
    }
  }
};

void interruptible_sleep_ms(int total_ms) {
  int remaining = total_ms;
  while (remaining > 0) {
    check_abort_or_interrupt();
    const int chunk = remaining > 50 ? 50 : remaining;
    Sleep(static_cast<DWORD>(chunk));
    remaining -= chunk;
  }
}

}  // namespace

}  // namespace winrpa

#endif  // _WIN32

[[cpp11::register]]
cpp11::writable::list cpp_get_display_metrics() {
#ifdef _WIN32
  std::vector<winrpa::MonitorEntry> mons;
  EnumDisplayMonitors(nullptr, nullptr, winrpa::enum_monitors_proc, reinterpret_cast<LPARAM>(&mons));

  const R_xlen_t n = static_cast<R_xlen_t>(mons.size());
  cpp11::writable::strings device(n);
  cpp11::writable::integers left(n);
  cpp11::writable::integers top(n);
  cpp11::writable::integers right(n);
  cpp11::writable::integers bottom(n);
  cpp11::writable::integers work_left(n);
  cpp11::writable::integers work_top(n);
  cpp11::writable::integers work_right(n);
  cpp11::writable::integers work_bottom(n);
  cpp11::writable::integers dpi_x(n);
  cpp11::writable::integers dpi_y(n);
  cpp11::writable::logicals primary(n);

  for (R_xlen_t i = 0; i < n; ++i) {
    const auto& m = mons[static_cast<std::size_t>(i)];
    device[i] = m.device;
    left[i] = m.left;
    top[i] = m.top;
    right[i] = m.right;
    bottom[i] = m.bottom;
    work_left[i] = m.work_left;
    work_top[i] = m.work_top;
    work_right[i] = m.work_right;
    work_bottom[i] = m.work_bottom;
    dpi_x[i] = m.dpi_x;
    dpi_y[i] = m.dpi_y;
    primary[i] = m.primary;
  }

  const DPI_AWARENESS_CONTEXT ctx = GetThreadDpiAwarenessContext();
  const DPI_AWARENESS aw = GetAwarenessFromDpiAwarenessContext(ctx);

  using namespace cpp11::literals;
  cpp11::writable::list vdesk({
      "virtual_x"_nm = GetSystemMetrics(SM_XVIRTUALSCREEN),
      "virtual_y"_nm = GetSystemMetrics(SM_YVIRTUALSCREEN),
      "virtual_w"_nm = GetSystemMetrics(SM_CXVIRTUALSCREEN),
      "virtual_h"_nm = GetSystemMetrics(SM_CYVIRTUALSCREEN),
      "monitor_count"_nm = GetSystemMetrics(SM_CMONITORS),
      "primary_w"_nm = GetSystemMetrics(SM_CXSCREEN),
      "primary_h"_nm = GetSystemMetrics(SM_CYSCREEN)
  });

  cpp11::writable::data_frame mon_df({
      "device"_nm = device,
      "left"_nm = left,
      "top"_nm = top,
      "right"_nm = right,
      "bottom"_nm = bottom,
      "work_left"_nm = work_left,
      "work_top"_nm = work_top,
      "work_right"_nm = work_right,
      "work_bottom"_nm = work_bottom,
      "dpi_x"_nm = dpi_x,
      "dpi_y"_nm = dpi_y,
      "primary"_nm = primary
  });

  return cpp11::writable::list({
      "virtual_desktop"_nm = vdesk,
      "dpi_awareness"_nm = winrpa::dpi_awareness_name(aw),
      "monitors"_nm = mon_df
  });
#else
  winrpa::require_windows_or_stop("cpp_get_display_metrics()");
  return cpp11::writable::list();
#endif
}

[[cpp11::register]]
cpp11::writable::list cpp_try_enable_per_monitor_dpi() {
#ifdef _WIN32
  using SetCtxFn = BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT);
  HMODULE user32 = GetModuleHandleW(L"user32.dll");
  SetCtxFn set_ctx = nullptr;
  if (user32 != nullptr) {
    FARPROC proc = GetProcAddress(user32, "SetProcessDpiAwarenessContext");
    if (proc != nullptr) {
      std::memcpy(&set_ctx, &proc, sizeof(set_ctx));
    }
  }

  bool ok = false;
  bool already_locked = false;
  std::string msg;

  if (set_ctx != nullptr) {
    SetLastError(0U);
    if (set_ctx(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) {
      ok = true;
      msg = "Enabled Per-Monitor V2 DPI awareness.";
    } else {
      const DWORD err = GetLastError();
      if (err == ERROR_ACCESS_DENIED ||
          static_cast<HRESULT>(err) == E_ACCESSDENIED) {
        already_locked = true;
        msg = "Process DPI awareness is already locked by host manifest (E_ACCESSDENIED).";
      } else {
        msg = "SetProcessDpiAwarenessContext returned FALSE (Win32 error " +
              std::to_string(err) + ").";
      }
    }
  } else {
    msg = "SetProcessDpiAwarenessContext unavailable on this Windows build.";
  }

  const DPI_AWARENESS aw =
      GetAwarenessFromDpiAwarenessContext(GetThreadDpiAwarenessContext());
  using namespace cpp11::literals;
  return cpp11::writable::list({
      "success"_nm = ok,
      "already_locked"_nm = already_locked,
      "awareness"_nm = winrpa::dpi_awareness_name(aw),
      "message"_nm = msg
  });
#else
  winrpa::require_windows_or_stop("cpp_try_enable_per_monitor_dpi()");
  return cpp11::writable::list();
#endif
}

[[cpp11::register]]
void cpp_mouse_move(int x, int y) {
#ifdef _WIN32
  winrpa::check_abort_or_interrupt();
  winrpa::send_mouse_abs_move(x, y);
#else
  (void)x;
  (void)y;
  winrpa::require_windows_or_stop("cpp_mouse_move()");
#endif
}

[[cpp11::register]]
void cpp_mouse_click(int x, int y, std::string button, int clicks, bool move_first) {
#ifdef _WIN32
  winrpa::check_abort_or_interrupt();
  if (clicks <= 0) {
    cpp11::stop("clicks must be >= 1.");
  }
  DWORD down_flag = MOUSEEVENTF_LEFTDOWN;
  DWORD up_flag = MOUSEEVENTF_LEFTUP;
  if (button == "right") {
    down_flag = MOUSEEVENTF_RIGHTDOWN;
    up_flag = MOUSEEVENTF_RIGHTUP;
  } else if (button == "middle") {
    down_flag = MOUSEEVENTF_MIDDLEDOWN;
    up_flag = MOUSEEVENTF_MIDDLEUP;
  } else if (button != "left") {
    cpp11::stop("Unsupported mouse button: '%s'.", button.c_str());
  }

  if (move_first) {
    winrpa::send_mouse_abs_move(x, y);
  }

  for (int i = 0; i < clicks; ++i) {
    winrpa::check_abort_or_interrupt();
    INPUT pair[2]{};
    pair[0].type = INPUT_MOUSE;
    pair[0].mi.dwFlags = down_flag;
    pair[1].type = INPUT_MOUSE;
    pair[1].mi.dwFlags = up_flag;
    if (SendInput(2U, pair, sizeof(INPUT)) != 2U) {
      cpp11::stop("SendInput mouse click failed (Win32 error %lu).", GetLastError());
    }
    if (i + 1 < clicks) {
      Sleep(40U);
    }
  }
#else
  (void)x;
  (void)y;
  (void)button;
  (void)clicks;
  (void)move_first;
  winrpa::require_windows_or_stop("cpp_mouse_click()");
#endif
}

[[cpp11::register]]
void cpp_mouse_scroll(int delta, bool horizontal) {
#ifdef _WIN32
  winrpa::check_abort_or_interrupt();
  INPUT inp{};
  inp.type = INPUT_MOUSE;
  inp.mi.dwFlags = horizontal ? MOUSEEVENTF_HWHEEL : MOUSEEVENTF_WHEEL;
  inp.mi.mouseData = static_cast<DWORD>(static_cast<std::int32_t>(delta));
  winrpa::send_single_input_or_stop(inp);
#else
  (void)delta;
  (void)horizontal;
  winrpa::require_windows_or_stop("cpp_mouse_scroll()");
#endif
}

[[cpp11::register]]
cpp11::writable::integers cpp_mouse_position() {
#ifdef _WIN32
  POINT pt{0, 0};
  if (!GetCursorPos(&pt)) {
    cpp11::stop("GetCursorPos failed (Win32 error %lu).", GetLastError());
  }
  cpp11::writable::integers out(2);
  out[0] = static_cast<int>(pt.x);
  out[1] = static_cast<int>(pt.y);
  return out;
#else
  winrpa::require_windows_or_stop("cpp_mouse_position()");
  return cpp11::writable::integers();
#endif
}

[[cpp11::register]]
void cpp_type_text(std::string text, std::string mode, int delay_ms) {
#ifdef _WIN32
  winrpa::check_abort_or_interrupt();
  const bool use_scancode = (mode == "scancode");
  if (!use_scancode && mode != "unicode") {
    cpp11::stop("Unsupported typing mode: '%s'.", mode.c_str());
  }

  const std::wstring wide = winrpa::utf8_to_wide(text);
  const HKL layout = GetKeyboardLayout(0);

  for (std::size_t i = 0; i < wide.size(); ++i) {
    if ((i % 16U) == 0U) {
      winrpa::check_abort_or_interrupt();
    }
    const wchar_t ch = wide[i];

    if (ch == L'\n') {
      winrpa::emit_vk_event(VK_RETURN, false, use_scancode);
      winrpa::emit_vk_event(VK_RETURN, true, use_scancode);
    } else if (ch == L'\t') {
      winrpa::emit_vk_event(VK_TAB, false, use_scancode);
      winrpa::emit_vk_event(VK_TAB, true, use_scancode);
    } else if (!use_scancode) {
      winrpa::emit_unicode_char(ch);
    } else {
      const SHORT vk_state = VkKeyScanExW(ch, layout);
      if (vk_state == -1) {
        winrpa::emit_unicode_char(ch);
      } else {
        const std::uint16_t vk = static_cast<std::uint16_t>(LOBYTE(vk_state));
        const BYTE mods = HIBYTE(vk_state);
        winrpa::ModifierGuard guard([](std::uint16_t mod_vk) {
          winrpa::emit_vk_event(mod_vk, true, true);
        });
        if ((mods & 1U) != 0U) {
          winrpa::emit_vk_event(VK_LSHIFT, false, true);
          guard.push(VK_LSHIFT);
        }
        if ((mods & 2U) != 0U) {
          winrpa::emit_vk_event(VK_LCONTROL, false, true);
          guard.push(VK_LCONTROL);
        }
        if ((mods & 4U) != 0U) {
          winrpa::emit_vk_event(VK_LMENU, false, true);
          guard.push(VK_LMENU);
        }
        winrpa::emit_vk_event(vk, false, true);
        winrpa::emit_vk_event(vk, true, true);
        guard.release_all();
      }
    }

    if (delay_ms > 0) {
      winrpa::interruptible_sleep_ms(delay_ms);
    }
  }
#else
  (void)text;
  (void)mode;
  (void)delay_ms;
  winrpa::require_windows_or_stop("cpp_type_text()");
#endif
}

[[cpp11::register]]
void cpp_key_chord(cpp11::strings keys, bool use_scancode) {
#ifdef _WIN32
  winrpa::check_abort_or_interrupt();
  if (keys.size() == 0) {
    cpp11::stop("keys must contain at least one key name.");
  }
  std::vector<std::uint16_t> vks;
  vks.reserve(static_cast<std::size_t>(keys.size()));
  for (R_xlen_t i = 0; i < keys.size(); ++i) {
    const std::string k = std::string(keys[i]);
    if (k.empty()) {
      cpp11::stop("Key names in chord must be non-empty strings.");
    }
    vks.push_back(winrpa::resolve_key_name(k));
  }

  winrpa::ModifierGuard guard([use_scancode](std::uint16_t mod_vk) {
    winrpa::emit_vk_event(mod_vk, true, use_scancode);
  });

  std::vector<std::uint16_t> non_mods;
  for (std::uint16_t vk : vks) {
    winrpa::emit_vk_event(vk, false, use_scancode);
    if (winrpa::is_modifier_vk(vk)) {
      guard.push(vk);
    } else {
      non_mods.push_back(vk);
    }
  }
  for (auto it = non_mods.rbegin(); it != non_mods.rend(); ++it) {
    winrpa::emit_vk_event(*it, true, use_scancode);
  }
  guard.release_all();
#else
  (void)keys;
  (void)use_scancode;
  winrpa::require_windows_or_stop("cpp_key_chord()");
#endif
}

[[cpp11::register]]
SEXP cpp_clipboard_get_text() {
#ifdef _WIN32
  winrpa::ClipboardGuard clip;
  if (!IsClipboardFormatAvailable(CF_UNICODETEXT)) {
    return R_NilValue;
  }
  HANDLE hdata = GetClipboardData(CF_UNICODETEXT);
  if (hdata == nullptr) {
    return R_NilValue;
  }
  winrpa::GlobalLockGuard lock(static_cast<HGLOBAL>(hdata));
  if (lock.ptr == nullptr) {
    return R_NilValue;
  }
  const auto* wstr = static_cast<const wchar_t*>(lock.ptr);
  const std::string utf8 = winrpa::wide_to_utf8(wstr);
  return cpp11::as_sexp(utf8);
#else
  winrpa::require_windows_or_stop("cpp_clipboard_get_text()");
  return R_NilValue;
#endif
}

[[cpp11::register]]
void cpp_clipboard_set_text(std::string text) {
#ifdef _WIN32
  winrpa::ClipboardGuard clip;
  if (!EmptyClipboard()) {
    cpp11::stop("EmptyClipboard failed (Win32 error %lu).", GetLastError());
  }
  const std::wstring wide = winrpa::utf8_to_wide(text);
  const std::size_t bytes = (wide.size() + 1U) * sizeof(wchar_t);
  HGLOBAL hmem = GlobalAlloc(GMEM_MOVEABLE, bytes);
  if (hmem == nullptr) {
    cpp11::stop("GlobalAlloc failed while writing to clipboard.");
  }
  {
    winrpa::GlobalLockGuard lock(hmem);
    if (lock.ptr == nullptr) {
      GlobalFree(hmem);
      cpp11::stop("GlobalLock failed while writing to clipboard.");
    }
    std::memcpy(lock.ptr, wide.c_str(), bytes);
  }
  if (SetClipboardData(CF_UNICODETEXT, hmem) == nullptr) {
    GlobalFree(hmem);
    cpp11::stop("SetClipboardData failed (Win32 error %lu).", GetLastError());
  }
#else
  (void)text;
  winrpa::require_windows_or_stop("cpp_clipboard_set_text()");
#endif
}

[[cpp11::register]]
int cpp_clipboard_sequence_number() {
#ifdef _WIN32
  const DWORD seq = GetClipboardSequenceNumber();
  return static_cast<int>(seq & 0x7FFFFFFFU);
#else
  winrpa::require_windows_or_stop("cpp_clipboard_sequence_number()");
  return 0;
#endif
}
