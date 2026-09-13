#include "loader_view.hpp"
#include "../net/ps4_client.hpp"
#include <WebView2.h>
#include <wrl.h>
#include <commdlg.h>
#include <algorithm>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using namespace Microsoft::WRL;

namespace ui::loader_view {
    static ComPtr<ICoreWebView2Controller> g_controller;
    static ComPtr<ICoreWebView2> g_webview;
    static bool g_ready = false;
    static std::vector<std::wstring> g_payloads;

    static auto json_escape(const std::wstring& s) -> std::wstring {
        std::wstring out;
        out.reserve(s.size() + 8);
        for (wchar_t c : s) {
            if (c == L'"') out += L"\\\"";
            else if (c == L'\\') out += L"\\\\";
            else if (c == L'\n') out += L"\\n";
            else if (c == L'\r') out += L"\\r";
            else if (c == L'\t') out += L"\\t";
            else out += c;
        }
        return out;
    }

    static auto file_name(const std::wstring& path) -> std::wstring {
        size_t pos = path.find_last_of(L"\\/");
        return pos == std::wstring::npos ? path : path.substr(pos + 1);
    }

    static auto post_report_json(HWND hwnd, const std::wstring& json) -> void {
        if (!hwnd || !IsWindow(hwnd)) return;
        auto* payload = new std::wstring(json);
        if (!PostMessageW(hwnd, kReportMessage, 0, reinterpret_cast<LPARAM>(payload))) {
            delete payload;
        }
    }

    static auto send_report(HWND hwnd, const std::wstring& label, bool ok, const std::wstring& detail = {}) -> void {
        std::wstring json = L"{\"kind\":\"report\",\"label\":\"" + json_escape(label) +
                            L"\",\"ok\":" + (ok ? L"true" : L"false") +
                            L",\"detail\":\"" + json_escape(detail) + L"\"}";
        post_report_json(hwnd, json);
    }

    static auto send_overall(HWND hwnd, bool ok) -> void {
        post_report_json(hwnd, ok ? L"{\"kind\":\"overall\",\"ok\":true}" : L"{\"kind\":\"overall\",\"ok\":false}");
    }

    static auto send_files(HWND hwnd) -> void {
        std::wstring json = L"{\"kind\":\"files\",\"items\":[";
        for (size_t i = 0; i < g_payloads.size(); ++i) {
            if (i) json += L",";
            const std::wstring& path = g_payloads[i];
            json += L"{\"name\":\"" + json_escape(file_name(path)) +
                    L"\",\"path\":\"" + json_escape(path) + L"\"}";
        }
        json += L"]}";
        post_report_json(hwnd, json);
    }

    static auto pick_files(HWND hwnd) -> std::vector<std::wstring> {
        std::vector<std::wstring> result;

        static const wchar_t filter[] =
            L"Payloads (*.bin;*.elf)\0*.bin;*.elf\0All Files (*.*)\0*.*\0";

        std::vector<wchar_t> buffer(65536, L'\0');

        OPENFILENAMEW ofn{};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = hwnd;
        ofn.lpstrFilter = filter;
        ofn.lpstrFile = buffer.data();
        ofn.nMaxFile = static_cast<DWORD>(buffer.size());
        ofn.Flags = OFN_ALLOWMULTISELECT | OFN_EXPLORER | OFN_FILEMUSTEXIST |
                    OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
        if (!GetOpenFileNameW(&ofn)) return result;

        std::wstring first = buffer.data();
        const wchar_t* p = buffer.data() + first.size() + 1;
        if (*p == L'\0') {
            result.push_back(first);
            return result;
        }

        while (*p) {
            std::wstring name = p;
            result.push_back(first + L"\\" + name);
            p += name.size() + 1;
        }
        return result;
    }

    static auto start_send(HWND hwnd, const std::wstring& host, unsigned short port) -> void {
        std::vector<std::wstring> paths = g_payloads;
        std::thread([hwnd, host, port, paths]() {
            post_report_json(hwnd, L"{\"kind\":\"reset\"}");

            bool ok = net::ps4_client::send_payloads(host, port, paths,
                [hwnd](const net::ps4_client::step& step) {
                    send_report(hwnd, step.label, step.ok, step.detail);
                });

            send_overall(hwnd, ok);
        }).detach();
    }

