#ifndef R_NO_REMAP
#define R_NO_REMAP
#endif
#include <cpp11.hpp>
#include "mta_worker.h"
#include "capture_hooks.h"
#include "winrpa_core.h"

#include <stdexcept>
#include <vector>

#ifdef _WIN32

namespace winrpa {

MtaComWorker& MtaComWorker::instance() {
  static MtaComWorker worker;
  return worker;
}

void MtaComWorker::ensure_started_locked() {
  if (!started_) {
    stop_ = false;
    thread_ = std::thread(&MtaComWorker::worker_loop, this);
    started_ = true;
  }
}

IUIAutomation* MtaComWorker::ensure_uia() {
  if (uia_ == nullptr) {
    HRESULT hr = CoCreateInstance(
        CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER,
        IID_IUIAutomation, reinterpret_cast<void**>(&uia_));
    if (FAILED(hr) || uia_ == nullptr) {
      uia_ = nullptr;
      throw std::runtime_error("Failed to create IUIAutomation COM instance.");
    }
  }
  return uia_;
}

void MtaComWorker::worker_loop() {
  HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  const bool com_initialized = SUCCEEDED(hr);
  for (;;) {
    std::function<void()> job;
    {
      std::unique_lock<std::mutex> lock(mu_);
      cv_.wait(lock, [this]() { return stop_ || !tasks_.empty(); });
      if (stop_ && tasks_.empty()) {
        break;
      }
      job = std::move(tasks_.front());
      tasks_.pop();
    }
    try {
      job();
    } catch (...) {
    }
  }
  if (uia_ != nullptr) {
    uia_->Release();
    uia_ = nullptr;
  }
  if (com_initialized) {
    CoUninitialize();
  }
}

void MtaComWorker::post(std::function<void()> fn) {
  std::lock_guard<std::mutex> lock(mu_);
  if (destroyed_ || !started_) {
    return;
  }
  tasks_.push(std::move(fn));
  cv_.notify_one();
}

void MtaComWorker::shutdown() {
  std::thread to_join;
  {
    std::lock_guard<std::mutex> lock(mu_);
    if (!started_) {
      return;
    }
    stop_ = true;
    cv_.notify_all();
    to_join = std::move(thread_);
    started_ = false;
  }
  if (to_join.joinable()) {
    to_join.join();
  }
}

MtaComWorker::~MtaComWorker() {
  try {
    std::lock_guard<std::mutex> lock(mu_);
    destroyed_ = true;
    if (started_) {
      stop_ = true;
      cv_.notify_all();
      if (thread_.joinable()) {
        thread_.detach();
      }
      started_ = false;
    }
  } catch (...) {
  }
}

void release_uia_element(UiaElementHandle* h) {
  if (h == nullptr) {
    return;
  }
  IUIAutomationElement* ptr = h->elem;
  h->elem = nullptr;
  delete h;
  if (ptr != nullptr) {
    MtaComWorker::instance().post([ptr]() {
      ptr->Release();
    });
  }
}

}  // namespace winrpa

#endif  // _WIN32

[[cpp11::register]]
void cpp_shutdown_runtime() {
  winrpa::stop_runtime_hooks();
#ifdef _WIN32
  winrpa::MtaComWorker::instance().shutdown();
#endif
}

[[cpp11::register]]
bool cpp_is_interactive_desktop() {
#ifdef _WIN32
  HDESK hdesk = OpenInputDesktop(0, FALSE, DESKTOP_READOBJECTS);
  if (hdesk != nullptr) {
    CloseDesktop(hdesk);
    return true;
  }
  return false;
#else
  return false;
#endif
}

[[cpp11::register]]
bool cpp_abort_triggered() {
  return winrpa::g_abort_requested.load(std::memory_order_acquire);
}

[[cpp11::register]]
void cpp_reset_abort() {
  winrpa::g_abort_requested.store(false, std::memory_order_release);
}

[[cpp11::register]]
void cpp_trigger_abort() {
  winrpa::g_abort_requested.store(true, std::memory_order_release);
}

