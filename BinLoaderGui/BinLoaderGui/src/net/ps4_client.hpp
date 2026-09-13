#pragma once
#include <string>
#include <vector>
#include <functional>

namespace net::ps4_client {
    struct step {
        std::wstring label;
        bool ok;
        std::wstring detail;
    };

    auto send_payloads(const std::wstring& host, unsigned short port,
                       const std::vector<std::wstring>& paths,
                       const std::function<void(const step&)>& on_step) -> bool;
}
