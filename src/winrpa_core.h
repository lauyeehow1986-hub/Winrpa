#ifndef WINRPA_CORE_H
#define WINRPA_CORE_H

#ifndef R_NO_REMAP
#define R_NO_REMAP
#endif

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>
#include <cpp11/protect.hpp>

namespace winrpa {

// Global lock-free abort flag toggled by OS emergency hotkey or R test helper.
inline std::atomic<bool> g_abort_requested{false};

struct VirtualCoord {
  std::int32_t norm_x;
  std::int32_t norm_y;
};

// Pure math helper (testable headlessly on all OSes, including CRAN check servers).
// Maps desktop pixel (x, y) into [0, 65535] across the full Virtual Desktop,
// supporting negative coordinates on monitors left/above the primary display.
inline VirtualCoord normalize_virtual_desktop(
    int x, int y, int vx, int vy, int vw, int vh) {
  if (vw <= 1 || vh <= 1) {
    return {0, 0};
  }
  double nx = (static_cast<double>(x - vx) * 65535.0) / static_cast<double>(vw - 1);
  double ny = (static_cast<double>(y - vy) * 65535.0) / static_cast<double>(vh - 1);
  if (nx < 0.0) nx = 0.0;
  if (nx > 65535.0) nx = 65535.0;
  if (ny < 0.0) ny = 0.0;
  if (ny > 65535.0) ny = 65535.0;
  return {
      static_cast<std::int32_t>(nx + 0.5),
      static_cast<std::int32_t>(ny + 0.5)
  };
}

inline int scale_logical_to_physical(int px, int dpi) {
  if (dpi <= 0) {
    return px;
  }
  const double scaled = (static_cast<double>(px) * static_cast<double>(dpi)) / 96.0;
  return static_cast<int>(scaled >= 0.0 ? scaled + 0.5 : scaled - 0.5);
}

inline int scale_physical_to_logical(int px, int dpi) {
  if (dpi <= 0) {
    return px;
  }
  const double scaled = (static_cast<double>(px) * 96.0) / static_cast<double>(dpi);
  return static_cast<int>(scaled >= 0.0 ? scaled + 0.5 : scaled - 0.5);
}

// RAII modifier guard ensuring pressed modifier keys release in LIFO order
// even when C++ exceptions or R interrupts unwind the stack.
class ModifierGuard {
 public:
  using ReleaseFn = std::function<void(std::uint16_t)>;

  explicit ModifierGuard(ReleaseFn release_fn)
      : release_fn_(std::move(release_fn)) {}

  ModifierGuard(const ModifierGuard&) = delete;
  ModifierGuard& operator=(const ModifierGuard&) = delete;

  ~ModifierGuard() {
    try {
      release_all();
    } catch (...) {
    }
  }

  void push(std::uint16_t vk) {
    pressed_.push_back(vk);
  }

  void release_all() {
    while (!pressed_.empty()) {
      const std::uint16_t vk = pressed_.back();
      pressed_.pop_back();
      if (release_fn_) {
        release_fn_(vk);
      }
    }
  }

 private:
  ReleaseFn release_fn_;
  std::vector<std::uint16_t> pressed_;
};

struct HookEvent {
  std::uint32_t vk_code;
  std::uint32_t scan_code;
  bool key_up;
  std::int64_t timestamp_ms;
};

// Lock-free, allocation-free ring buffer for WH_KEYBOARD_LL hook callbacks.
class HookEventRingBuffer {
 public:
  static constexpr std::size_t kCapacity = 256;

  void push(const HookEvent& ev) noexcept {
    const std::uint64_t w = write_seq_.load(std::memory_order_relaxed);
    AtomicSlot& slot = slots_[w % kCapacity];
    slot.seq.store(w * 2ULL + 1ULL, std::memory_order_release);

    const std::uint64_t packed =
        static_cast<std::uint64_t>(ev.vk_code) |
        (static_cast<std::uint64_t>(ev.scan_code & 0x7FFFFFFFU) << 32) |
        (ev.key_up ? (1ULL << 63) : 0ULL);

    slot.packed.store(packed, std::memory_order_relaxed);
    slot.timestamp_ms.store(ev.timestamp_ms, std::memory_order_relaxed);
    slot.seq.store((w + 1ULL) * 2ULL, std::memory_order_release);
    write_seq_.store(w + 1ULL, std::memory_order_release);
  }