[[cpp11::register]]
cpp11::writable::integers cpp_normalize_virtual_coords(
    int x, int y, int vx, int vy, int vw, int vh) {
  const auto out = winrpa::normalize_virtual_desktop(x, y, vx, vy, vw, vh);
  cpp11::writable::integers res(2);
  res[0] = out.norm_x;
  res[1] = out.norm_y;
  return res;
}

[[cpp11::register]]
cpp11::writable::list cpp_test_modifier_guard_lifecycle(
    cpp11::integers vks, bool simulate_error) {
  std::vector<int> released;
  bool unwound = false;
  try {
    winrpa::ModifierGuard guard([&released](std::uint16_t vk) {
      released.push_back(static_cast<int>(vk));
    });
    for (R_xlen_t i = 0; i < vks.size(); ++i) {
      guard.push(static_cast<std::uint16_t>(vks[i]));
    }
    if (simulate_error) {
      throw std::runtime_error("simulated unwind");
    }
    guard.release_all();
  } catch (const std::runtime_error&) {
    unwound = true;
  }

  cpp11::writable::integers r_released(static_cast<R_xlen_t>(released.size()));
  for (std::size_t i = 0; i < released.size(); ++i) {
    r_released[static_cast<R_xlen_t>(i)] = released[i];
  }
  using namespace cpp11::literals;
  return cpp11::writable::list({
      "unwound"_nm = unwound,
      "released"_nm = r_released
  });
}

[[cpp11::register]]
cpp11::writable::list cpp_ring_buffer_selftest(int n_push) {
  winrpa::HookEventRingBuffer ring;
  const int n = n_push < 0 ? 0 : n_push;
  for (int i = 1; i <= n; ++i) {
    winrpa::HookEvent ev;
    ev.vk_code = static_cast<std::uint32_t>(i);
    ev.scan_code = static_cast<std::uint32_t>(i & 0xFF);
    ev.key_up = (i % 2) == 0;
    ev.timestamp_ms = static_cast<std::int64_t>(i * 10);
    ring.push(ev);
  }
  std::vector<winrpa::HookEvent> drained;
  const std::uint64_t dropped = ring.drain(drained);
  cpp11::writable::integers vks(static_cast<R_xlen_t>(drained.size()));
  for (std::size_t i = 0; i < drained.size(); ++i) {
    vks[static_cast<R_xlen_t>(i)] = static_cast<int>(drained[i].vk_code);
  }
  using namespace cpp11::literals;
  return cpp11::writable::list({
      "capacity"_nm = static_cast<int>(winrpa::HookEventRingBuffer::kCapacity),
      "pushed"_nm = n,
      "dropped"_nm = static_cast<int>(dropped),
      "count"_nm = static_cast<int>(drained.size()),
      "vk_codes"_nm = vks
  });
}

[[cpp11::register]]
cpp11::writable::raws cpp_encode_bmp_test(int width, int height) {
  if (width <= 0 || height <= 0) {
    cpp11::stop("width and height must be positive integers.");
  }
  const std::size_t pixel_count =
      static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
  std::vector<std::uint8_t> bgra(pixel_count * 4U, 0U);
  for (std::size_t i = 0; i < pixel_count; ++i) {
    bgra[i * 4U + 0U] = static_cast<std::uint8_t>((i * 3U) & 0xFFU);
    bgra[i * 4U + 1U] = static_cast<std::uint8_t>((i * 5U) & 0xFFU);
    bgra[i * 4U + 2U] = static_cast<std::uint8_t>((i * 7U) & 0xFFU);
    bgra[i * 4U + 3U] = 255U;
  }
  const auto bytes = winrpa::encode_bgr_bmp(width, height, bgra.data());
  cpp11::writable::raws out(static_cast<R_xlen_t>(bytes.size()));
  for (std::size_t i = 0; i < bytes.size(); ++i) {
    out[static_cast<R_xlen_t>(i)] = static_cast<Rbyte>(bytes[i]);
  }
  return out;
}
