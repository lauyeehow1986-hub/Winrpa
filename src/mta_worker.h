#ifndef WINRPA_MTA_WORKER_H
#define WINRPA_MTA_WORKER_H

#ifndef R_NO_REMAP
#define R_NO_REMAP
#endif

#include <cpp11.hpp>
#include "winrpa_core.h"

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
#include <uiautomation.h>
#include <condition_variable>
#include <functional>
#include <future>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <utility>

namespace winrpa {

template <typename T>
class ComPtr {
 public:
  ComPtr() noexcept : ptr_(nullptr) {}
  explicit ComPtr(T* ptr) noexcept : ptr_(ptr) {}
  ComPtr(const ComPtr&) = delete;
  ComPtr& operator=(const ComPtr&) = delete;

  ComPtr(ComPtr&& other) noexcept : ptr_(other.ptr_) {
    other.ptr_ = nullptr;
  }

  ComPtr& operator=(ComPtr&& other) noexcept {
    if (this != &other) {
      reset();
      ptr_ = other.ptr_;
      other.ptr_ = nullptr;
    }
    return *this;
  }

  ~ComPtr() { reset(); }

  void reset(T* p = nullptr) noexcept {
    if (ptr_ != nullptr) {
      ptr_->Release();
    }
    ptr_ = p;
  }

  T* detach() noexcept {
    T* p = ptr_;
    ptr_ = nullptr;
    return p;
  }

  T* get() const noexcept { return ptr_; }
  T* operator->() const noexcept { return ptr_; }
  T** put() noexcept {
    reset();
    return &ptr_;
  }
  explicit operator bool() const noexcept { return ptr_ != nullptr; }

 private:
  T* ptr_;
};

class BstrGuard {
 public:
  BstrGuard() noexcept : bstr_(nullptr) {}
  BstrGuard(const BstrGuard&) = delete;
  BstrGuard& operator=(const BstrGuard&) = delete;
  ~BstrGuard() { reset(); }

  void reset() noexcept {
    if (bstr_ != nullptr) {
      SysFreeString(bstr_);
      bstr_ = nullptr;
    }
  }

  BSTR* put() noexcept {
    reset();
    return &bstr_;
  }

  BSTR get() const noexcept { return bstr_; }

 private:
  BSTR bstr_;
};

inline std::wstring utf8_to_wide(const std::string& utf8) {
  if (utf8.empty()) {
    return {};
  }
  const int len = MultiByteToWideChar(
      CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
  if (len <= 0) {
    return {};
  }
  std::wstring wide(static_cast<std::size_t>(len), L'\0');
  MultiByteToWideChar(
      CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), wide.data(), len);
  return wide;
}

inline std::string wide_to_utf8(const wchar_t* wide, int len = -1) {
  if (wide == nullptr) {
    return {};
  }
  if (len == 0) {
    return {};
  }
  const int needed = WideCharToMultiByte(
      CP_UTF8, 0, wide, len, nullptr, 0, nullptr, nullptr);
  if (needed <= 0) {
    return {};
  }
  std::string out(static_cast<std::size_t>(needed), '\0');
  WideCharToMultiByte(
      CP_UTF8, 0, wide, len, out.data(), needed, nullptr, nullptr);
  if (len < 0 && !out.empty() && out.back() == '\0') {
    out.pop_back();
  }
  return out;
}

inline std::string bstr_to_utf8(BSTR bstr) {
  if (bstr == nullptr) {
    return {};
  }
  const UINT len = SysStringLen(bstr);
  if (len == 0U) {
    return {};
  }
  return wide_to_utf8(bstr, static_cast<int>(len));
}

class MtaComWorker {
 public:
  static MtaComWorker& instance();

  template <typename Fn>
  auto submit(Fn&& fn) -> decltype(fn(std::declval<IUIAutomation*>())) {
    using Ret = decltype(fn(std::declval<IUIAutomation*>()));
    ensure_started();
    auto task = std::make_shared<std::packaged_task<Ret()>>(
        [this, f = std::forward<Fn>(fn)]() mutable {
          return f(ensure_uia());
        });
    std::future<Ret> fut = task->get_future();
    {
      std::lock_guard<std::mutex> lock(mu_);
      if (shut_down_) {
        throw std::runtime_error("winrpa MTA COM worker is shut down.");
      }
      tasks_.push([task]() { (*task)(); });
    }
    cv_.notify_one();
    return fut.get();
  }

  void post(std::function<void()> fn);
  void shutdown();

  ~MtaComWorker();

 private:
  MtaComWorker() = default;
  void ensure_started();
  IUIAutomation* ensure_uia();
  void worker_loop();

  std::mutex mu_;
  std::condition_variable cv_;
  std::queue<std::function<void()>> tasks_;
  std::thread thread_;
  IUIAutomation* uia_ = nullptr;
  bool started_ = false;
  bool stop_ = false;
  bool shut_down_ = false;
};

struct UiaElementHandle {
  IUIAutomationElement* elem = nullptr;
};

void release_uia_element(UiaElementHandle* h);
using UiaElementXPtr = cpp11::external_pointer<UiaElementHandle, release_uia_element>;

}  // namespace winrpa

#else

namespace winrpa {

struct UiaElementHandle {
  void* elem = nullptr;
};

inline void release_uia_element(UiaElementHandle* h) {
  delete h;
}

using UiaElementXPtr = cpp11::external_pointer<UiaElementHandle, release_uia_element>;

}  // namespace winrpa

#endif  // _WIN32

#endif  // WINRPA_MTA_WORKER_H
