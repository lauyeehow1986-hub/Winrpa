#ifndef R_NO_REMAP
#define R_NO_REMAP
#endif
#include <cpp11.hpp>
#include "recorder.h"
#include "capture_hooks.h"
#include "mta_worker.h"
#include "winrpa_core.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
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
#include <tlhelp32.h>
#include <uiautomation.h>

namespace winrpa {

namespace {

struct VariantGuardRec {
  VARIANT v;
  VariantGuardRec() { VariantInit(&v); }
  VariantGuardRec(const VariantGuardRec&) = delete;
  VariantGuardRec& operator=(const VariantGuardRec&) = delete;
  ~VariantGuardRec() { VariantClear(&v); }
};

struct HandleGuard {
  HANDLE h = nullptr;
  HandleGuard() noexcept = default;
  explicit HandleGuard(HANDLE handle) noexcept : h(handle) {}
  HandleGuard(const HandleGuard&) = delete;
  HandleGuard& operator=(const HandleGuard&) = delete;
  ~HandleGuard() {
    if (h != nullptr && h != INVALID_HANDLE_VALUE) {
      CloseHandle(h);
    }
  }
};

struct RecordedAction {
  std::string kind;
  std::int64_t timestamp_ms = 0;
  int x = 0;
  int y = 0;
  int rel_x = 0;
  int rel_y = 0;
  int wheel_delta = 0;
  bool horizontal = false;
  std::string button;
  int vk_code = 0;
  int scan_code = 0;
  int modifiers = 0;
  std::string key_name;
  std::string unicode_text;
  std::string win_title;
  std::string win_class;
  std::string win_hwnd;
  int win_pid = 0;
  int win_left = 0;
  int win_top = 0;
  int win_right = 0;
  int win_bottom = 0;
  std::string el_name;
  std::string el_automation_id;
  std::string el_control_type;
  std::string el_class_name;
  std::string el_hwnd;
  int el_pid = 0;
  int el_left = 0;
  int el_top = 0;
  int el_right = 0;
  int el_bottom = 0;
  bool is_password = false;
  bool has_toggle = false;
  bool has_select = false;
  bool has_value = false;
};

struct ResolvedTarget {
  std::string win_title;
  std::string win_class;
  std::string win_hwnd;
  int win_pid = 0;
  int win_left = 0;
  int win_top = 0;
  int win_right = 0;
  int win_bottom = 0;
  std::string el_name;
  std::string el_automation_id;
  std::string el_control_type;
  std::string el_class_name;
  std::string el_hwnd;
  int el_pid = 0;
  int el_left = 0;
  int el_top = 0;
  int el_right = 0;
  int el_bottom = 0;
  bool is_password = false;
  bool has_toggle = false;
  bool has_select = false;
  bool has_value = false;
  bool valid = false;
};

DWORD get_parent_pid_win32(DWORD pid) {
  HandleGuard snap(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0U));
  if (snap.h == nullptr || snap.h == INVALID_HANDLE_VALUE) {
    return 0U;
  }
  PROCESSENTRY32W pe{};
  pe.dwSize = sizeof(pe);
  if (!Process32FirstW(snap.h, &pe)) {
    return 0U;
  }
  do {
    if (pe.th32ProcessID == pid) {
      return pe.th32ParentProcessID;
    }
  } while (Process32NextW(snap.h, &pe));
  return 0U;
}

void read_win32_window_meta(HWND hwnd, ResolvedTarget& out) {
  if (hwnd == nullptr || !IsWindow(hwnd)) {
    return;
  }
  if (out.win_hwnd.empty()) {
    out.win_hwnd = hwnd_to_hex(reinterpret_cast<UIA_HWND>(hwnd));
  }
  if (out.win_title.empty()) {
    wchar_t tbuf[512]{};
    const int n = GetWindowTextW(hwnd, tbuf, 512);
    if (n > 0) {
      out.win_title = wide_to_utf8(tbuf, n);
    }
  }
  if (out.win_class.empty()) {
    wchar_t cbuf[256]{};
    const int n = GetClassNameW(hwnd, cbuf, 256);
    if (n > 0) {
      out.win_class = wide_to_utf8(cbuf, n);
    }
  }
  if (out.win_pid <= 0) {
    DWORD pid = 0U;
    GetWindowThreadProcessId(hwnd, &pid);
    out.win_pid = static_cast<int>(pid);
  }
  if (out.win_right <= out.win_left || out.win_bottom <= out.win_top) {
    RECT rc{0, 0, 0, 0};
    if (GetWindowRect(hwnd, &rc)) {
      out.win_left = static_cast<int>(rc.left);
      out.win_top = static_cast<int>(rc.top);
      out.win_right = static_cast<int>(rc.right);
      out.win_bottom = static_cast<int>(rc.bottom);
    }
  }
}

void read_cached_elem_props(IUIAutomationElement* el, ResolvedTarget& out) {
  if (el == nullptr) {
    return;
  }
  BstrGuard b_name;
  if (SUCCEEDED(el->get_CachedName(b_name.put()))) {
    out.el_name = bstr_to_utf8(b_name.get());
  }
  BstrGuard b_aid;
  if (SUCCEEDED(el->get_CachedAutomationId(b_aid.put()))) {
    out.el_automation_id = bstr_to_utf8(b_aid.get());
  }
  CONTROLTYPEID ctype = 0;
  if (SUCCEEDED(el->get_CachedControlType(&ctype))) {
    out.el_control_type = control_type_to_string(ctype);
  }
  BstrGuard b_cls;
  if (SUCCEEDED(el->get_CachedClassName(b_cls.put()))) {
    out.el_class_name = bstr_to_utf8(b_cls.get());
  }
  int pid = 0;
  if (SUCCEEDED(el->get_CachedProcessId(&pid))) {
    out.el_pid = pid;
  }
  UIA_HWND hwnd = nullptr;
  if (SUCCEEDED(el->get_CachedNativeWindowHandle(&hwnd))) {
    out.el_hwnd = hwnd_to_hex(hwnd);
  }
  RECT rc{0, 0, 0, 0};
  if (SUCCEEDED(el->get_CachedBoundingRectangle(&rc))) {
    out.el_left = static_cast<int>(rc.left);
    out.el_top = static_cast<int>(rc.top);
    out.el_right = static_cast<int>(rc.right);
    out.el_bottom = static_cast<int>(rc.bottom);
  }
  BOOL is_pwd = FALSE;
  if (SUCCEEDED(el->get_CachedIsPassword(&is_pwd))) {
    out.is_password = (is_pwd != FALSE);
  }
  ComPtr<IUnknown> pat;
  if (SUCCEEDED(el->GetCachedPattern(UIA_TogglePatternId, pat.put())) && pat) {
    out.has_toggle = true;
  }
  pat.reset();
  if (SUCCEEDED(el->GetCachedPattern(UIA_SelectionItemPatternId, pat.put())) && pat) {
    out.has_select = true;
  }
  pat.reset();
  if (SUCCEEDED(el->GetCachedPattern(UIA_ValuePatternId, pat.put())) && pat) {
    out.has_value = true;
  }
  out.valid = true;
}

ComPtr<IUIAutomationElement> find_top_level_window(
    IUIAutomation* uia,
    IUIAutomationCacheRequest* req,
    IUIAutomationElement* root,
    IUIAutomationTreeWalker* walker,
    IUIAutomationElement* start_el) {
  if (uia == nullptr || walker == nullptr || start_el == nullptr) {
    return {};
  }
  if (root != nullptr) {
    BOOL same_root = FALSE;
    if (SUCCEEDED(uia->CompareElements(start_el, root, &same_root)) && same_root != FALSE) {
      return {};
    }
  }
  start_el->AddRef();
  ComPtr<IUIAutomationElement> cur(start_el);
  for (int hop = 0; hop < 32; ++hop) {
    ComPtr<IUIAutomationElement> parent;
    HRESULT hr = req != nullptr
                     ? walker->GetParentElementBuildCache(cur.get(), req, parent.put())
                     : walker->GetParentElement(cur.get(), parent.put());
    if (FAILED(hr) || !parent) {
      return cur;
    }
    if (root != nullptr) {
      BOOL is_root = FALSE;
      if (SUCCEEDED(uia->CompareElements(parent.get(), root, &is_root)) && is_root != FALSE) {
        return cur;
      }
    }
    cur = std::move(parent);
  }
  return cur;
}

void resolve_target_from_uia(
    IUIAutomation* uia,
    IUIAutomationCacheRequest* req,
    IUIAutomationElement* root,
    IUIAutomationTreeWalker* walker,
    IUIAutomationElement* el,
    HWND fallback_hwnd,
    ResolvedTarget& out) {
  out = ResolvedTarget{};
  if (el != nullptr) {
    read_cached_elem_props(el, out);
    ComPtr<IUIAutomationElement> top = find_top_level_window(uia, req, root, walker, el);
    if (top) {
      BstrGuard wtitle;
      if (SUCCEEDED(top->get_CachedName(wtitle.put()))) {
        out.win_title = bstr_to_utf8(wtitle.get());
      }
      BstrGuard wcls;
      if (SUCCEEDED(top->get_CachedClassName(wcls.put()))) {
        out.win_class = bstr_to_utf8(wcls.get());
      }
      int wpid = 0;
      if (SUCCEEDED(top->get_CachedProcessId(&wpid))) {
        out.win_pid = wpid;
      }
      UIA_HWND whwnd = nullptr;
      if (SUCCEEDED(top->get_CachedNativeWindowHandle(&whwnd))) {
        out.win_hwnd = hwnd_to_hex(whwnd);
        if (whwnd != nullptr) {
          HWND root_hwnd = GetAncestor(reinterpret_cast<HWND>(whwnd), GA_ROOT);
          read_win32_window_meta(root_hwnd != nullptr ? root_hwnd : reinterpret_cast<HWND>(whwnd), out);
        }
      }
      RECT wrc{0, 0, 0, 0};
      if (SUCCEEDED(top->get_CachedBoundingRectangle(&wrc))) {
        out.win_left = static_cast<int>(wrc.left);
        out.win_top = static_cast<int>(wrc.top);
        out.win_right = static_cast<int>(wrc.right);
        out.win_bottom = static_cast<int>(wrc.bottom);
      }
    }
  }
  if (fallback_hwnd != nullptr && IsWindow(fallback_hwnd)) {
    HWND root_hwnd = GetAncestor(fallback_hwnd, GA_ROOT);
    read_win32_window_meta(root_hwnd != nullptr ? root_hwnd : fallback_hwnd, out);
  }
}

std::string button_code_to_string(std::uint8_t b) {
  switch (b) {
    case 1U: return "left";
    case 2U: return "right";
    case 3U: return "middle";
    default: return "";
  }
}

std::string translate_vk_to_utf8(
    std::uint32_t vk,
    std::uint32_t scan_code,
    std::uint8_t mods,
    std::uint64_t hkl_val) {
  if (is_modifier_vk_code(vk)) {
    return "";
  }
  const bool has_ctrl = (mods & 1U) != 0U;
  const bool has_shift = (mods & 2U) != 0U;
  const bool has_alt = (mods & 4U) != 0U;
  const bool has_win = (mods & 8U) != 0U;
  // Treat pure Ctrl, pure Alt, or Win as non-text shortcut chords, but allow AltGr (Ctrl+Alt).
  if (has_win || (has_ctrl && !has_alt) || (has_alt && !has_ctrl)) {
    return "";
  }
  if (vk == VK_RETURN || vk == VK_TAB || vk == VK_ESCAPE || vk == VK_BACK) {
    return "";
  }
  BYTE kstate[256]{};
  if (has_shift) {
    kstate[VK_SHIFT] = 0x80;
    kstate[VK_LSHIFT] = 0x80;
  }
  if (has_ctrl && has_alt) {
    kstate[VK_CONTROL] = 0x80;
    kstate[VK_MENU] = 0x80;
  }
  if ((GetKeyState(VK_CAPITAL) & 0x0001) != 0) {
    kstate[VK_CAPITAL] = 0x01;
  }
  HKL layout = reinterpret_cast<HKL>(static_cast<std::uintptr_t>(hkl_val));
  if (layout == nullptr) {
    layout = GetKeyboardLayout(0U);
  }
  wchar_t wbuf[8]{};
  // Flag 0x04 preserves foreground dead-key/keyboard layout buffer state (Win10 1607+).
  const int rc = ToUnicodeEx(
      static_cast<UINT>(vk),
      static_cast<UINT>(scan_code),
      kstate,
      wbuf,
      8,
      0x04U,
      layout);
  if (rc <= 0) {
    return "";
  }
  if (rc == 1 && static_cast<unsigned int>(wbuf[0]) < 0x20U) {
    return "";
  }
  return wide_to_utf8(wbuf, rc);
}

std::string fallback_oem_key_name(std::uint32_t vk, std::uint64_t hkl_val) {
  std::string name = vk_code_to_key_name(vk);
  if (!name.empty() || is_modifier_vk_code(vk)) {
    return name;
  }
  HKL layout = reinterpret_cast<HKL>(static_cast<std::uintptr_t>(hkl_val));
  if (layout == nullptr) {
    layout = GetKeyboardLayout(0U);
  }
  const UINT ch = MapVirtualKeyExW(static_cast<UINT>(vk), MAPVK_VK_TO_CHAR, layout) & 0x7FFFU;
  if (ch >= 33U && ch <= 126U) {
    const char lower_c = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return std::string(1U, lower_c);
  }
  return "";
}

class DesktopRecorder {
 public:
  static DesktopRecorder& instance() {
    static DesktopRecorder rec;
    return rec;
  }