  std::uint64_t drain(std::vector<HookEvent>& out) {
    out.clear();
    const std::uint64_t w = write_seq_.load(std::memory_order_acquire);
    std::uint64_t r = read_seq_.load(std::memory_order_relaxed);
    std::uint64_t dropped = 0ULL;
    const std::uint64_t cap = static_cast<std::uint64_t>(kCapacity);
    if (w > r + cap) {
      dropped = w - (r + cap);
      r = w - cap;
    }
    out.reserve(static_cast<std::size_t>(w - r));
    for (std::uint64_t s = r; s < w; ++s) {
      const AtomicSlot& slot = slots_[s % kCapacity];
      const std::uint64_t s1 = slot.seq.load(std::memory_order_acquire);
      const std::uint64_t packed = slot.packed.load(std::memory_order_relaxed);
      const std::int64_t ts = slot.timestamp_ms.load(std::memory_order_relaxed);
      const std::uint64_t s2 = slot.seq.load(std::memory_order_acquire);
      if ((s1 & 1ULL) == 0ULL && s1 == s2 && s1 == (s + 1ULL) * 2ULL) {
        HookEvent ev;
        ev.vk_code = static_cast<std::uint32_t>(packed & 0xFFFFFFFFULL);
        ev.scan_code = static_cast<std::uint32_t>((packed >> 32) & 0x7FFFFFFFULL);
        ev.key_up = ((packed >> 63) & 1ULL) != 0ULL;
        ev.timestamp_ms = ts;
        out.push_back(ev);
      }
    }
    read_seq_.store(w, std::memory_order_release);
    return dropped;
  }

  void clear() {
    const std::uint64_t w = write_seq_.load(std::memory_order_acquire);
    read_seq_.store(w, std::memory_order_release);
  }

  std::uint64_t total_pushed() const noexcept {
    return write_seq_.load(std::memory_order_acquire);
  }

 private:
  struct AtomicSlot {
    std::atomic<std::uint64_t> seq{0ULL};
    std::atomic<std::uint64_t> packed{0ULL};
    std::atomic<std::int64_t> timestamp_ms{0LL};
  };

  AtomicSlot slots_[kCapacity]{};
  std::atomic<std::uint64_t> write_seq_{0ULL};
  std::atomic<std::uint64_t> read_seq_{0ULL};
};

enum class RecordEventKind : std::uint8_t {
  kNone = 0,
  kMouseDown = 1,
  kMouseUp = 2,
  kMouseWheel = 3,
  kKeyDown = 4,
  kKeyUp = 5
};

struct RawRecordEvent {
  std::uint8_t kind = 0U;
  std::uint8_t button = 0U;
  std::uint8_t modifiers = 0U;
  std::uint8_t flags = 0U;
  std::int32_t x = 0;
  std::int32_t y = 0;
  std::int32_t wheel_delta = 0;
  std::uint32_t vk_code = 0U;
  std::uint32_t scan_code = 0U;
  std::int64_t timestamp_ms = 0;
  std::uint64_t fg_hwnd = 0ULL;
  std::uint64_t hkl = 0ULL;
};

// Lock-free SPSC seqlock ring buffer for WH_MOUSE_LL + WH_KEYBOARD_LL recorder hooks.
class RecordEventRingBuffer {
 public:
  static constexpr std::size_t kCapacity = 512;

  void push(const RawRecordEvent& ev) noexcept {
    const std::uint64_t w = write_seq_.load(std::memory_order_relaxed);
    AtomicSlot& slot = slots_[w % kCapacity];
    slot.seq.store(w * 2ULL + 1ULL, std::memory_order_release);
    std::atomic_thread_fence(std::memory_order_release);
    slot.ev.kind = ev.kind;
    slot.ev.button = ev.button;
    slot.ev.modifiers = ev.modifiers;
    slot.ev.flags = ev.flags;
    slot.ev.x = ev.x;
    slot.ev.y = ev.y;
    slot.ev.wheel_delta = ev.wheel_delta;
    slot.ev.vk_code = ev.vk_code;
    slot.ev.scan_code = ev.scan_code;
    slot.ev.timestamp_ms = ev.timestamp_ms;
    slot.ev.fg_hwnd = ev.fg_hwnd;
    slot.ev.hkl = ev.hkl;
    slot.seq.store((w + 1ULL) * 2ULL, std::memory_order_release);
    write_seq_.store(w + 1ULL, std::memory_order_release);
  }

