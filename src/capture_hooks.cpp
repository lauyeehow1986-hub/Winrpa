#ifndef R_NO_REMAP
#define R_NO_REMAP
#endif
#include <cpp11.hpp>
#include "capture_hooks.h"
#include "mta_worker.h"
#include "winrpa_core.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <future>
#include <mutex>
#include <string>
#include <thread>
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

namespace winrpa {

namespace {

HookEventRingBuffer g_hook_ring;

LRESULT CALLBACK low_level_keyboard_proc(int nCode, WPARAM wParam, LPARAM lParam) {
  if (nCode == HC_ACTION && lParam != 0) {
    const auto* kb = reinterpret_cast<const KBDLLHOOKSTRUCT*>(lParam);
    HookEvent ev{};
    ev.vk_code = static_cast<std::uint32_t>(kb->vkCode);
    ev.scan_code = static_cast<std::uint32_t>(kb->scanCode);
    ev.key_up = (wParam == WM_KEYUP || wParam == WM_SYSKEYUP);
    ev.timestamp_ms = static_cast<std::int64_t>(kb->time);
    g_hook_ring.push(ev);
  }
  return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

class RuntimeHookThread {
 public:
  static constexpr UINT kMsgRegisterHotkey = WM_APP + 1;
  static constexpr UINT kMsgUnregisterHotkey = WM_APP + 2;
  static constexpr UINT kMsgStartHook = WM_APP + 3;
  static constexpr UINT kMsgStopHook = WM_APP + 4;
  static constexpr int kKillswitchId = 0x5752;

  struct CmdPayload {
    UINT modifiers = 0U;
    UINT vk = 0U;
    bool ok = false;
    DWORD err = 0U;
    std::promise<void> done;
  };

  static RuntimeHookThread& instance() {
    static RuntimeHookThread inst;
    return inst;
  }

  bool enable_killswitch(UINT modifiers, UINT vk, DWORD& err_out) {
    ensure_running();
    CmdPayload cmd;
    cmd.modifiers = modifiers;
    cmd.vk = vk;
    auto fut = cmd.done.get_future();
    if (!PostThreadMessageW(tid_, kMsgRegisterHotkey, 0U, reinterpret_cast<LPARAM>(&cmd))) {
      err_out = GetLastError();
      return false;
    }
    fut.wait();
    err_out = cmd.err;
    return cmd.ok;
  }

  bool disable_killswitch() {
    std::lock_guard<std::mutex> lock(mu_);
    if (!running_) {
      return true;
    }
    CmdPayload cmd;
    auto fut = cmd.done.get_future();
    if (!PostThreadMessageW(tid_, kMsgUnregisterHotkey, 0U, reinterpret_cast<LPARAM>(&cmd))) {
      return false;
    }
    fut.wait();
    return cmd.ok;
  }

  bool start_keyboard_hook(DWORD& err_out) {
    ensure_running();
    CmdPayload cmd;
    auto fut = cmd.done.get_future();
    if (!PostThreadMessageW(tid_, kMsgStartHook, 0U, reinterpret_cast<LPARAM>(&cmd))) {
      err_out = GetLastError();
      return false;
    }
    fut.wait();
    err_out = cmd.err;
    return cmd.ok;
  }

  bool stop_keyboard_hook() {
    std::lock_guard<std::mutex> lock(mu_);
    if (!running_) {
      return true;
    }
    CmdPayload cmd;
    auto fut = cmd.done.get_future();
    if (!PostThreadMessageW(tid_, kMsgStopHook, 0U, reinterpret_cast<LPARAM>(&cmd))) {
      return false;
    }
    fut.wait();
    return cmd.ok;
  }

  void shutdown() {
    std::thread to_join;
    {
      std::lock_guard<std::mutex> lock(mu_);
      if (!running_) {
        return;
      }
      PostThreadMessageW(tid_, WM_QUIT, 0U, 0);
      to_join = std::move(thread_);
      running_ = false;
      tid_ = 0U;
    }
    if (to_join.joinable()) {
      to_join.join();
    }
  }

  ~RuntimeHookThread() {
    try {
      shutdown();
    } catch (...) {
    }
  }

 private:
  RuntimeHookThread() = default;

  void ensure_running() {
    std::lock_guard<std::mutex> lock(mu_);
    if (running_) {
      return;
    }
    std::promise<DWORD> ready;
    auto fut = ready.get_future();
    thread_ = std::thread(&RuntimeHookThread::loop, this, std::move(ready));
    tid_ = fut.get();
    running_ = true;
  }

  void loop(std::promise<DWORD> ready) {
    MSG msg{};
    PeekMessageW(&msg, nullptr, WM_USER, WM_USER, PM_NOREMOVE);
    ready.set_value(GetCurrentThreadId());

    bool hotkey_active = false;
    HHOOK kbd_hook = nullptr;

    BOOL gm = FALSE;
    while ((gm = GetMessageW(&msg, nullptr, 0U, 0U)) != 0) {
      if (gm == -1) {
        break;
      }
      if (msg.message == WM_HOTKEY && static_cast<int>(msg.wParam) == kKillswitchId) {
        g_abort_requested.store(true, std::memory_order_release);
        continue;
      }
      if (msg.message == kMsgRegisterHotkey) {
        auto* cmd = reinterpret_cast<CmdPayload*>(msg.lParam);
        if (hotkey_active) {
          UnregisterHotKey(nullptr, kKillswitchId);
          hotkey_active = false;
        }
        SetLastError(0U);
        const BOOL ok = RegisterHotKey(
            nullptr, kKillswitchId, cmd->modifiers | MOD_NOREPEAT, cmd->vk);
        cmd->ok = (ok != FALSE);
        cmd->err = cmd->ok ? 0U : GetLastError();
        if (cmd->ok) {
          hotkey_active = true;
        }
        cmd->done.set_value();
        continue;
      }
      if (msg.message == kMsgUnregisterHotkey) {
        auto* cmd = reinterpret_cast<CmdPayload*>(msg.lParam);
        if (hotkey_active) {
          UnregisterHotKey(nullptr, kKillswitchId);
          hotkey_active = false;
        }
        cmd->ok = true;
        cmd->err = 0U;
        cmd->done.set_value();
        continue;
      }
      if (msg.message == kMsgStartHook) {
        auto* cmd = reinterpret_cast<CmdPayload*>(msg.lParam);
        if (kbd_hook == nullptr) {
          HMODULE hmod = nullptr;
          GetModuleHandleExW(
              GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                  GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
              reinterpret_cast<LPCWSTR>(&low_level_keyboard_proc),
              &hmod);
          SetLastError(0U);
          kbd_hook = SetWindowsHookExW(
              WH_KEYBOARD_LL, low_level_keyboard_proc, hmod, 0U);
          cmd->ok = (kbd_hook != nullptr);
          cmd->err = cmd->ok ? 0U : GetLastError();
        } else {
          cmd->ok = true;
          cmd->err = 0U;
        }
        cmd->done.set_value();
        continue;
      }
      if (msg.message == kMsgStopHook) {
        auto* cmd = reinterpret_cast<CmdPayload*>(msg.lParam);
        if (kbd_hook != nullptr) {
          UnhookWindowsHookEx(kbd_hook);
          kbd_hook = nullptr;
        }
        cmd->ok = true;
        cmd->err = 0U;
        cmd->done.set_value();
        continue;
      }
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }

    if (hotkey_active) {
      UnregisterHotKey(nullptr, kKillswitchId);
    }
    if (kbd_hook != nullptr) {
      UnhookWindowsHookEx(kbd_hook);
    }
  }

  std::mutex mu_;
  std::thread thread_;
  DWORD tid_ = 0U;
  bool running_ = false;
};

struct DcReleaseGuard {
  HWND hwnd = nullptr;
  HDC hdc = nullptr;
  ~DcReleaseGuard() {
    if (hdc != nullptr) {
      ReleaseDC(hwnd, hdc);
    }
  }
};

struct MemDcGuard {
  HDC hdc = nullptr;
  ~MemDcGuard() {
    if (hdc != nullptr) {
      DeleteDC(hdc);
    }
  }
};

struct BitmapGuard {
  HBITMAP hbm = nullptr;
  ~BitmapGuard() {
    if (hbm != nullptr) {
      DeleteObject(hbm);
    }
  }
};

struct SelectObjGuard {
  HDC hdc = nullptr;
  HGDIOBJ old = nullptr;
  ~SelectObjGuard() {
    if (hdc != nullptr && old != nullptr) {
      SelectObject(hdc, old);
    }
  }
};

}  // namespace

void stop_runtime_hooks() {
  RuntimeHookThread::instance().shutdown();
}

}  // namespace winrpa

#else

namespace winrpa {

void stop_runtime_hooks() {}

}  // namespace winrpa

#endif  // _WIN32

[[cpp11::register]]
std::string cpp_capture_window_bmp(std::string hwnd_hex, std::string out_path) {
#ifdef _WIN32
  HWND target = nullptr;
  int cap_x = 0;
  int cap_y = 0;
  int width = 0;
  int height = 0;

  if (!hwnd_hex.empty()) {
    std::uintptr_t addr = 0U;
    try {
      addr = static_cast<std::uintptr_t>(std::stoull(hwnd_hex, nullptr, 0));
    } catch (...) {
      cpp11::stop("Invalid hwnd hex string: '%s'.", hwnd_hex.c_str());
    }
    target = reinterpret_cast<HWND>(addr);
    if (!IsWindow(target)) {
      cpp11::stop("Window handle is not a valid active window: '%s'.", hwnd_hex.c_str());
    }
    RECT rc{0, 0, 0, 0};
    if (!GetWindowRect(target, &rc)) {
      cpp11::stop("GetWindowRect failed (Win32 error %lu).", GetLastError());
    }
    width = static_cast<int>(rc.right - rc.left);
    height = static_cast<int>(rc.bottom - rc.top);
  } else {
    cap_x = GetSystemMetrics(SM_XVIRTUALSCREEN);
    cap_y = GetSystemMetrics(SM_YVIRTUALSCREEN);
    width = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    height = GetSystemMetrics(SM_CYVIRTUALSCREEN);
  }

  if (width <= 0 || height <= 0) {
    cpp11::stop("Target capture region has zero area (%d x %d).", width, height);
  }

  winrpa::DcReleaseGuard screen_dc;
  screen_dc.hwnd = nullptr;
  screen_dc.hdc = GetDC(nullptr);
  if (screen_dc.hdc == nullptr) {
    cpp11::stop("GetDC(nullptr) failed.");
  }

  winrpa::MemDcGuard mem_dc;
  mem_dc.hdc = CreateCompatibleDC(screen_dc.hdc);
  if (mem_dc.hdc == nullptr) {
    cpp11::stop("CreateCompatibleDC failed.");
  }

  BITMAPINFO bmi{};
  bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bmi.bmiHeader.biWidth = width;
  bmi.bmiHeader.biHeight = -height;
  bmi.bmiHeader.biPlanes = 1;
  bmi.bmiHeader.biBitCount = 32;
  bmi.bmiHeader.biCompression = BI_RGB;

  void* raw_bits = nullptr;
  winrpa::BitmapGuard bmp;
  bmp.hbm = CreateDIBSection(
      screen_dc.hdc, &bmi, DIB_RGB_COLORS, &raw_bits, nullptr, 0U);
  if (bmp.hbm == nullptr || raw_bits == nullptr) {
    cpp11::stop("CreateDIBSection failed.");
  }

  winrpa::SelectObjGuard sel;
  sel.hdc = mem_dc.hdc;
  sel.old = SelectObject(mem_dc.hdc, bmp.hbm);

  bool captured = false;
  if (target != nullptr) {
    if (PrintWindow(target, mem_dc.hdc, PW_RENDERFULLCONTENT)) {
      captured = true;
    } else if (PrintWindow(target, mem_dc.hdc, 0U)) {
      captured = true;
    } else {
      winrpa::DcReleaseGuard win_dc;
      win_dc.hwnd = target;
      win_dc.hdc = GetWindowDC(target);
      if (win_dc.hdc != nullptr &&
          BitBlt(mem_dc.hdc, 0, 0, width, height, win_dc.hdc, 0, 0, SRCCOPY | CAPTUREBLT)) {
        captured = true;
      }
    }
  } else {
    if (BitBlt(mem_dc.hdc, 0, 0, width, height, screen_dc.hdc, cap_x, cap_y, SRCCOPY | CAPTUREBLT)) {
      captured = true;
    }
  }

  if (!captured) {
    cpp11::stop("Window/screen capture failed (Win32 error %lu).", GetLastError());
  }

  const auto bytes = winrpa::encode_bgr_bmp(
      width, height, static_cast<const std::uint8_t*>(raw_bits));
  if (bytes.empty()) {
    cpp11::stop("BMP encoding produced an empty buffer.");
  }

  const std::wstring wpath = winrpa::utf8_to_wide(out_path);
  std::ofstream ofs(std::filesystem::path(wpath), std::ios::binary | std::ios::trunc);
  if (!ofs.is_open()) {
    cpp11::stop("Failed to open screenshot path for writing: '%s'.", out_path.c_str());
  }
  ofs.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  if (!ofs.good()) {
    cpp11::stop("Failed writing BMP screenshot bytes to '%s'.", out_path.c_str());
  }
  return out_path;
#else
  (void)hwnd_hex;
  (void)out_path;
  winrpa::require_windows_or_stop("cpp_capture_window_bmp()");
  return "";
#endif
}

[[cpp11::register]]
bool cpp_enable_killswitch(int modifiers, int vk) {
#ifdef _WIN32
  DWORD err = 0U;
  const bool ok = winrpa::RuntimeHookThread::instance().enable_killswitch(
      static_cast<UINT>(modifiers), static_cast<UINT>(vk), err);
  if (!ok) {
    cpp11::stop("RegisterHotKey failed (Win32 error %lu).", err);
  }
  return true;
#else
  (void)modifiers;
  (void)vk;
  winrpa::require_windows_or_stop("cpp_enable_killswitch()");
  return false;
#endif
}

[[cpp11::register]]
bool cpp_disable_killswitch() {
#ifdef _WIN32
  return winrpa::RuntimeHookThread::instance().disable_killswitch();
#else
  return true;
#endif
}

[[cpp11::register]]
bool cpp_hook_start() {
#ifdef _WIN32
  winrpa::g_hook_ring.clear();
  DWORD err = 0U;
  const bool ok = winrpa::RuntimeHookThread::instance().start_keyboard_hook(err);
  if (!ok) {
    cpp11::stop("SetWindowsHookExW failed (Win32 error %lu).", err);
  }
  return true;
#else
  winrpa::require_windows_or_stop("cpp_hook_start()");
  return false;
#endif
}

[[cpp11::register]]
bool cpp_hook_stop() {
#ifdef _WIN32
  return winrpa::RuntimeHookThread::instance().stop_keyboard_hook();
#else
  return true;
#endif
}

[[cpp11::register]]
cpp11::writable::data_frame cpp_hook_drain() {
#ifdef _WIN32
  std::vector<winrpa::HookEvent> evs;
  winrpa::g_hook_ring.drain(evs);
  const R_xlen_t n = static_cast<R_xlen_t>(evs.size());
  cpp11::writable::integers vk_code(n);
  cpp11::writable::integers scan_code(n);
  cpp11::writable::logicals key_up(n);
  cpp11::writable::doubles timestamp_ms(n);

  for (R_xlen_t i = 0; i < n; ++i) {
    const auto& e = evs[static_cast<std::size_t>(i)];
    vk_code[i] = static_cast<int>(e.vk_code);
    scan_code[i] = static_cast<int>(e.scan_code);
    key_up[i] = e.key_up;
    timestamp_ms[i] = static_cast<double>(e.timestamp_ms);
  }

  using namespace cpp11::literals;
  return cpp11::writable::data_frame({
      "vk_code"_nm = vk_code,
      "scan_code"_nm = scan_code,
      "key_up"_nm = key_up,
      "timestamp_ms"_nm = timestamp_ms
  });
#else
  winrpa::require_windows_or_stop("cpp_hook_drain()");
  return cpp11::writable::data_frame();
#endif
}