  void start(
      bool include_wheel,
      bool redact_passwords,
      bool install_stop_hotkey,
      std::uint32_t stop_mods,
      std::uint32_t stop_vk) {
    stop_internal();

    {
      std::lock_guard<std::mutex> lock(mu_);
      actions_.clear();
      redact_passwords_ = redact_passwords;
      stop_requested_.store(false, std::memory_order_release);
      active_.store(true, std::memory_order_release);
      start_tp_ = std::chrono::steady_clock::now();
    }

    std::uint32_t hook_err = 0U;
    if (!start_record_hooks(include_wheel, install_stop_hotkey, stop_mods, stop_vk, hook_err)) {
      active_.store(false, std::memory_order_release);
      cpp11::stop("Failed to start desktop recorder hooks (Win32 error %lu).", hook_err);
    }

    std::lock_guard<std::mutex> lock(mu_);
    resolver_thread_ = std::thread(&DesktopRecorder::resolver_loop, this);
  }

  void stop_and_drain(
      std::vector<RecordedAction>& out_actions,
      std::uint64_t& dropped_out,
      double& elapsed_out,
      bool& hotkey_stopped_out) {
    stop_record_hooks();
    hotkey_stopped_out = record_stop_hotkey_triggered();

    std::thread to_join;
    {
      std::lock_guard<std::mutex> lock(mu_);
      stop_requested_.store(true, std::memory_order_release);
      if (resolver_thread_.joinable()) {
        to_join = std::move(resolver_thread_);
      }
    }
    signal_record_event();
    if (to_join.joinable()) {
      to_join.join();
    }

    std::lock_guard<std::mutex> lock(mu_);
    out_actions.assign(actions_.begin(), actions_.end());
    actions_.clear();
    dropped_out = record_ring().dropped_total();
    const auto now_tp = std::chrono::steady_clock::now();
    elapsed_out = std::chrono::duration<double>(now_tp - start_tp_).count();
    active_.store(false, std::memory_order_release);
  }

