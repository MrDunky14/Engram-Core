#pragma once
// ============================================================
// WinSock HTTP Client + HTML Parser (Lightweight, Zero Dependencies)
// For FP-SAN Research Agent: Autonomous web fetching without libcurl
// ============================================================

// CRITICAL: Define WIN32_LEAN_AND_MEAN BEFORE including windows.h to avoid old winsock.h
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <winhttp.h>
#include <string>
#include <cctype>
#include <cstring>
#include <vector>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "winhttp.lib")

// ============================================================
// HTML TEXT EXTRACTOR (Strips tags, returns clean text)
// ============================================================
class HTMLParser {
public:
    // Strip HTML tags and script/style content from raw HTML
    static std::string extract_text(const std::string& html) {
        std::string result;
        bool in_tag = false;
        bool in_script = false;
        bool in_style = false;
        
        for (size_t i = 0; i < html.length(); ++i) {
            char c = html[i];
            
            // Detect script and style tags
            if (c == '<') {
                if (html.substr(i).find("<script") == 0 || 
                    html.substr(i).find("<SCRIPT") == 0) {
                    in_script = true;
                    in_tag = true;
                } else if (html.substr(i).find("<style") == 0 || 
                           html.substr(i).find("<STYLE") == 0) {
                    in_style = true;
                    in_tag = true;
                } else {
                    in_tag = true;
                }
            }
            
            if (c == '>') {
                in_tag = false;
                // Skip the space after tag close
                if (result.length() > 0 && result.back() != ' ' && result.back() != '\n') {
                    result += ' ';
                }
                continue;
            }
            
            // Check for closing tags
            if (in_script && html.substr(i).find("</script") == 0) {
                in_script = false;
                // Fast-forward to end of tag
                size_t end = html.find('>', i);
                if (end != std::string::npos) i = end;
                continue;
            }
            if (in_style && html.substr(i).find("</style") == 0) {
                in_style = false;
                size_t end = html.find('>', i);
                if (end != std::string::npos) i = end;
                continue;
            }
            
            // Skip content inside tags or scripts
            if (in_tag || in_script || in_style) continue;
            
            // Add regular text
            if (c == '\n' || c == '\r') {
                if (result.length() > 0 && result.back() != '\n') {
                    result += '\n';
                }
            } else if (c == '\t') {
                result += ' ';
            } else {
                result += c;
            }
        }
        
        // Clean up multiple spaces and newlines
        std::string cleaned;
        bool last_was_space = false;
        for (char ch : result) {
            if (std::isspace((unsigned char)ch)) {
                if (!last_was_space) {
                    cleaned += ' ';
                    last_was_space = true;
                }
            } else {
                cleaned += ch;
                last_was_space = false;
            }
        }
        
        return cleaned;
    }
};

// ============================================================
// WinSock HTTP CLIENT (Raw socket, no libcurl)
// ============================================================
class HTTPClient {
public:
    static bool initialized;