  std::uint64_t drain(std::vector<RawRecordEvent>& out) {
    out.clear();
    const std::uint64_t w = write_seq_.load(std::memory_order_acquire);
    std::uint64_t r = read_seq_.load(std::memory_order_relaxed);
    std::uint64_t dropped = 0ULL;
    const std::uint64_t cap = static_cast<std::uint64_t>(kCapacity);
    if (w > r + cap) {
      dropped = w - (r + cap);
      r = w - cap;
    }
    out.reserve(static_cast<std::size_t>(w - r));
    for (std::uint64_t s = r; s < w; ++s) {
      const AtomicSlot& slot = slots_[s % kCapacity];
      const std::uint64_t s1 = slot.seq.load(std::memory_order_acquire);
      RawRecordEvent copy = slot.ev;
      std::atomic_thread_fence(std::memory_order_acquire);
      const std::uint64_t s2 = slot.seq.load(std::memory_order_acquire);
      if ((s1 & 1ULL) == 0ULL && s1 == s2 && s1 == (s + 1ULL) * 2ULL) {
        out.push_back(copy);
      } else {
        ++dropped;
      }
    }
    if (dropped > 0ULL) {
      dropped_total_.fetch_add(dropped, std::memory_order_relaxed);
    }
    read_seq_.store(w, std::memory_order_release);
    return dropped;
  }

  void clear() {
    const std::uint64_t w = write_seq_.load(std::memory_order_acquire);
    read_seq_.store(w, std::memory_order_release);
    dropped_total_.store(0ULL, std::memory_order_release);
  }

  std::uint64_t dropped_total() const noexcept {
    const std::uint64_t base = dropped_total_.load(std::memory_order_acquire);
    const std::uint64_t w = write_seq_.load(std::memory_order_acquire);
    const std::uint64_t r = read_seq_.load(std::memory_order_acquire);
    const std::uint64_t cap = static_cast<std::uint64_t>(kCapacity);
    const std::uint64_t unconsumed = (w > r + cap) ? (w - (r + cap)) : 0ULL;
    return base + unconsumed;
  }

  std::uint64_t total_pushed() const noexcept {
    return write_seq_.load(std::memory_order_acquire);
  }

 private:
  struct AtomicSlot {
    std::atomic<std::uint64_t> seq{0ULL};
    RawRecordEvent ev{};
  };