  void status(
      bool& active_out,
      bool& hotkey_stopped_out,
      double& elapsed_out,
      int& captured_out,
      int& dropped_out) {
    std::lock_guard<std::mutex> lock(mu_);
    const bool hk = record_stop_hotkey_triggered();
    hotkey_stopped_out = hk;
    active_out = active_.load(std::memory_order_acquire) && !hk;
    if (active_.load(std::memory_order_acquire)) {
      elapsed_out = std::chrono::duration<double>(
                        std::chrono::steady_clock::now() - start_tp_)
                        .count();
    } else {
      elapsed_out = 0.0;
    }
    captured_out = static_cast<int>(actions_.size());
    dropped_out = static_cast<int>(record_ring().dropped_total());
  }

  void shutdown() {
    stop_internal();
  }

  ~DesktopRecorder() {
    try {
      stop_internal();
    } catch (...) {
    }
  }

 private:
  DesktopRecorder() = default;

  void stop_internal() {
    stop_record_hooks();
    std::thread to_join;
    {
      std::lock_guard<std::mutex> lock(mu_);
      stop_requested_.store(true, std::memory_order_release);
      if (resolver_thread_.joinable()) {
        to_join = std::move(resolver_thread_);
      }
    }
    signal_record_event();
    if (to_join.joinable()) {
      to_join.join();
    }
    std::lock_guard<std::mutex> lock(mu_);
    actions_.clear();
    active_.store(false, std::memory_order_release);
  }

