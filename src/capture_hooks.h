#ifndef WINRPA_CAPTURE_HOOKS_H
#define WINRPA_CAPTURE_HOOKS_H

#include "winrpa_core.h"
#include <cstdint>

namespace winrpa {

void stop_runtime_hooks();

bool start_record_hooks(
    bool include_wheel,
    bool install_stop_hotkey,
    std::uint32_t stop_mods,
    std::uint32_t stop_vk,
    std::uint32_t& err_out);

bool stop_record_hooks();

bool record_stop_hotkey_triggered() noexcept;

void reset_record_stop_hotkey() noexcept;

RecordEventRingBuffer& record_ring() noexcept;

bool wait_record_signal(std::uint32_t timeout_ms);

void signal_record_event() noexcept;

}  // namespace winrpa

#endif  // WINRPA_CAPTURE_HOOKS_H
