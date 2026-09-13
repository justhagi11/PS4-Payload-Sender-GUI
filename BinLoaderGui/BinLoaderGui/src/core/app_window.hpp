#pragma once
#include <Windows.h>
#include <functional>

namespace core::app_window {
    struct config {
        int width = 600;
        int height = 540;
        const wchar_t* title = L"PS4 Bin Loader";
    };

    auto create(const config& cfg) -> HWND;
    auto run(HWND hwnd) -> int;

    auto on_ready(std::function<void(HWND)> callback) -> void;
}