  static ComPtr<IUIAutomationCacheRequest> build_cache_request(IUIAutomation* uia) {
    ComPtr<IUIAutomationCacheRequest> req;
    if (FAILED(uia->CreateCacheRequest(req.put())) || !req) {
      return {};
    }
    req->AddProperty(UIA_NamePropertyId);
    req->AddProperty(UIA_AutomationIdPropertyId);
    req->AddProperty(UIA_ControlTypePropertyId);
    req->AddProperty(UIA_ClassNamePropertyId);
    req->AddProperty(UIA_ProcessIdPropertyId);
    req->AddProperty(UIA_NativeWindowHandlePropertyId);
    req->AddProperty(UIA_BoundingRectanglePropertyId);
    req->AddProperty(UIA_IsPasswordPropertyId);
    req->AddPattern(UIA_TogglePatternId);
    req->AddPattern(UIA_SelectionItemPatternId);
    req->AddPattern(UIA_ValuePatternId);
    return req;
  }

  void resolve_batch(
      const std::vector<RawRecordEvent>& batch,
      IUIAutomation* uia,
      IUIAutomationCacheRequest* req,
      IUIAutomationElement* root,
      IUIAutomationTreeWalker* walker,
      ResolvedTarget& last_mouse_target,
      std::uint8_t& last_mouse_button,
      std::int64_t& last_mouse_ts,
      ResolvedTarget& focused_target,
      bool& focus_dirty,
      std::uint64_t& last_key_fg) {
    for (const RawRecordEvent& ev : batch) {
      const auto kind = static_cast<RecordEventKind>(ev.kind);
      if (kind == RecordEventKind::kKeyUp || kind == RecordEventKind::kNone) {
        continue;
      }

      RecordedAction act;
      act.timestamp_ms = ev.timestamp_ms;
      act.x = static_cast<int>(ev.x);
      act.y = static_cast<int>(ev.y);
      act.wheel_delta = static_cast<int>(ev.wheel_delta);
      act.horizontal = (ev.flags & 1U) != 0U;
      act.modifiers = static_cast<int>(ev.modifiers);

      ResolvedTarget target;
      if (kind == RecordEventKind::kMouseDown || kind == RecordEventKind::kMouseWheel) {
        act.kind = (kind == RecordEventKind::kMouseDown) ? "mouse_down" : "mouse_wheel";
        act.button = button_code_to_string(ev.button);
        if (uia != nullptr && req != nullptr) {
          ComPtr<IUIAutomationElement> el;
          POINT pt{static_cast<LONG>(ev.x), static_cast<LONG>(ev.y)};
          if (SUCCEEDED(uia->ElementFromPointBuildCache(pt, req, el.put())) && el) {
            HWND pt_hwnd = WindowFromPoint(pt);
            resolve_target_from_uia(uia, req, root, walker, el.get(), pt_hwnd, target);
          } else {
            HWND pt_hwnd = WindowFromPoint(pt);
            HWND root_hwnd = (pt_hwnd != nullptr) ? GetAncestor(pt_hwnd, GA_ROOT) : nullptr;
            read_win32_window_meta(root_hwnd != nullptr ? root_hwnd : pt_hwnd, target);
          }
        }
        if (kind == RecordEventKind::kMouseDown) {
          last_mouse_target = target;
          last_mouse_button = ev.button;
          last_mouse_ts = ev.timestamp_ms;
          focus_dirty = true;
        }
      } else if (kind == RecordEventKind::kMouseUp) {
        act.kind = "mouse_up";
        act.button = button_code_to_string(ev.button);
        // Reuse mouse_down target so popup-closing clicks retain the clicked control selector.
        if (last_mouse_target.valid &&
            last_mouse_button == ev.button &&
            (ev.timestamp_ms - last_mouse_ts) >= 0 &&
            (ev.timestamp_ms - last_mouse_ts) <= 5000) {
          target = last_mouse_target;
        } else if (uia != nullptr && req != nullptr) {
          ComPtr<IUIAutomationElement> el;
          POINT pt{static_cast<LONG>(ev.x), static_cast<LONG>(ev.y)};
          if (SUCCEEDED(uia->ElementFromPointBuildCache(pt, req, el.put())) && el) {
            HWND pt_hwnd = WindowFromPoint(pt);
            resolve_target_from_uia(uia, req, root, walker, el.get(), pt_hwnd, target);
          }
        }
        focus_dirty = true;
      } else if (kind == RecordEventKind::kKeyDown) {
        act.kind = "key_down";
        act.vk_code = static_cast<int>(ev.vk_code);
        act.scan_code = static_cast<int>(ev.scan_code);

        if (focus_dirty || ev.fg_hwnd != last_key_fg || !focused_target.valid) {
          focused_target = ResolvedTarget{};
          HWND fg = reinterpret_cast<HWND>(static_cast<std::uintptr_t>(ev.fg_hwnd));
          if (uia != nullptr && req != nullptr) {
            ComPtr<IUIAutomationElement> focused;
            if (SUCCEEDED(uia->GetFocusedElementBuildCache(req, focused.put())) && focused) {
              resolve_target_from_uia(uia, req, root, walker, focused.get(), fg, focused_target);
            } else {
              read_win32_window_meta(fg, focused_target);
            }
          } else {
            read_win32_window_meta(fg, focused_target);
          }
          focus_dirty = false;
          last_key_fg = ev.fg_hwnd;
        }
        target = focused_target;
        act.key_name = fallback_oem_key_name(ev.vk_code, ev.hkl);
        act.unicode_text = translate_vk_to_utf8(
            ev.vk_code, ev.scan_code, ev.modifiers, ev.hkl);

        if (redact_passwords_ && target.is_password) {
          const bool has_non_shift_mod =
              (ev.modifiers & (1U | 4U | 8U)) != 0U;
          if (!act.unicode_text.empty() ||
              (!has_non_shift_mod && !is_modifier_vk_code(ev.vk_code) &&
               ev.vk_code != VK_RETURN && ev.vk_code != VK_TAB &&
               ev.vk_code != VK_ESCAPE && ev.vk_code != VK_BACK)) {
            act.unicode_text.clear();
            act.key_name.clear();
            act.vk_code = 0;
          }
        }
      }

      act.win_title = target.win_title;
      act.win_class = target.win_class;
      act.win_hwnd = target.win_hwnd;
      act.win_pid = target.win_pid;
      act.win_left = target.win_left;
      act.win_top = target.win_top;
      act.win_right = target.win_right;
      act.win_bottom = target.win_bottom;
      act.el_name = target.el_name;
      act.el_automation_id = target.el_automation_id;
      act.el_control_type = target.el_control_type;
      act.el_class_name = target.el_class_name;
      act.el_hwnd = target.el_hwnd;
      act.el_pid = target.el_pid;
      act.el_left = target.el_left;
      act.el_top = target.el_top;
      act.el_right = target.el_right;
      act.el_bottom = target.el_bottom;
      act.is_password = target.is_password;
      act.has_toggle = target.has_toggle;
      act.has_select = target.has_select;
      act.has_value = target.has_value;

      if (kind == RecordEventKind::kKeyDown) {
        if (target.el_right > target.el_left && target.el_bottom > target.el_top) {
          act.x = (target.el_left + target.el_right) / 2;
          act.y = (target.el_top + target.el_bottom) / 2;
        }
      }
      if (target.win_right > target.win_left && target.win_bottom > target.win_top) {
        act.rel_x = act.x - target.win_left;
        act.rel_y = act.y - target.win_top;
      }

      std::lock_guard<std::mutex> lock(mu_);
      actions_.push_back(std::move(act));
    }
  }

