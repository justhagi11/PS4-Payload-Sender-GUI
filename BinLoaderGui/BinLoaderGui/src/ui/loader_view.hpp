#pragma once
#include <Windows.h>

namespace ui::loader_view {
    constexpr UINT kReportMessage = WM_APP + 0x201;

    auto initialize(HWND hwnd) -> bool;
    auto shutdown() -> void;
    auto resize(HWND hwnd) -> void;
    auto handle_ui_message(HWND hwnd, WPARAM wparam, LPARAM lparam) -> void;
}
