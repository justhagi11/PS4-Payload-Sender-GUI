#include "core/app_window.hpp"
#include "ui/loader_view.hpp"

auto APIENTRY wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int) -> int {
    core::app_window::on_ready([](HWND hwnd) {
        ui::loader_view::initialize(hwnd);
    });

    core::app_window::config cfg{};
    HWND hwnd = core::app_window::create(cfg);
    if (!hwnd) return 1;

    return core::app_window::run(hwnd);
}