    static void init_winsock() {
        if (initialized) return;
        WSADATA wsa_data;
        if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
            printf("[HTTPClient] WSAStartup failed. Network unavailable.\n");
            initialized = false;
            return;
        }
        initialized = true;
    }

    static void cleanup_winsock() {
        if (initialized) {
            WSACleanup();
            initialized = false;
        }
    }

    // Fetch a URL via HTTP GET and return response body
    // url: "en.wikipedia.org/w/api.php?action=query&titles=AGI&prop=extracts&format=json"
    // Returns the response body or empty string on failure
    static std::string fetch(const std::string& host, const std::string& path, int timeout_sec = 5) {
        init_winsock();
        if (!initialized) return std::string();

        SOCKET sock = INVALID_SOCKET;
        try {
            // Resolve hostname
            struct addrinfo hints = {};
            hints.ai_family = AF_INET;
            hints.ai_socktype = SOCK_STREAM;
            hints.ai_protocol = IPPROTO_TCP;

            struct addrinfo* result = nullptr;
            if (getaddrinfo(host.c_str(), "80", &hints, &result) != 0) {
                printf("[HTTPClient] getaddrinfo failed for %s\n", host.c_str());
                return std::string();
            }

            // Create socket
            sock = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
            if (sock == INVALID_SOCKET) {
                printf("[HTTPClient] socket() failed\n");
                freeaddrinfo(result);
                return std::string();
            }

            // Set timeout
            int timeout_ms = timeout_sec * 1000;
            setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout_ms, sizeof(timeout_ms));
            setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (const char*)&timeout_ms, sizeof(timeout_ms));

            // Connect
            if (connect(sock, result->ai_addr, (int)result->ai_addrlen) == SOCKET_ERROR) {
                printf("[HTTPClient] connect() failed\n");
                freeaddrinfo(result);
                closesocket(sock);
                return std::string();
            }

            freeaddrinfo(result);

            // Build HTTP request
            std::string request = "GET " + path + " HTTP/1.1\r\n";
            request += "Host: " + host + "\r\n";
            request += "Connection: close\r\n";
            request += "User-Agent: EngramCore-Research/1.0.0\r\n";
            request += "\r\n";

            // Send request
            if (send(sock, request.c_str(), (int)request.length(), 0) == SOCKET_ERROR) {
                printf("[HTTPClient] send() failed\n");
                closesocket(sock);
                return std::string();
            }

            // Receive response
            std::string response;
            char buffer[4096];
            int bytes_received;

            // Read until connection closes or error
            while ((bytes_received = recv(sock, buffer, sizeof(buffer) - 1, 0)) > 0) {
                buffer[bytes_received] = '\0';
                response += buffer;
            }

            closesocket(sock);

            // Extract body (skip headers)
            size_t header_end = response.find("\r\n\r\n");
            if (header_end != std::string::npos) {
                return response.substr(header_end + 4);
            } else {
                // Fallback for LF-only
                header_end = response.find("\n\n");
                if (header_end != std::string::npos) {
                    return response.substr(header_end + 2);
                }
            }

            return response;
        } catch (...) {
            if (sock != INVALID_SOCKET) closesocket(sock);
            return std::string();
        }
    }

    // Fetch a URL via HTTPS GET using WinHTTP and return response body.
    // This preserves zero external dependencies while supporting TLS.
    static std::string fetch_https(const std::string& host, const std::string& path, int timeout_sec = 8) {
        std::wstring whost(host.begin(), host.end());
        std::wstring wpath(path.begin(), path.end());

        HINTERNET hSession = WinHttpOpen(L"EngramCore-Research/1.0.0",
                                         WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                         WINHTTP_NO_PROXY_NAME,
                                         WINHTTP_NO_PROXY_BYPASS,
                                         0);
        if (!hSession) {
            printf("[HTTPClient] WinHttpOpen failed\n");
            return std::string();
        }

        DWORD timeout_ms = (DWORD)(timeout_sec * 1000);
        WinHttpSetTimeouts(hSession, timeout_ms, timeout_ms, timeout_ms, timeout_ms);

        HINTERNET hConnect = WinHttpConnect(hSession, whost.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
        if (!hConnect) {
            printf("[HTTPClient] WinHttpConnect failed for %s\n", host.c_str());
            WinHttpCloseHandle(hSession);
            return std::string();
        }

        HINTERNET hRequest = WinHttpOpenRequest(hConnect,
                                                L"GET",
                                                wpath.c_str(),
                                                nullptr,
                                                WINHTTP_NO_REFERER,
                                                WINHTTP_DEFAULT_ACCEPT_TYPES,
                                                WINHTTP_FLAG_SECURE);
        if (!hRequest) {
            printf("[HTTPClient] WinHttpOpenRequest failed\n");
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            return std::string();
        }

        BOOL ok = WinHttpSendRequest(hRequest,
                                     WINHTTP_NO_ADDITIONAL_HEADERS,
                                     0,
                                     WINHTTP_NO_REQUEST_DATA,
                                     0,
                                     0,
                                     0);
        if (!ok) {
            printf("[HTTPClient] WinHttpSendRequest failed\n");
            WinHttpCloseHandle(hRequest);
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            return std::string();
        }

        ok = WinHttpReceiveResponse(hRequest, nullptr);
        if (!ok) {
            printf("[HTTPClient] WinHttpReceiveResponse failed\n");
            WinHttpCloseHandle(hRequest);
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            return std::string();
        }

        DWORD status_code = 0;
        DWORD status_size = sizeof(status_code);
        WinHttpQueryHeaders(hRequest,
                            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX,
                            &status_code,
                            &status_size,
                            WINHTTP_NO_HEADER_INDEX);
        if (status_code < 200 || status_code >= 300) {
            printf("[HTTPClient] HTTPS status %lu for %s\n", (unsigned long)status_code, host.c_str());
        }

        std::string body;
        for (;;) {
            DWORD available = 0;
            if (!WinHttpQueryDataAvailable(hRequest, &available)) {
                break;
            }
            if (available == 0) {
                break;
            }

            std::vector<char> buf(available + 1, 0);
            DWORD read = 0;
            if (!WinHttpReadData(hRequest, buf.data(), available, &read)) {
                break;
            }
            if (read == 0) {
                break;
            }
            body.append(buf.data(), read);
        }

        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return body;
    }

    // Convenience: Fetch Wikipedia article extract as plain text
    // title: "Artificial General Intelligence"
    static std::string fetch_wikipedia_summary(const std::string& title) {
        auto parse_json_string_after_key = [](const std::string& body, const char* key) -> std::string {
            size_t pos = body.find(key);
            if (pos == std::string::npos) return std::string();
            pos += strlen(key);
            size_t start = body.find('"', pos);
            if (start == std::string::npos) return std::string();
            start++;

            std::string escaped;
            bool esc = false;
            for (size_t i = start; i < body.length(); ++i) {
                char ch = body[i];
                if (!esc && ch == '\\') {
                    esc = true;
                    escaped += ch;
                    continue;
                }
                if (!esc && ch == '"') {
                    break;
                }
                escaped += ch;
                esc = false;
            }

            std::string out;
            for (size_t i = 0; i < escaped.length(); ++i) {
                if (escaped[i] == '\\' && i + 1 < escaped.length()) {
                    char next = escaped[i + 1];
                    if (next == 'n') { out += '\n'; i++; }
                    else if (next == 't') { out += '\t'; i++; }
                    else if (next == '"') { out += '"'; i++; }
                    else if (next == '\\') { out += '\\'; i++; }
                    else if (next == '/') { out += '/'; i++; }
                    else if (next == 'r') { out += '\r'; i++; }
                    else if (next == 'u' && i + 5 < escaped.length()) {
                        std::string hex_str = escaped.substr(i + 2, 4);
                        int codepoint = 0;
                        try { codepoint = std::stoi(hex_str, nullptr, 16); } catch(...) {}
                        if (codepoint > 0 && codepoint <= 0x7F) {
                            out += (char)codepoint;
                        } else if (codepoint <= 0x7FF) {
                            out += (char)(0xC0 | ((codepoint >> 6) & 0x1F));
                            out += (char)(0x80 | (codepoint & 0x3F));
                        } else {
                            out += (char)(0xE0 | ((codepoint >> 12) & 0x0F));
                            out += (char)(0x80 | ((codepoint >> 6) & 0x3F));
                            out += (char)(0x80 | (codepoint & 0x3F));
                        }
                        i += 5; // consume u and 4 hex digits
                    }
                    else { out += escaped[i]; }
                } else {
                    out += escaped[i];
                }
            }
            return out;
        };

        auto fetch_wiki_extract_for_title = [&](const std::string& t) -> std::string {
            std::string path = "/w/api.php?action=query&titles=" + url_encode(t) +
                               "&prop=extracts&exintro=1&explaintext=1&format=json";
            std::string body = fetch_https("en.wikipedia.org", path);
            if (body.empty()) {
                body = fetch("en.wikipedia.org", path);
            }
            return parse_json_string_after_key(body, "\"extract\":");
        };

        std::string result = fetch_wiki_extract_for_title(title);
        if (!result.empty()) return result;

        // Fallback: resolve to best matching title via search API.
        std::string search_path = "/w/api.php?action=query&list=search&srlimit=1&srprop=&format=json&srsearch=" +
                                  url_encode(title);
        std::string search_body = fetch_https("en.wikipedia.org", search_path);
        if (search_body.empty()) {
            search_body = fetch("en.wikipedia.org", search_path);
        }

        std::string resolved_title = parse_json_string_after_key(search_body, "\"title\":");
        if (resolved_title.empty() || resolved_title == title) return std::string();

        return fetch_wiki_extract_for_title(resolved_title);
    }

private:
    static std::string url_encode(const std::string& str) {
        std::string encoded;
        for (unsigned char c : str) {
            if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
                encoded += c;
            } else if (c == ' ') {
                encoded += "%20";
            } else {
                char buf[4];
                snprintf(buf, sizeof(buf), "%%%02X", (unsigned char)c);
                encoded += buf;
            }
        }
        return encoded;
    }
};

bool HTTPClient::initialized = false;
