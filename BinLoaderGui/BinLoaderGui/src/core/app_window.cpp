#include "app_window.hpp"
#include "../ui/loader_view.hpp"

namespace core::app_window {
    static std::function<void(HWND)> g_ready_callback;

    static auto CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) -> LRESULT {
        switch (msg) {
            case WM_SIZE: {
                if (wparam != SIZE_MINIMIZED) {
                    ui::loader_view::resize(hwnd);
                }
                return 0;
            }
            case ui::loader_view::kReportMessage:
                ui::loader_view::handle_ui_message(hwnd, wparam, lparam);
                return 0;
            case WM_DESTROY:
                ui::loader_view::shutdown();
                PostQuitMessage(0);
                return 0;
        }
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }

    auto on_ready(std::function<void(HWND)> callback) -> void {
        g_ready_callback = std::move(callback);
    }

    auto create(const config& cfg) -> HWND {
        HINSTANCE instance = GetModuleHandleW(nullptr);
        const wchar_t* class_name = L"BinLoaderGuiWindow";

        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = wnd_proc;
        wc.hInstance = instance;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = CreateSolidBrush(RGB(8, 8, 8));
        wc.lpszClassName = class_name;
        RegisterClassExW(&wc);

        int screen_w = GetSystemMetrics(SM_CXSCREEN);
        int screen_h = GetSystemMetrics(SM_CYSCREEN);
        int x = (screen_w - cfg.width) / 2;
        int y = (screen_h - cfg.height) / 2;
        const wchar_t* title = cfg.title ? cfg.title : L"PS4 Bin Loader";

        HWND hwnd = CreateWindowExW(
            WS_EX_APPWINDOW,
            class_name,
            title,
            WS_POPUP | WS_VISIBLE,
            x, y, cfg.width, cfg.height,
            nullptr, nullptr, instance, nullptr);

        if (hwnd && g_ready_callback) {
            g_ready_callback(hwnd);
        }

        return hwnd;
    }

    auto run(HWND hwnd) -> int {
        MSG msg{};
        while (GetMessageW(&msg, nullptr, 0, 0)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        return static_cast<int>(msg.wParam);
    }
}