  void resolver_loop() {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool com_init = SUCCEEDED(hr);

    ComPtr<IUIAutomation> uia;
    CoCreateInstance(
        CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER,
        IID_IUIAutomation, reinterpret_cast<void**>(uia.put()));

    ComPtr<IUIAutomationCacheRequest> req;
    ComPtr<IUIAutomationElement> root;
    ComPtr<IUIAutomationTreeWalker> walker;
    if (uia) {
      req = build_cache_request(uia.get());
      uia->GetRootElement(root.put());
      uia->get_ControlViewWalker(walker.put());
    }

    ResolvedTarget last_mouse_target;
    std::uint8_t last_mouse_button = 0U;
    std::int64_t last_mouse_ts = 0;
    ResolvedTarget focused_target;
    bool focus_dirty = true;
    std::uint64_t last_key_fg = 0ULL;
    std::vector<RawRecordEvent> batch;

    for (;;) {
      const bool stopping = stop_requested_.load(std::memory_order_acquire);
      record_ring().drain(batch);
      if (!batch.empty()) {
        resolve_batch(
            batch, uia.get(), req.get(), root.get(), walker.get(),
            last_mouse_target, last_mouse_button, last_mouse_ts,
            focused_target, focus_dirty, last_key_fg);
      }
      if (stopping) {
        // Final drain after hooks stopped guarantees zero left-behind raw events.
        record_ring().drain(batch);
        if (!batch.empty()) {
          resolve_batch(
              batch, uia.get(), req.get(), root.get(), walker.get(),
              last_mouse_target, last_mouse_button, last_mouse_ts,
              focused_target, focus_dirty, last_key_fg);
        }
        break;
      }
      wait_record_signal(50U);
    }

    walker.reset();
    root.reset();
    req.reset();
    uia.reset();
    if (com_init) {
      CoUninitialize();
    }
  }

