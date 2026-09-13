#include <winsock2.h>
#include <ws2tcpip.h>
#include <Windows.h>
#include "ps4_client.hpp"
#include <algorithm>
#include <vector>

#pragma comment(lib, "ws2_32.lib")

namespace net::ps4_client {
    static auto file_name(const std::wstring& path) -> std::wstring {
        size_t pos = path.find_last_of(L"\\/");
        return pos == std::wstring::npos ? path : path.substr(pos + 1);
    }

    static auto read_file(const std::wstring& path, std::vector<char>& data) -> bool {
        HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                  OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) return false;

        LARGE_INTEGER size{};
        if (!GetFileSizeEx(file, &size)) {
            CloseHandle(file);
            return false;
        }

        data.resize(static_cast<size_t>(size.QuadPart));
        bool ok = true;
        if (!data.empty()) {
            DWORD read = 0;
            ok = ReadFile(file, data.data(), static_cast<DWORD>(data.size()), &read, nullptr) && read == data.size();
        }

        CloseHandle(file);
        return ok;
    }

    static auto send_all(SOCKET sock, const std::vector<char>& data) -> bool {
        size_t offset = 0;
        while (offset < data.size()) {
            size_t remaining = data.size() - offset;
            int chunk = static_cast<int>(std::min<size_t>(remaining, 1u << 20));
            int sent = send(sock, data.data() + offset, chunk, 0);
            if (sent <= 0) return false;
            offset += static_cast<size_t>(sent);
        }
        return true;
    }

    static auto resolve(const std::wstring& host, unsigned short port, sockaddr_in& addr) -> bool {
        addr = {};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        if (InetPtonW(AF_INET, host.c_str(), &addr.sin_addr) == 1) return true;

        ADDRINFOW hints{};
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        ADDRINFOW* result = nullptr;
        if (GetAddrInfoW(host.c_str(), nullptr, &hints, &result) != 0 || !result) return false;

        addr.sin_addr = reinterpret_cast<sockaddr_in*>(result->ai_addr)->sin_addr;
        FreeAddrInfoW(result);
        return true;
    }

    static auto send_one(const std::wstring& host, unsigned short port, const std::wstring& path, std::wstring& detail) -> bool {
        std::vector<char> data;
        if (!read_file(path, data)) {
            detail = L"Failed to read file";
            return false;
        }
        if (data.empty()) {
            detail = L"File is empty";
            return false;
        }

        SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (sock == INVALID_SOCKET) {
            detail = L"Socket creation failed";
            return false;
        }

        DWORD timeout = 8000;
        setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));

        sockaddr_in addr{};
        if (!resolve(host, port, addr)) {
            detail = L"Invalid address";
            closesocket(sock);
            return false;
        }

        bool ok = false;
        if (connect(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
            detail = L"Connection failed";
        } else if (!send_all(sock, data)) {
            detail = L"Send failed";
        } else {
            ok = true;
            detail = std::to_wstring(data.size()) + L" bytes sent";
        }

        shutdown(sock, SD_BOTH);
        closesocket(sock);
        return ok;
    }

    auto send_payloads(const std::wstring& host, unsigned short port,
                       const std::vector<std::wstring>& paths,
                       const std::function<void(const step&)>& on_step) -> bool {
        WSADATA wsa{};
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
            on_step({L"Network", false, L"WSAStartup failed"});
            return false;
        }

        bool all_ok = true;
        for (const auto& path : paths) {
            std::wstring detail;
            bool ok = send_one(host, port, path, detail);
            if (!ok) all_ok = false;
            on_step({file_name(path), ok, detail});
        }

        WSACleanup();
        return all_ok;
    }
}