  AtomicSlot slots_[kCapacity]{};
  std::atomic<std::uint64_t> write_seq_{0ULL};
  std::atomic<std::uint64_t> read_seq_{0ULL};
  std::atomic<std::uint64_t> dropped_total_{0ULL};
};

inline bool is_modifier_vk_code(std::uint32_t vk) noexcept {
  switch (vk) {
    case 0x10U:  // VK_SHIFT
    case 0x11U:  // VK_CONTROL
    case 0x12U:  // VK_MENU
    case 0x5BU:  // VK_LWIN
    case 0x5CU:  // VK_RWIN
    case 0xA0U:  // VK_LSHIFT
    case 0xA1U:  // VK_RSHIFT
    case 0xA2U:  // VK_LCONTROL
    case 0xA3U:  // VK_RCONTROL
    case 0xA4U:  // VK_LMENU
    case 0xA5U:  // VK_RMENU
      return true;
    default:
      return false;
  }
}

inline std::string vk_code_to_key_name(std::uint32_t vk) {
  switch (vk) {
    case 0x08U: return "backspace";
    case 0x09U: return "tab";
    case 0x0DU: return "enter";
    case 0x14U: return "capslock";
    case 0x1BU: return "esc";
    case 0x20U: return "space";
    case 0x21U: return "pageup";
    case 0x22U: return "pagedown";
    case 0x23U: return "end";
    case 0x24U: return "home";
    case 0x25U: return "left";
    case 0x26U: return "up";
    case 0x27U: return "right";
    case 0x28U: return "down";
    case 0x2CU: return "printscreen";
    case 0x2DU: return "insert";
    case 0x2EU: return "delete";
    default:
      break;
  }
  if (vk >= 0x70U && vk <= 0x87U) {
    return "f" + std::to_string((vk - 0x70U) + 1U);
  }
  if (vk >= 0x30U && vk <= 0x39U) {
    return std::string(1U, static_cast<char>(vk));
  }
  if (vk >= 0x41U && vk <= 0x5AU) {
    return std::string(1U, static_cast<char>(vk + 32U));
  }
  return "";
}

// Pure C++17 24-bit uncompressed BMP encoder (top-down BGRA input -> bottom-up 24-bit BMP).
inline std::vector<std::uint8_t> encode_bgr_bmp(
    int width, int height, const std::uint8_t* bgra_top_down) {
  if (width <= 0 || height <= 0 || bgra_top_down == nullptr) {
    return {};
  }
  const std::size_t w = static_cast<std::size_t>(width);
  const std::size_t h = static_cast<std::size_t>(height);
  const std::size_t row_bytes = w * 3U;
  const std::size_t row_stride = (row_bytes + 3U) & ~std::size_t{3U};
  const std::size_t pixel_bytes = row_stride * h;
  const std::size_t header_bytes = 54U;
  const std::size_t total_bytes = header_bytes + pixel_bytes;

  std::vector<std::uint8_t> out(total_bytes, 0U);

  auto write_u16 = [&](std::size_t off, std::uint16_t v) {
    out[off] = static_cast<std::uint8_t>(v & 0xFFU);
    out[off + 1U] = static_cast<std::uint8_t>((v >> 8) & 0xFFU);
  };
  auto write_u32 = [&](std::size_t off, std::uint32_t v) {
    out[off] = static_cast<std::uint8_t>(v & 0xFFU);
    out[off + 1U] = static_cast<std::uint8_t>((v >> 8) & 0xFFU);
    out[off + 2U] = static_cast<std::uint8_t>((v >> 16) & 0xFFU);
    out[off + 3U] = static_cast<std::uint8_t>((v >> 24) & 0xFFU);
  };

  // BITMAPFILEHEADER (14 bytes)
  out[0] = static_cast<std::uint8_t>('B');
  out[1] = static_cast<std::uint8_t>('M');
  write_u32(2U, static_cast<std::uint32_t>(total_bytes));
  write_u32(10U, static_cast<std::uint32_t>(header_bytes));

  // BITMAPINFOHEADER (40 bytes)
  write_u32(14U, 40U);
  write_u32(18U, static_cast<std::uint32_t>(width));
  write_u32(22U, static_cast<std::uint32_t>(height));
  write_u16(26U, 1U);
  write_u16(28U, 24U);
  write_u32(34U, static_cast<std::uint32_t>(pixel_bytes));

  for (std::size_t r = 0; r < h; ++r) {
    const std::size_t src_y = (h - 1U) - r;
    const std::size_t dst_row = header_bytes + r * row_stride;
    const std::size_t src_row = src_y * w * 4U;
    for (std::size_t x = 0; x < w; ++x) {
      const std::size_t s = src_row + x * 4U;
      const std::size_t d = dst_row + x * 3U;
      out[d + 0U] = bgra_top_down[s + 0U];
      out[d + 1U] = bgra_top_down[s + 1U];
      out[d + 2U] = bgra_top_down[s + 2U];
    }
  }
  return out;
}

inline void check_abort_or_interrupt() {
  if (g_abort_requested.load(std::memory_order_acquire)) {
    cpp11::stop("winrpa emergency abort requested.");
  }
  cpp11::check_user_interrupt();
}

inline void require_windows_or_stop(const char* feature) {
  cpp11::stop("%s is only supported on Windows platforms.", feature);
}

}  // namespace winrpa

#endif  // WINRPA_CORE_H