  std::mutex mu_;
  std::thread resolver_thread_;
  std::deque<RecordedAction> actions_;
  std::atomic<bool> active_{false};
  std::atomic<bool> stop_requested_{false};
  bool redact_passwords_ = true;
  std::chrono::steady_clock::time_point start_tp_{};
};

cpp11::writable::data_frame actions_to_dataframe(const std::vector<RecordedAction>& acts) {
  const R_xlen_t n = static_cast<R_xlen_t>(acts.size());
  cpp11::writable::strings kind(n);
  cpp11::writable::doubles timestamp_ms(n);
  cpp11::writable::integers x(n);
  cpp11::writable::integers y(n);
  cpp11::writable::integers rel_x(n);
  cpp11::writable::integers rel_y(n);
  cpp11::writable::integers wheel_delta(n);
  cpp11::writable::logicals horizontal(n);
  cpp11::writable::strings button(n);
  cpp11::writable::integers vk_code(n);
  cpp11::writable::integers scan_code(n);
  cpp11::writable::integers modifiers(n);
  cpp11::writable::strings key_name(n);
  cpp11::writable::strings unicode_text(n);
  cpp11::writable::strings win_title(n);
  cpp11::writable::strings win_class(n);
  cpp11::writable::strings win_hwnd(n);
  cpp11::writable::integers win_pid(n);
  cpp11::writable::integers win_left(n);
  cpp11::writable::integers win_top(n);
  cpp11::writable::integers win_right(n);
  cpp11::writable::integers win_bottom(n);
  cpp11::writable::strings el_name(n);
  cpp11::writable::strings el_automation_id(n);
  cpp11::writable::strings el_control_type(n);
  cpp11::writable::strings el_class_name(n);
  cpp11::writable::strings el_hwnd(n);
  cpp11::writable::integers el_pid(n);
  cpp11::writable::integers el_left(n);
  cpp11::writable::integers el_top(n);
  cpp11::writable::integers el_right(n);
  cpp11::writable::integers el_bottom(n);
  cpp11::writable::logicals is_password(n);
  cpp11::writable::logicals has_toggle(n);
  cpp11::writable::logicals has_select(n);
  cpp11::writable::logicals has_value(n);

  for (R_xlen_t i = 0; i < n; ++i) {
    const auto& a = acts[static_cast<std::size_t>(i)];
    kind[i] = a.kind;
    timestamp_ms[i] = static_cast<double>(a.timestamp_ms);
    x[i] = a.x;
    y[i] = a.y;
    rel_x[i] = a.rel_x;
    rel_y[i] = a.rel_y;
    wheel_delta[i] = a.wheel_delta;
    horizontal[i] = a.horizontal;
    button[i] = a.button;
    vk_code[i] = a.vk_code;
    scan_code[i] = a.scan_code;
    modifiers[i] = a.modifiers;
    key_name[i] = a.key_name;
    unicode_text[i] = a.unicode_text;
    win_title[i] = a.win_title;
    win_class[i] = a.win_class;
    win_hwnd[i] = a.win_hwnd;
    win_pid[i] = a.win_pid;
    win_left[i] = a.win_left;
    win_top[i] = a.win_top;
    win_right[i] = a.win_right;
    win_bottom[i] = a.win_bottom;
    el_name[i] = a.el_name;
    el_automation_id[i] = a.el_automation_id;
    el_control_type[i] = a.el_control_type;
    el_class_name[i] = a.el_class_name;
    el_hwnd[i] = a.el_hwnd;
    el_pid[i] = a.el_pid;
    el_left[i] = a.el_left;
    el_top[i] = a.el_top;
    el_right[i] = a.el_right;
    el_bottom[i] = a.el_bottom;
    is_password[i] = a.is_password;
    has_toggle[i] = a.has_toggle;
    has_select[i] = a.has_select;
    has_value[i] = a.has_value;
  }

  using namespace cpp11::literals;
  return cpp11::writable::data_frame({
      "kind"_nm = kind,
      "timestamp_ms"_nm = timestamp_ms,
      "x"_nm = x,
      "y"_nm = y,
      "rel_x"_nm = rel_x,
      "rel_y"_nm = rel_y,
      "wheel_delta"_nm = wheel_delta,
      "horizontal"_nm = horizontal,
      "button"_nm = button,
      "vk_code"_nm = vk_code,
      "scan_code"_nm = scan_code,
      "modifiers"_nm = modifiers,
      "key_name"_nm = key_name,
      "unicode_text"_nm = unicode_text,
      "win_title"_nm = win_title,
      "win_class"_nm = win_class,
      "win_hwnd"_nm = win_hwnd,
      "win_pid"_nm = win_pid,
      "win_left"_nm = win_left,
      "win_top"_nm = win_top,
      "win_right"_nm = win_right,
      "win_bottom"_nm = win_bottom,
      "el_name"_nm = el_name,
      "el_automation_id"_nm = el_automation_id,
      "el_control_type"_nm = el_control_type,
      "el_class_name"_nm = el_class_name,
      "el_hwnd"_nm = el_hwnd,
      "el_pid"_nm = el_pid,
      "el_left"_nm = el_left,
      "el_top"_nm = el_top,
      "el_right"_nm = el_right,
      "el_bottom"_nm = el_bottom,
      "is_password"_nm = is_password,
      "has_toggle"_nm = has_toggle,
      "has_select"_nm = has_select,
      "has_value"_nm = has_value
  });
}

}  // namespace