    static auto handle_message(HWND hwnd, const std::wstring& message) -> void {
        if (message == L"drag") {
            ReleaseCapture();
            SendMessageW(hwnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
            return;
        }
        if (message == L"minimize") {
            ShowWindow(hwnd, SW_MINIMIZE);
            return;
        }
        if (message == L"exit") {
            PostMessageW(hwnd, WM_CLOSE, 0, 0);
            return;
        }
        if (message == L"add_files") {
            for (const auto& path : pick_files(hwnd)) {
                if (std::find(g_payloads.begin(), g_payloads.end(), path) == g_payloads.end()) {
                    g_payloads.push_back(path);
                }
            }
            send_files(hwnd);
            return;
        }
        if (message == L"clear") {
            g_payloads.clear();
            send_files(hwnd);
            return;
        }
        if (message.rfind(L"remove|", 0) == 0) {
            int index = _wtoi(message.c_str() + 7);
            if (index >= 0 && index < static_cast<int>(g_payloads.size())) {
                g_payloads.erase(g_payloads.begin() + index);
            }
            send_files(hwnd);
            return;
        }
        if (message.rfind(L"send|", 0) == 0) {
            std::wstring rest = message.substr(5);
            size_t sep = rest.find(L'|');
            if (sep == std::wstring::npos) return;

            std::wstring host = rest.substr(0, sep);
            std::wstring port_text = rest.substr(sep + 1);
            if (host.empty() || g_payloads.empty()) {
                send_report(hwnd, L"Validation", false,
                            host.empty() ? L"IP address is required" : L"No payloads selected");
                send_overall(hwnd, false);
                return;
            }

            unsigned short port = static_cast<unsigned short>(_wtoi(port_text.c_str()));
            if (port == 0) port = 9020;
            start_send(hwnd, host, port);
            return;
        }
    }

    auto handle_ui_message(HWND, WPARAM, LPARAM lparam) -> void {
        std::unique_ptr<std::wstring> json(reinterpret_cast<std::wstring*>(lparam));
        if (!json || !g_webview) return;
        g_webview->PostWebMessageAsString(json->c_str());
    }

    auto initialize(HWND hwnd) -> bool {
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

        wchar_t temp_path[MAX_PATH];
        GetTempPathW(MAX_PATH, temp_path);
        std::wstring user_data_folder = std::wstring(temp_path) + L"bin_loader_gui_webview";

        CreateCoreWebView2EnvironmentWithOptions(nullptr, user_data_folder.c_str(), nullptr,
            Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
                [hwnd](HRESULT result, ICoreWebView2Environment* env) -> HRESULT {
                    if (FAILED(result)) return result;

                    env->CreateCoreWebView2Controller(hwnd,
                        Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                            [hwnd](HRESULT result, ICoreWebView2Controller* controller) -> HRESULT {
                                if (FAILED(result)) return result;

                                g_controller = controller;
                                g_controller->get_CoreWebView2(&g_webview);

                                ComPtr<ICoreWebView2Settings> settings;
                                g_webview->get_Settings(&settings);
                                settings->put_IsScriptEnabled(TRUE);
                                settings->put_AreDefaultScriptDialogsEnabled(FALSE);
                                settings->put_IsWebMessageEnabled(TRUE);
                                settings->put_AreDevToolsEnabled(FALSE);
                                settings->put_AreDefaultContextMenusEnabled(FALSE);
                                settings->put_IsStatusBarEnabled(FALSE);
                                settings->put_IsZoomControlEnabled(FALSE);

                                ComPtr<ICoreWebView2Controller2> controller2;
                                if (SUCCEEDED(controller->QueryInterface(IID_PPV_ARGS(&controller2)))) {
                                    COREWEBVIEW2_COLOR bg{ 255, 8, 8, 8 };
                                    controller2->put_DefaultBackgroundColor(bg);
                                }

                                RECT bounds;
                                GetClientRect(hwnd, &bounds);
                                g_controller->put_Bounds(bounds);

                                EventRegistrationToken token;
                                g_webview->add_WebMessageReceived(
                                    Callback<ICoreWebView2WebMessageReceivedEventHandler>(
                                        [hwnd](ICoreWebView2* sender, ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT {
                                            LPWSTR message = nullptr;
                                            if (SUCCEEDED(args->TryGetWebMessageAsString(&message)) && message) {
                                                handle_message(hwnd, message);
                                                CoTaskMemFree(message);
                                            }
                                            return S_OK;
                                        }).Get(),
                                    &token);

                                std::wstring html = LR"html(
                                    <!DOCTYPE html>
                                    <html>
                                    <head>
                                      <meta charset="UTF-8">
                                      <style>
                                        @import url('https://fonts.googleapis.com/css2?family=Sora:wght@300;400;500;600;700;800&display=swap');
                                        :root { --orb1: rgba(255,255,255,0.12); --orb2: rgba(255,255,255,0.08); }
                                        * { cursor: default !important; user-select: none !important; -webkit-user-drag: none !important; box-sizing: border-box; }
                                        html, body { background: transparent !important; margin: 0; padding: 0; overflow: hidden; height: 100vh; width: 100vw; font-family: 'Sora', sans-serif; color: #fff; -webkit-font-smoothing: antialiased; }
                                        .orb { position: fixed; border-radius: 50%; pointer-events: none; z-index: 1; will-change: transform; }
                                        .orb1 { width: 340px; height: 340px; background: var(--orb1); filter: blur(90px); top: -20%; left: -8%; animation: drift 22s ease-in-out infinite alternate; }
                                        .orb2 { width: 380px; height: 380px; background: var(--orb2); filter: blur(110px); bottom: -30%; right: -6%; animation: drift-slow 28s ease-in-out infinite alternate; }
                                        @keyframes drift { 0% { transform: translate(0,0) scale(1); } 50% { transform: translate(30px,22px) scale(1.15); } 100% { transform: translate(-16px,14px) scale(0.92); } }
                                        @keyframes drift-slow { 0% { transform: translate(0,0) scale(1); } 50% { transform: translate(-22px,-28px) scale(1.12); } 100% { transform: translate(14px,-14px) scale(1); } }
                                        @keyframes spin { to { transform: rotate(360deg); } }
                                        @keyframes fadeIn { from { opacity: 0; transform: translateY(4px); } to { opacity: 1; transform: none; } }

                                        .frame { position: relative; height: 100vh; width: 100vw; background: #050505; border: 1px solid #555555; overflow: hidden; opacity: 0; transform: scale(0.97); transition: opacity 0.4s ease-out, transform 0.4s cubic-bezier(0.2, 0.8, 0.2, 1); }
                                        .frame.active { opacity: 1; transform: scale(1); }

                                        .page { position: absolute; inset: 0; z-index: 10; display: none; flex-direction: column; align-items: center; justify-content: center; }
                                        .page.active { display: flex; }

                                        .reveal { opacity: 0; transform: translateY(10px); transition: opacity 0.5s ease-out, transform 0.5s cubic-bezier(0.2, 0.8, 0.2, 1); transition-delay: 0s; }
                                        .frame.active .page.active .reveal { opacity: 1; transform: translateY(0); }
                                        .reveal.d1 { transition-delay: 0.05s; }
                                        .reveal.d2 { transition-delay: 0.12s; }
                                        .reveal.d3 { transition-delay: 0.18s; }

                                        .wordmark { font-size: 26px; font-weight: 700; letter-spacing: -0.01em; }

                                        .endpoint { margin-top: 22px; display: flex; align-items: center; gap: 6px; }
                                        .field { width: 158px; padding: 9px 12px; background: rgba(255,255,255,0.03); border: 1px solid #555555; color: #fff; font-family: 'Sora', sans-serif; font-size: 13px; font-weight: 500; text-align: center; outline: none; transition: border-color 0.25s ease; cursor: text !important; user-select: text !important; -webkit-user-select: text !important; }
                                        .field:focus { border-color: #d3d3d3; }
                                        .field::placeholder { color: rgba(255,255,255,0.25); }
                                        .field.port { width: 76px; }
                                        .colon { font-size: 15px; font-weight: 600; color: rgba(255,255,255,0.35); }

                                        .ghost-btn { margin-top: 14px; width: 240px; padding: 10px; background: transparent; color: #fff; font-family: 'Sora', sans-serif; font-size: 12px; font-weight: 600; letter-spacing: 0.01em; border: 1px solid #555555; cursor: default !important; transition: border-color 0.25s ease, transform 0.15s ease, opacity 0.15s ease; }
                                        .ghost-btn:hover { border-color: #d3d3d3; opacity: .92; }
                                        .ghost-btn:active { transform: scale(0.98); }

                                        .file-list { width: 372px; max-height: 146px; overflow-y: auto; margin-top: 12px; border: 1px solid rgba(255,255,255,0.08); background: rgba(255,255,255,0.02); }
                                        .file-empty { padding: 14px; text-align: center; font-size: 11px; font-weight: 500; color: rgba(255,255,255,0.25); }
                                        .file-row { display: flex; align-items: center; gap: 8px; padding: 8px 10px; border-bottom: 1px solid rgba(255,255,255,0.08); animation: fadeIn 0.3s ease-out; }
                                        .file-row:last-child { border-bottom: none; }
                                        .file-name { flex: 1 1 auto; font-size: 12px; font-weight: 500; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }
                                        .file-badge { flex: 0 0 auto; font-size: 9px; font-weight: 700; letter-spacing: 0.06em; color: rgba(255,255,255,0.5); }
                                        .file-remove { flex: 0 0 auto; background: transparent; border: none; color: rgba(255,255,255,0.4); font-family: 'Sora', sans-serif; font-size: 10px; font-weight: 700; padding: 2px 4px; cursor: default !important; transition: color 0.2s ease; }
                                        .file-remove:hover { color: #f87171; }

                                        .load-btn { margin-top: 16px; width: 240px; padding: 12px; border-radius: 0px; background: transparent; color: #fff; font-family: 'Sora', sans-serif; font-size: 13px; font-weight: 600; letter-spacing: 0.01em; border: 1px solid #555555; cursor: default !important; transition: border-color 0.25s ease, transform 0.15s ease, opacity 0.15s ease; position: relative; overflow: hidden; }
                                        .load-btn:hover { border-color: #d3d3d3; opacity: .92; }
                                        .load-btn:active { transform: scale(0.98); }
                                        .load-btn.loading { pointer-events: none; opacity: 0.85; }
                                        .load-btn.loading .btn-text { display: none; }
                                        .spinner { width: 16px; height: 16px; border: 2.5px solid rgba(255,255,255,0.15); border-top-color: #fff; border-radius: 50%; animation: spin 0.7s linear infinite; display: none; margin: 0 auto; }
                                        .load-btn.loading .spinner { display: block; }

                                        .send-status { width: 372px; max-height: 120px; overflow-y: auto; margin-top: 12px; text-align: center; }
                                        .status-row { font-size: 12px; font-weight: 600; padding: 4px 0; animation: fadeIn 0.3s ease-out; }
                                        .status-row.ok { color: #4ade80; }
                                        .status-row.fail { color: #f87171; }

                                        .ip-hint { margin-top: 8px; font-size: 11px; font-weight: 500; color: #f87171; height: 13px; opacity: 0; transition: opacity 0.2s ease; }
                                        .ip-hint.visible { opacity: 1; }

                                        .window-controls { position: fixed; top: 0; right: 0; z-index: 30; display: flex; padding: 7px 9px; gap: 2px; }
                                        .win-btn { width: 26px; height: 20px; background: transparent; border: none; color: rgba(255,255,255,0.4); font-family: 'Sora', sans-serif; font-size: 12px; font-weight: 600; line-height: 1; cursor: default !important; transition: color 0.2s ease, background 0.2s ease; }
                                        .win-btn:hover { color: #fff; background: rgba(255,255,255,0.1); }
                                        .win-btn.close:hover { color: #fff; background: #c0392b; }
                                      </style>
                                    </head>
                                    <body>
                                      <div id="app" class="frame">
                                        <div class="orb orb1"></div>
                                        <div class="orb orb2"></div>

                                        <div class="window-controls">
                                          <button class="win-btn" onmousedown="event.stopPropagation()" onclick="minimizeWindow()">&#8722;</button>
                                          <button class="win-btn close" onmousedown="event.stopPropagation()" onclick="exitWindow()">&#10005;</button>
                                        </div>

                                        <div id="page-main" class="page active" onmousedown="drag(event)">
                                          <div class="wordmark reveal d1">PS4 Bin Loader</div>

                                          <div class="endpoint reveal d2" onmousedown="event.stopPropagation()">
                                            <input id="ip" class="field" type="text" placeholder="192.168.1.100" spellcheck="false" autocomplete="off" oninput="clearIpHint()">
                                            <span class="colon">:</span>
                                            <input id="port" class="field port" type="text" value="9020" spellcheck="false" autocomplete="off">
                                          </div>

                                          <div id="ipHint" class="ip-hint"></div>

                                          <button id="addBtn" class="ghost-btn reveal d3" onmousedown="event.stopPropagation()" onclick="addFiles()">Add Payloads</button>

                                          <div id="fileList" class="file-list reveal d3" onmousedown="event.stopPropagation()"></div>

                                          <button id="sendBtn" class="load-btn reveal d3" onmousedown="event.stopPropagation()" onclick="doSend()">
                                            <span class="btn-text">Send All</span>
                                            <div class="spinner"></div>
                                          </button>

                                          <div id="sendStatus" class="send-status"></div>
                                        </div>
                                      </div>
                                      <script>
                                        function drag(e) {
                                          if (e.target.closest('button') || e.target.closest('input') || e.target.closest('.file-list')) return;
                                          window.chrome.webview.postMessage('drag');
                                        }

                                        function minimizeWindow() {
                                          window.chrome.webview.postMessage('minimize');
                                        }

                                        function exitWindow() {
                                          window.chrome.webview.postMessage('exit');
                                        }

                                        function showIpHint(text) {
                                          const h = document.getElementById('ipHint');
                                          h.textContent = text;
                                          h.classList.add('visible');
                                        }

                                        function clearIpHint() {
                                          document.getElementById('ipHint').classList.remove('visible');
                                        }

                                        function addFiles() {
                                          window.chrome.webview.postMessage('add_files');
                                        }

                                        function removeFile(i) {
                                          window.chrome.webview.postMessage('remove|' + i);
                                        }

                                        function renderFiles(items) {
                                          const list = document.getElementById('fileList');
                                          list.innerHTML = '';
                                          if (!items.length) {
                                            const empty = document.createElement('div');
                                            empty.className = 'file-empty';
                                            empty.textContent = 'No payloads selected';
                                            list.appendChild(empty);
                                            return;
                                          }
                                          items.forEach((item, i) => {
                                            const row = document.createElement('div');
                                            row.className = 'file-row';
                                            row.onmousedown = (e) => e.stopPropagation();

                                            const name = document.createElement('span');
                                            name.className = 'file-name';
                                            name.textContent = item.name;
                                            name.title = item.path;

                                            const badge = document.createElement('span');
                                            badge.className = 'file-badge';
                                            badge.textContent = item.name.toLowerCase().endsWith('.elf') ? 'ELF' : 'BIN';

                                            const remove = document.createElement('button');
                                            remove.className = 'file-remove';
                                            remove.textContent = '\u2715';
                                            remove.onclick = (e) => { e.stopPropagation(); removeFile(i); };

                                            row.appendChild(name);
                                            row.appendChild(badge);
                                            row.appendChild(remove);
                                            list.appendChild(row);
                                          });
                                        }

                                        function doSend() {
                                          const btn = document.getElementById('sendBtn');
                                          if (btn.classList.contains('loading')) return;
                                          const count = document.querySelectorAll('#fileList .file-row').length;
                                          if (!count) return;
                                          const ip = document.getElementById('ip').value.trim();
                                          const port = document.getElementById('port').value.trim() || '9020';
                                          if (!ip) { showIpHint('Enter the console IP address'); return; }
                                          clearIpHint();
                                          clearStatus();
                                          btn.classList.add('loading');
                                          window.chrome.webview.postMessage('send|' + ip + '|' + port);
                                        }

                                        function clearStatus() {
                                          document.getElementById('sendStatus').innerHTML = '';
                                        }

                                        function setOverall(ok) {
                                          document.getElementById('sendBtn').classList.remove('loading');
                                        }

                                        function addReport(label, ok, detail) {
                                          const list = document.getElementById('sendStatus');
                                          const row = document.createElement('div');
                                          row.className = 'status-row ' + (ok ? 'ok' : 'fail');
                                          row.textContent = ok ? 'payload delivered' : 'payload failed';
                                          list.appendChild(row);
                                        }

                                        window.chrome.webview.addEventListener('message', e => {
                                          try {
                                            const d = JSON.parse(e.data);
                                            if (d.kind === 'files') {
                                              renderFiles(d.items || []);
                                            } else if (d.kind === 'report') {
                                              addReport(d.label, d.ok, d.detail);
                                            } else if (d.kind === 'overall') {
                                              setOverall(d.ok);
                                            } else if (d.kind === 'reset') {
                                              clearStatus();
                                            }
                                          } catch (err) {}
                                        });

                                        window.addEventListener('DOMContentLoaded', () => {
                                          requestAnimationFrame(() => document.getElementById('app').classList.add('active'));
                                          renderFiles([]);
                                        });
                                      </script>
                                    </body>
                                    </html>
                                )html";

                                g_webview->NavigateToString(html.c_str());
                                g_ready = true;
                                return S_OK;
                            }).Get());
                    return S_OK;
                }).Get());

        return true;
    }

    auto shutdown() -> void {
        if (g_controller) {
            g_controller->Close();
            g_controller = nullptr;
            g_webview = nullptr;
        }
        CoUninitialize();
    }

    auto resize(HWND hwnd) -> void {
        if (!g_controller) return;
        RECT bounds;
        GetClientRect(hwnd, &bounds);
        g_controller->put_Bounds(bounds);
    }
}
