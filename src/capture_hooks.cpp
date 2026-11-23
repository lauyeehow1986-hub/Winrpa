#ifndef R_NO_REMAP
#define R_NO_REMAP
#endif
#include <cpp11.hpp>
#include "capture_hooks.h"
#include "mta_worker.h"
#include "winrpa_core.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <future>
#include <memory>
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
RecordEventRingBuffer g_record_ring;

std::atomic<bool> g_raw_kbd_active{false};
std::atomic<bool> g_record_active{false};
std::atomic<bool> g_record_include_wheel{true};
std::atomic<bool> g_record_stop_triggered{false};
std::atomic<bool> g_stop_hotkey_installed{false};
std::atomic<std::uint32_t> g_stop_hotkey_mods{0U};
std::atomic<std::uint32_t> g_stop_hotkey_vk{0U};
std::atomic<HANDLE> g_record_signal{nullptr};

void ensure_record_signal() noexcept {
  HANDLE existing = g_record_signal.load(std::memory_order_acquire);
  if (existing != nullptr) {
    return;
  }
  HANDLE created = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  if (created == nullptr) {
    return;
  }
  HANDLE expected = nullptr;
  if (!g_record_signal.compare_exchange_strong(
          expected, created, std::memory_order_acq_rel)) {
    CloseHandle(created);
  }
}

void close_record_signal() noexcept {
  HANDLE h = g_record_signal.exchange(nullptr, std::memory_order_acq_rel);
  if (h != nullptr) {
    CloseHandle(h);
  }
}