void stop_desktop_recorder() {
  DesktopRecorder::instance().shutdown();
}

}  // namespace winrpa

#else

namespace winrpa {

void stop_desktop_recorder() {}

}  // namespace winrpa

#endif  // _WIN32

[[cpp11::register]]
bool cpp_record_start(
    bool include_wheel,
    bool redact_passwords,
    bool install_stop_hotkey,
    int stop_mods,
    int stop_vk) {
#ifdef _WIN32
  winrpa::DesktopRecorder::instance().start(
      include_wheel,
      redact_passwords,
      install_stop_hotkey,
      static_cast<std::uint32_t>(stop_mods),
      static_cast<std::uint32_t>(stop_vk));
  return true;
#else
  (void)include_wheel;
  (void)redact_passwords;
  (void)install_stop_hotkey;
  (void)stop_mods;
  (void)stop_vk;
  winrpa::require_windows_or_stop("cpp_record_start()");
  return false;
#endif
}

[[cpp11::register]]
cpp11::writable::list cpp_record_stop() {
#ifdef _WIN32
  std::vector<winrpa::RecordedAction> acts;
  std::uint64_t dropped = 0ULL;
  double elapsed = 0.0;
  bool hotkey_stopped = false;
  winrpa::DesktopRecorder::instance().stop_and_drain(
      acts, dropped, elapsed, hotkey_stopped);

  using namespace cpp11::literals;
  return cpp11::writable::list({
      "events"_nm = winrpa::actions_to_dataframe(acts),
      "dropped"_nm = static_cast<int>(dropped),
      "elapsed_sec"_nm = elapsed,
      "stopped_by_hotkey"_nm = hotkey_stopped
  });
#else
  winrpa::require_windows_or_stop("cpp_record_stop()");
  return cpp11::writable::list();
#endif
}