std::uint8_t read_async_modifiers() noexcept {
  std::uint8_t mods = 0U;
  if ((GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0) mods |= 1U;
  if ((GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0) mods |= 2U;
  if ((GetAsyncKeyState(VK_MENU) & 0x8000) != 0) mods |= 4U;
  if (((GetAsyncKeyState(VK_LWIN) & 0x8000) != 0) ||
      ((GetAsyncKeyState(VK_RWIN) & 0x8000) != 0)) {
    mods |= 8U;
  }
  return mods;
}

bool matches_stop_hotkey(std::uint32_t vk, std::uint8_t mods) noexcept {
  if (!g_stop_hotkey_installed.load(std::memory_order_acquire)) {
    return false;
  }
  if (vk != g_stop_hotkey_vk.load(std::memory_order_acquire)) {
    return false;
  }
  const std::uint32_t req = g_stop_hotkey_mods.load(std::memory_order_acquire);
  // RegisterHotKey bitmask: MOD_ALT=1, MOD_CONTROL=2, MOD_SHIFT=4, MOD_WIN=8
  std::uint8_t req_async = 0U;
  if ((req & 2U) != 0U) req_async |= 1U;
  if ((req & 4U) != 0U) req_async |= 2U;
  if ((req & 1U) != 0U) req_async |= 4U;
  if ((req & 8U) != 0U) req_async |= 8U;
  return (mods & req_async) == req_async;
}

LRESULT CALLBACK low_level_keyboard_proc(int nCode, WPARAM wParam, LPARAM lParam) {
  if (nCode == HC_ACTION && lParam != 0) {
    const auto* kb = reinterpret_cast<const KBDLLHOOKSTRUCT*>(lParam);
    const bool is_up = (wParam == WM_KEYUP || wParam == WM_SYSKEYUP);

    if (g_raw_kbd_active.load(std::memory_order_acquire)) {
      HookEvent ev{};
      ev.vk_code = static_cast<std::uint32_t>(kb->vkCode);
      ev.scan_code = static_cast<std::uint32_t>(kb->scanCode);
      ev.key_up = is_up;
      ev.timestamp_ms = static_cast<std::int64_t>(kb->time);
      g_hook_ring.push(ev);
    }

    if (g_record_active.load(std::memory_order_acquire) &&
        (kb->flags & LLKHF_INJECTED) == 0U) {
      const std::uint8_t mods = read_async_modifiers();
      const std::uint32_t vk = static_cast<std::uint32_t>(kb->vkCode);
      if (!(!is_up && matches_stop_hotkey(vk, mods))) {
        RawRecordEvent rev{};
        rev.kind = static_cast<std::uint8_t>(
            is_up ? RecordEventKind::kKeyUp : RecordEventKind::kKeyDown);
        rev.modifiers = mods;
        rev.vk_code = vk;
        rev.scan_code = static_cast<std::uint32_t>(kb->scanCode);
        rev.timestamp_ms = static_cast<std::int64_t>(kb->time);
        HWND fg = GetForegroundWindow();
        rev.fg_hwnd = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(fg));
        const DWORD fg_tid = (fg != nullptr) ? GetWindowThreadProcessId(fg, nullptr) : 0U;
        HKL layout = (fg_tid != 0U) ? GetKeyboardLayout(fg_tid) : GetKeyboardLayout(0U);
        rev.hkl = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(layout));
        g_record_ring.push(rev);
        HANDLE sig = g_record_signal.load(std::memory_order_acquire);
        if (sig != nullptr) {
          SetEvent(sig);
        }
      }
    }
  }
  return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

LRESULT CALLBACK low_level_mouse_proc(int nCode, WPARAM wParam, LPARAM lParam) {
  if (nCode == HC_ACTION && lParam != 0 &&
      wParam != WM_MOUSEMOVE &&
      g_record_active.load(std::memory_order_acquire)) {
    const auto* ms = reinterpret_cast<const MSLLHOOKSTRUCT*>(lParam);
    if ((ms->flags & LLMHF_INJECTED) == 0U) {
      RawRecordEvent rev{};
      bool emit = true;
      switch (wParam) {
        case WM_LBUTTONDOWN:
          rev.kind = static_cast<std::uint8_t>(RecordEventKind::kMouseDown);
          rev.button = 1U;
          break;
        case WM_LBUTTONUP:
          rev.kind = static_cast<std::uint8_t>(RecordEventKind::kMouseUp);
          rev.button = 1U;
          break;
        case WM_RBUTTONDOWN:
          rev.kind = static_cast<std::uint8_t>(RecordEventKind::kMouseDown);
          rev.button = 2U;
          break;
        case WM_RBUTTONUP:
          rev.kind = static_cast<std::uint8_t>(RecordEventKind::kMouseUp);
          rev.button = 2U;
          break;
        case WM_MBUTTONDOWN:
          rev.kind = static_cast<std::uint8_t>(RecordEventKind::kMouseDown);
          rev.button = 3U;
          break;
        case WM_MBUTTONUP:
          rev.kind = static_cast<std::uint8_t>(RecordEventKind::kMouseUp);
          rev.button = 3U;
          break;
        case WM_MOUSEWHEEL:
        case WM_MOUSEHWHEEL: {
          if (!g_record_include_wheel.load(std::memory_order_acquire)) {
            emit = false;
          } else {
            rev.kind = static_cast<std::uint8_t>(RecordEventKind::kMouseWheel);
            rev.flags = (wParam == WM_MOUSEHWHEEL) ? 1U : 0U;
            const std::uint16_t raw_delta = static_cast<std::uint16_t>(
                (static_cast<std::uint32_t>(ms->mouseData) >> 16U) & 0xFFFFU);
            rev.wheel_delta = static_cast<std::int32_t>(
                static_cast<std::int16_t>(raw_delta));
          }
          break;
        }
        default:
          emit = false;
          break;
      }

      if (emit) {
        rev.modifiers = read_async_modifiers();
        rev.x = static_cast<std::int32_t>(ms->pt.x);
        rev.y = static_cast<std::int32_t>(ms->pt.y);
        rev.timestamp_ms = static_cast<std::int64_t>(ms->time);
        HWND fg = GetForegroundWindow();
        rev.fg_hwnd = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(fg));
        g_record_ring.push(rev);
        HANDLE sig = g_record_signal.load(std::memory_order_acquire);
        if (sig != nullptr) {
          SetEvent(sig);
        }
      }
    }
  }
  return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

class RuntimeHookThread {
 public:
  static constexpr UINT kMsgRegisterHotkey = WM_APP + 1;
  static constexpr UINT kMsgUnregisterHotkey = WM_APP + 2;
  static constexpr UINT kMsgStartHook = WM_APP + 3;
  static constexpr UINT kMsgStopHook = WM_APP + 4;
  static constexpr UINT kMsgStartRecord = WM_APP + 5;
  static constexpr UINT kMsgStopRecord = WM_APP + 6;
  static constexpr int kKillswitchId = 0x5752;
  static constexpr int kRecordStopHotkeyId = 0x5753;

  struct CmdPayload {
    UINT modifiers = 0U;
    UINT vk = 0U;
    bool include_wheel = true;
    bool install_stop_hotkey = false;
    bool ok = false;
    DWORD err = 0U;
    std::promise<void> done;
  };

  static RuntimeHookThread& instance() {
    static RuntimeHookThread inst;
    return inst;
  }

  bool enable_killswitch(UINT modifiers, UINT vk, DWORD& err_out) {
    if (!ensure_running(err_out)) {
      return false;
    }
    auto cmd = std::make_shared<CmdPayload>();
    cmd->modifiers = modifiers;
    cmd->vk = vk;
    return send_cmd(kMsgRegisterHotkey, cmd, err_out);
  }

  bool disable_killswitch() {
    auto cmd = std::make_shared<CmdPayload>();
    DWORD err_out = 0U;
    return send_cmd_if_running(kMsgUnregisterHotkey, cmd, err_out);
  }

  bool start_keyboard_hook(DWORD& err_out) {
    if (!ensure_running(err_out)) {
      return false;
    }
    g_raw_kbd_active.store(true, std::memory_order_release);
    auto cmd = std::make_shared<CmdPayload>();
    if (!send_cmd(kMsgStartHook, cmd, err_out)) {
      g_raw_kbd_active.store(false, std::memory_order_release);
      return false;
    }
    return true;
  }

  bool stop_keyboard_hook() {
    g_raw_kbd_active.store(false, std::memory_order_release);
    auto cmd = std::make_shared<CmdPayload>();
    DWORD err_out = 0U;
    return send_cmd_if_running(kMsgStopHook, cmd, err_out);
  }

  bool start_record_hooks(
      bool include_wheel,
      bool install_stop_hotkey,
      UINT stop_mods,
      UINT stop_vk,
      DWORD& err_out) {
    ensure_record_signal();
    if (!ensure_running(err_out)) {
      return false;
    }
    g_record_ring.clear();
    g_record_stop_triggered.store(false, std::memory_order_release);
    g_record_include_wheel.store(include_wheel, std::memory_order_release);
    g_stop_hotkey_mods.store(static_cast<std::uint32_t>(stop_mods), std::memory_order_release);
    g_stop_hotkey_vk.store(static_cast<std::uint32_t>(stop_vk), std::memory_order_release);
    g_record_active.store(true, std::memory_order_release);

    auto cmd = std::make_shared<CmdPayload>();
    cmd->include_wheel = include_wheel;
    cmd->install_stop_hotkey = install_stop_hotkey;
    cmd->modifiers = stop_mods;
    cmd->vk = stop_vk;
    if (!send_cmd(kMsgStartRecord, cmd, err_out)) {
      g_record_active.store(false, std::memory_order_release);
      g_stop_hotkey_installed.store(false, std::memory_order_release);
      return false;
    }
    return true;
  }

  bool stop_record_hooks() {
    g_record_active.store(false, std::memory_order_release);
    g_stop_hotkey_installed.store(false, std::memory_order_release);
    auto cmd = std::make_shared<CmdPayload>();
    DWORD err_out = 0U;
    return send_cmd_if_running(kMsgStopRecord, cmd, err_out);
  }

  void shutdown() {
    g_raw_kbd_active.store(false, std::memory_order_release);
    g_record_active.store(false, std::memory_order_release);
    g_stop_hotkey_installed.store(false, std::memory_order_release);

    std::thread to_join;
    DWORD target_tid = 0U;
    {
      std::lock_guard<std::mutex> lock(mu_);
      shutting_down_.store(true, std::memory_order_release);
      if (!running_) {
        close_record_signal();
        return;
      }
      target_tid = tid_.load(std::memory_order_acquire);
      to_join = std::move(thread_);
      running_ = false;
      tid_.store(0U, std::memory_order_release);
    }
    if (target_tid != 0U) {
      PostThreadMessageW(target_tid, WM_QUIT, 0U, 0);
    }
    HANDLE sig = g_record_signal.load(std::memory_order_acquire);
    if (sig != nullptr) {
      SetEvent(sig);
    }
    if (to_join.joinable()) {
      to_join.join();
    }
    close_record_signal();
  }

  ~RuntimeHookThread() {
    try {
      shutdown();
    } catch (...) {
    }
  }

 private:
  RuntimeHookThread() = default;

  bool send_cmd(
      UINT msg,
      const std::shared_ptr<CmdPayload>& cmd,
      DWORD& err_out) {
    DWORD target_tid = 0U;
    {
      std::lock_guard<std::mutex> lock(mu_);
      if (!running_ || shutting_down_.load(std::memory_order_acquire)) {
        err_out = ERROR_INVALID_THREAD_ID;
        return false;
      }
      target_tid = tid_.load(std::memory_order_acquire);
    }
    if (target_tid == 0U) {
      err_out = ERROR_INVALID_THREAD_ID;
      return false;
    }

    auto fut = cmd->done.get_future();
    auto* heap_cmd = new std::shared_ptr<CmdPayload>(cmd);
    if (!PostThreadMessageW(target_tid, msg, 0U, reinterpret_cast<LPARAM>(heap_cmd))) {
      err_out = GetLastError();
      delete heap_cmd;
      return false;
    }
    if (fut.wait_for(std::chrono::milliseconds(2000)) != std::future_status::ready) {
      err_out = ERROR_TIMEOUT;
      return false;
    }
    err_out = cmd->err;
    return cmd->ok;
  }

  bool send_cmd_if_running(
      UINT msg,
      const std::shared_ptr<CmdPayload>& cmd,
      DWORD& err_out) {
    {
      std::lock_guard<std::mutex> lock(mu_);
      if (!running_ || shutting_down_.load(std::memory_order_acquire)) {
        err_out = 0U;
        return true;
      }
    }
    return send_cmd(msg, cmd, err_out);
  }

  bool ensure_running(DWORD& err_out) {
    std::lock_guard<std::mutex> lock(mu_);
    shutting_down_.store(false, std::memory_order_release);
    if (running_) {
      return true;
    }
    std::promise<DWORD> ready;
    auto fut = ready.get_future();
    thread_ = std::thread(&RuntimeHookThread::loop, this, std::move(ready));
    if (fut.wait_for(std::chrono::milliseconds(3000)) != std::future_status::ready) {
      shutting_down_.store(true, std::memory_order_release);
      if (thread_.joinable()) {
        thread_.join();
      }
      err_out = ERROR_TIMEOUT;
      return false;
    }
    tid_.store(fut.get(), std::memory_order_release);
    running_ = true;
    return true;
  }

  HMODULE self_module() noexcept {
    HMODULE hmod = nullptr;
    GetModuleHandleExW(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&low_level_keyboard_proc),
        &hmod);
    return hmod;
  }

  bool ensure_kbd_hook(HHOOK& kbd_hook, DWORD& err) noexcept {
    if (kbd_hook != nullptr) {
      err = 0U;
      return true;
    }
    SetLastError(0U);
    kbd_hook = SetWindowsHookExW(
        WH_KEYBOARD_LL, low_level_keyboard_proc, self_module(), 0U);
    err = (kbd_hook != nullptr) ? 0U : GetLastError();
    return kbd_hook != nullptr;
  }

  void maybe_unhook_kbd(HHOOK& kbd_hook) noexcept {
    if (kbd_hook != nullptr &&
        !g_raw_kbd_active.load(std::memory_order_acquire) &&
        !g_record_active.load(std::memory_order_acquire)) {
      UnhookWindowsHookEx(kbd_hook);
      kbd_hook = nullptr;
    }
  }

  void loop(std::promise<DWORD> ready) {
    MSG msg{};
    PeekMessageW(&msg, nullptr, WM_USER, WM_USER, PM_NOREMOVE);
    ready.set_value(GetCurrentThreadId());

    bool hotkey_active = false;
    bool rec_hotkey_active = false;
    HHOOK kbd_hook = nullptr;
    HHOOK mouse_hook = nullptr;

    BOOL gm = FALSE;
    while (!shutting_down_.load(std::memory_order_acquire) &&
           (gm = GetMessageW(&msg, nullptr, 0U, 0U)) != 0) {
      if (gm == -1) {
        break;
      }
      if (msg.message == WM_HOTKEY) {
        const int hk_id = static_cast<int>(msg.wParam);
        if (hk_id == kKillswitchId) {
          g_abort_requested.store(true, std::memory_order_release);
        } else if (hk_id == kRecordStopHotkeyId) {
          g_record_stop_triggered.store(true, std::memory_order_release);
          g_record_active.store(false, std::memory_order_release);
          g_stop_hotkey_installed.store(false, std::memory_order_release);
          if (mouse_hook != nullptr) {
            UnhookWindowsHookEx(mouse_hook);
            mouse_hook = nullptr;
          }
          maybe_unhook_kbd(kbd_hook);
          if (rec_hotkey_active) {
            UnregisterHotKey(nullptr, kRecordStopHotkeyId);
            rec_hotkey_active = false;
          }
          HANDLE sig = g_record_signal.load(std::memory_order_acquire);
          if (sig != nullptr) {
            SetEvent(sig);
          }
        }
        continue;
      }
      if (msg.message >= kMsgRegisterHotkey && msg.message <= kMsgStopRecord) {
        std::unique_ptr<std::shared_ptr<CmdPayload>> holder(
            reinterpret_cast<std::shared_ptr<CmdPayload>*>(msg.lParam));
        CmdPayload& cmd = **holder;
        if (msg.message == kMsgRegisterHotkey) {
          if (hotkey_active) {
            UnregisterHotKey(nullptr, kKillswitchId);
            hotkey_active = false;
          }
          SetLastError(0U);
          const BOOL ok = RegisterHotKey(
              nullptr, kKillswitchId, cmd.modifiers | MOD_NOREPEAT, cmd.vk);
          cmd.ok = (ok != FALSE);
          cmd.err = cmd.ok ? 0U : GetLastError();
          if (cmd.ok) {
            hotkey_active = true;
          }
        } else if (msg.message == kMsgUnregisterHotkey) {
          if (hotkey_active) {
            UnregisterHotKey(nullptr, kKillswitchId);
            hotkey_active = false;
          }
          cmd.ok = true;
          cmd.err = 0U;
        } else if (msg.message == kMsgStartHook) {
          DWORD err = 0U;
          cmd.ok = ensure_kbd_hook(kbd_hook, err);
          cmd.err = err;
        } else if (msg.message == kMsgStopHook) {
          maybe_unhook_kbd(kbd_hook);
          cmd.ok = true;
          cmd.err = 0U;
        } else if (msg.message == kMsgStartRecord) {
          DWORD kerr = 0U;
          const bool k_ok = ensure_kbd_hook(kbd_hook, kerr);
          DWORD merr = 0U;
          bool m_ok = (mouse_hook != nullptr);
          if (!m_ok) {
            SetLastError(0U);
            mouse_hook = SetWindowsHookExW(
                WH_MOUSE_LL, low_level_mouse_proc, self_module(), 0U);
            m_ok = (mouse_hook != nullptr);
            merr = m_ok ? 0U : GetLastError();
          }
          if (!k_ok || !m_ok) {
            g_record_active.store(false, std::memory_order_release);
            if (mouse_hook != nullptr) {
              UnhookWindowsHookEx(mouse_hook);
              mouse_hook = nullptr;
            }
            maybe_unhook_kbd(kbd_hook);
            cmd.ok = false;
            cmd.err = !k_ok ? kerr : merr;
          } else {
            if (rec_hotkey_active) {
              UnregisterHotKey(nullptr, kRecordStopHotkeyId);
              rec_hotkey_active = false;
            }
            if (cmd.install_stop_hotkey && cmd.vk != 0U) {
              if (RegisterHotKey(
                      nullptr, kRecordStopHotkeyId,
                      cmd.modifiers | MOD_NOREPEAT, cmd.vk)) {
                rec_hotkey_active = true;
                g_stop_hotkey_installed.store(true, std::memory_order_release);
              }
            }
            cmd.ok = true;
            cmd.err = 0U;
          }
        } else if (msg.message == kMsgStopRecord) {
          if (mouse_hook != nullptr) {
            UnhookWindowsHookEx(mouse_hook);
            mouse_hook = nullptr;
          }
          maybe_unhook_kbd(kbd_hook);
          if (rec_hotkey_active) {
            UnregisterHotKey(nullptr, kRecordStopHotkeyId);
            rec_hotkey_active = false;
          }
          g_stop_hotkey_installed.store(false, std::memory_order_release);
          cmd.ok = true;
          cmd.err = 0U;
        }
        cmd.done.set_value();
        continue;
      }
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }

    while (PeekMessageW(&msg, nullptr, kMsgRegisterHotkey, kMsgStopRecord, PM_REMOVE)) {
      std::unique_ptr<std::shared_ptr<CmdPayload>> holder(
          reinterpret_cast<std::shared_ptr<CmdPayload>*>(msg.lParam));
      if (holder && *holder) {
        (*holder)->ok = false;
        (*holder)->err = ERROR_CANCELLED;
        try {
          (*holder)->done.set_value();
        } catch (...) {
        }
      }
    }

    if (hotkey_active) {
      UnregisterHotKey(nullptr, kKillswitchId);
    }
    if (rec_hotkey_active) {
      UnregisterHotKey(nullptr, kRecordStopHotkeyId);
    }
    if (mouse_hook != nullptr) {
      UnhookWindowsHookEx(mouse_hook);
    }
    if (kbd_hook != nullptr) {
      UnhookWindowsHookEx(kbd_hook);
    }
  }

  std::mutex mu_;
  std::thread thread_;
  std::atomic<DWORD> tid_{0U};
  std::atomic<bool> shutting_down_{false};
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

bool start_record_hooks(
    bool include_wheel,
    bool install_stop_hotkey,
    std::uint32_t stop_mods,
    std::uint32_t stop_vk,
    std::uint32_t& err_out) {
  DWORD derr = 0U;
  const bool ok = RuntimeHookThread::instance().start_record_hooks(
      include_wheel,
      install_stop_hotkey,
      static_cast<UINT>(stop_mods),
      static_cast<UINT>(stop_vk),
      derr);
  err_out = static_cast<std::uint32_t>(derr);
  return ok;
}

bool stop_record_hooks() {
  return RuntimeHookThread::instance().stop_record_hooks();
}

bool record_stop_hotkey_triggered() noexcept {
  return g_record_stop_triggered.load(std::memory_order_acquire);
}

void reset_record_stop_hotkey() noexcept {
  g_record_stop_triggered.store(false, std::memory_order_release);
}

RecordEventRingBuffer& record_ring() noexcept {
  return g_record_ring;
}

bool wait_record_signal(std::uint32_t timeout_ms) {
  HANDLE sig = g_record_signal.load(std::memory_order_acquire);
  if (sig == nullptr) {
    Sleep(timeout_ms > 20U ? 20U : timeout_ms);
    return false;
  }
  return WaitForSingleObject(sig, static_cast<DWORD>(timeout_ms)) == WAIT_OBJECT_0;
}

void signal_record_event() noexcept {
  HANDLE sig = g_record_signal.load(std::memory_order_acquire);
  if (sig != nullptr) {
    SetEvent(sig);
  }
}

}  // namespace winrpa

#else

namespace winrpa {

namespace {
HookEventRingBuffer g_hook_ring;
RecordEventRingBuffer g_record_ring;
}  // namespace

void stop_runtime_hooks() {}

bool start_record_hooks(
    bool, bool, std::uint32_t, std::uint32_t, std::uint32_t& err_out) {
  err_out = 0U;
  return false;
}

bool stop_record_hooks() { return true; }

bool record_stop_hotkey_triggered() noexcept { return false; }

void reset_record_stop_hotkey() noexcept {}

RecordEventRingBuffer& record_ring() noexcept { return g_record_ring; }

bool wait_record_signal(std::uint32_t) { return false; }

void signal_record_event() noexcept {}

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