[[cpp11::register]]
cpp11::writable::list cpp_record_status() {
#ifdef _WIN32
  bool active = false;
  bool hotkey_stopped = false;
  double elapsed = 0.0;
  int captured = 0;
  int dropped = 0;
  winrpa::DesktopRecorder::instance().status(
      active, hotkey_stopped, elapsed, captured, dropped);

  using namespace cpp11::literals;
  return cpp11::writable::list({
      "active"_nm = active,
      "stopped_by_hotkey"_nm = hotkey_stopped,
      "elapsed_sec"_nm = elapsed,
      "captured_events"_nm = captured,
      "dropped_events"_nm = dropped
  });
#else
  using namespace cpp11::literals;
  return cpp11::writable::list({
      "active"_nm = false,
      "stopped_by_hotkey"_nm = false,
      "elapsed_sec"_nm = 0.0,
      "captured_events"_nm = 0,
      "dropped_events"_nm = 0
  });
#endif
}

[[cpp11::register]]
cpp11::writable::list cpp_record_system_metrics() {
#ifdef _WIN32
  const int dbl_ms = static_cast<int>(GetDoubleClickTime());
  const int drag_cx = std::max(4, static_cast<int>(GetSystemMetrics(SM_CXDRAG)));
  const int drag_cy = std::max(4, static_cast<int>(GetSystemMetrics(SM_CYDRAG)));
  const DWORD self_pid = GetCurrentProcessId();
  const DWORD parent_pid = winrpa::get_parent_pid_win32(self_pid);

  using namespace cpp11::literals;
  return cpp11::writable::list({
      "double_click_ms"_nm = dbl_ms > 0 ? dbl_ms : 500,
      "drag_cx"_nm = drag_cx,
      "drag_cy"_nm = drag_cy,
      "self_pid"_nm = static_cast<int>(self_pid),
      "parent_pid"_nm = static_cast<int>(parent_pid)
  });
#else
  using namespace cpp11::literals;
  return cpp11::writable::list({
      "double_click_ms"_nm = 500,
      "drag_cx"_nm = 4,
      "drag_cy"_nm = 4,
      "self_pid"_nm = 0,
      "parent_pid"_nm = 0
  });
#endif
}

[[cpp11::register]]
cpp11::writable::list cpp_record_ring_selftest(int n_push) {
  winrpa::RecordEventRingBuffer ring;
  const int n = n_push < 0 ? 0 : n_push;
  for (int i = 1; i <= n; ++i) {
    winrpa::RawRecordEvent ev;
    ev.kind = static_cast<std::uint8_t>(winrpa::RecordEventKind::kMouseDown);
    ev.button = 1U;
    ev.x = i * 2;
    ev.y = i * 3;
    ev.vk_code = static_cast<std::uint32_t>(i);
    ev.timestamp_ms = static_cast<std::int64_t>(i * 15);
    ring.push(ev);
  }
  const int pre_drain_dropped = static_cast<int>(ring.dropped_total());
  std::vector<winrpa::RawRecordEvent> drained;
  const std::uint64_t batch_dropped = ring.drain(drained);

  cpp11::writable::integers xs(static_cast<R_xlen_t>(drained.size()));
  for (std::size_t i = 0U; i < drained.size(); ++i) {
    xs[static_cast<R_xlen_t>(i)] = static_cast<int>(drained[i].x);
  }
  using namespace cpp11::literals;
  return cpp11::writable::list({
      "capacity"_nm = static_cast<int>(winrpa::RecordEventRingBuffer::kCapacity),
      "pushed"_nm = n,
      "pre_drain_dropped"_nm = pre_drain_dropped,
      "batch_dropped"_nm = static_cast<int>(batch_dropped),
      "total_dropped"_nm = static_cast<int>(ring.dropped_total()),
      "count"_nm = static_cast<int>(drained.size()),
      "xs"_nm = xs
  });
}

[[cpp11::register]]
cpp11::writable::list cpp_record_vk_selftest() {
  using namespace cpp11::literals;
  return cpp11::writable::list({
      "mod_ctrl"_nm = winrpa::is_modifier_vk_code(0x11U),
      "mod_lshift"_nm = winrpa::is_modifier_vk_code(0xA0U),
      "mod_a"_nm = winrpa::is_modifier_vk_code(0x41U),
      "enter"_nm = winrpa::vk_code_to_key_name(0x0DU),
      "tab"_nm = winrpa::vk_code_to_key_name(0x09U),
      "esc"_nm = winrpa::vk_code_to_key_name(0x1BU),
      "backspace"_nm = winrpa::vk_code_to_key_name(0x08U),
      "f1"_nm = winrpa::vk_code_to_key_name(0x70U),
      "f12"_nm = winrpa::vk_code_to_key_name(0x7BU),
      "key_s"_nm = winrpa::vk_code_to_key_name(0x53U),
      "key_7"_nm = winrpa::vk_code_to_key_name(0x37U)
  });
}
