#include "netplay/connection.h"
#include "netplay/protocol.h"
#include "ws_util.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winhttp.h>
#else
#include "sock.h"
#include <filesystem>
#if NETPLAY_HAVE_MBEDTLS
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/error.h>
#include <mbedtls/net_sockets.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>
#endif
#endif

namespace netplay {

bool ParseUrl(const std::string& text, Url& out) {
    std::string s = CleanText(text, 512);
    out = Url{};
    size_t at = 0;
    auto starts = [&](const char* p) {
        const size_t n = std::strlen(p);
        if (s.size() >= n) {
            for (size_t i = 0; i < n; ++i) if (std::tolower(uint8_t(s[i])) != p[i]) return false;
            at = n;
            return true;
        }
        return false;
    };
    if (starts("wss://") || starts("https://")) out.secure = true;
    else if (starts("ws://") || starts("http://")) out.secure = false;
    const size_t slash = s.find('/', at);
    std::string hostport = s.substr(at, slash == std::string::npos ? std::string::npos : slash - at);
    if (slash != std::string::npos) out.path = s.substr(slash);
    if (hostport.empty()) return false;
    uint32_t port = 0;
    const size_t colon = hostport.rfind(':');
    if (colon != std::string::npos && hostport.find(']') == std::string::npos) {
        for (size_t i = colon + 1; i < hostport.size(); ++i) {
            if (hostport[i] < '0' || hostport[i] > '9') return false;
            port = port * 10 + uint32_t(hostport[i] - '0');
            if (port > 65535) return false;
        }
        hostport.resize(colon);
    }
    if (hostport.empty()) return false;
    out.host = hostport;
    const bool bare = at == 0;
    out.port = uint16_t(port ? port : (out.secure ? 443 : (bare ? kDefaultPort : 80)));
    return true;
}

namespace {

using Clock = std::chrono::steady_clock;

class QueuedConnection : public Connection {
public:
    void Send(std::vector<uint8_t> message) override {
        {
            std::lock_guard lock(mutex_);
            if (closed_) return;
            outgoing_.push_back(std::move(message));
        }
        wake_.notify_all();
    }
    bool Poll(Event& out) override {
        std::lock_guard lock(mutex_);
        if (incoming_.empty()) return false;
        out = std::move(incoming_.front());
        incoming_.pop_front();
        return true;
    }

protected:
    void Push(Event e) {
        std::lock_guard lock(mutex_);
        if (e.kind == Event::Closed) {
            if (closed_) return;
            closed_ = true;
        }
        incoming_.push_back(std::move(e));
    }
    void Fail(std::string reason) { Push(Event{Event::Closed, {}, std::move(reason)}); }
    bool TakeOutgoing(std::vector<uint8_t>& out) {
        std::lock_guard lock(mutex_);
        if (outgoing_.empty()) return false;
        out = std::move(outgoing_.front());
        outgoing_.pop_front();
        return true;
    }

    std::mutex mutex_;
    std::condition_variable wake_;
    std::deque<Event> incoming_;
    std::deque<std::vector<uint8_t>> outgoing_;
    bool closed_ = false;
    std::atomic<bool> stop_{false};
};

#ifdef _WIN32

std::wstring Widen(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
    std::wstring w(size_t(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), w.data(), n);
    return w;
}

class WinHttpConnection final : public QueuedConnection {
public:
    explicit WinHttpConnection(Url url) : url_(std::move(url)) {
        reader_ = std::thread([this] { Run(); });
    }
    ~WinHttpConnection() override { Close(); }

    void Close() override {
        stop_ = true;
        wake_.notify_all();
        {
            std::lock_guard lock(handleMutex_);
            // Closing the handles aborts a blocked receive. WinHttpWebSocketClose would wait for the
            // server's close reply, which may never come.
            for (HINTERNET* h : {&socket_, &request_, &connect_, &session_}) {
                if (*h) { WinHttpCloseHandle(*h); *h = nullptr; }
            }
        }
        if (reader_.joinable()) reader_.join();
        if (writer_.joinable()) writer_.join();
        Fail("");
    }

private:
    std::string Connect() {
        session_ = WinHttpOpen(L"ExcitePC-NetworkPlay/1", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                               WINHTTP_NO_PROXY_BYPASS, 0);
        if (!session_) {
            session_ = WinHttpOpen(L"ExcitePC-NetworkPlay/1", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
                                   WINHTTP_NO_PROXY_BYPASS, 0);
        }
        if (!session_) return "Windows networking (WinHTTP) isn't available.";
        WinHttpSetTimeouts(session_, 8000, 8000, 10000, 10000);
        connect_ = WinHttpConnect(session_, Widen(url_.host).c_str(), url_.port, 0);
        if (!connect_) return "Couldn't find the server " + url_.host + ".";
        request_ = WinHttpOpenRequest(connect_, L"GET", Widen(url_.path).c_str(), nullptr, WINHTTP_NO_REFERER,
                                      WINHTTP_DEFAULT_ACCEPT_TYPES, url_.secure ? WINHTTP_FLAG_SECURE : 0);
        if (!request_) return "Couldn't open a request to the server.";
        WinHttpSetOption(request_, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, nullptr, 0);
        if (!WinHttpSendRequest(request_, WINHTTP_NO_ADDITIONAL_HEADERS, 0, nullptr, 0, 0, 0) ||
            !WinHttpReceiveResponse(request_, nullptr)) {
            const DWORD e = GetLastError();
            if (e == ERROR_WINHTTP_NAME_NOT_RESOLVED) return "Couldn't find the server " + url_.host + ". Check the address and your internet connection.";
            if (e == ERROR_WINHTTP_CANNOT_CONNECT || e == ERROR_WINHTTP_TIMEOUT)
                return "Couldn't reach " + url_.host + ":" + std::to_string(url_.port) + ". Is the room's PC running and its port open?";
            if (e == ERROR_WINHTTP_SECURE_FAILURE) return "The server's security certificate was refused.";
            return "Couldn't connect to the server (WinHTTP error " + std::to_string(e) + ").";
        }
        DWORD status = 0, size = sizeof status;
        WinHttpQueryHeaders(request_, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                            &status, &size, WINHTTP_NO_HEADER_INDEX);
        if (status != 101) return "That address isn't a Network Play server (HTTP " + std::to_string(status) + ").";
        HINTERNET ws = WinHttpWebSocketCompleteUpgrade(request_, 0);
        if (!ws) return "The server didn't accept the connection.";
        std::lock_guard lock(handleMutex_);
        socket_ = ws;
        WinHttpCloseHandle(request_);
        request_ = nullptr;
        return {};
    }

    void Run() {
        std::string error = Connect();
        if (!error.empty() || stop_) {
            Fail(stop_ ? "" : error);
            return;
        }
        Push(Event{Event::Connected, {}, {}});
        writer_ = std::thread([this] { Write(); });
        std::vector<uint8_t> msg, buf(16384);
        while (!stop_) {
            DWORD got = 0;
            WINHTTP_WEB_SOCKET_BUFFER_TYPE type{};
            const DWORD r = WinHttpWebSocketReceive(socket_, buf.data(), DWORD(buf.size()), &got, &type);
            if (r != NO_ERROR) {
                Fail(stop_ ? "" : "Lost the connection to the server.");
                break;
            }
            if (type == WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE) {
                Fail("The server closed the connection.");
                break;
            }
            msg.insert(msg.end(), buf.begin(), buf.begin() + got);
            if (msg.size() > kMaxMessageBytes + 16) {
                Fail("The server sent a message that was too large.");
                break;
            }
            if (type == WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE) {
                Push(Event{Event::Message, std::move(msg), {}});
                msg.clear();
            } else if (type == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE) {
                msg.clear();
            }
        }
        stop_ = true;
        wake_.notify_all();
    }

    void Write() {
        std::vector<uint8_t> m;
        while (!stop_) {
            {
                std::unique_lock lock(mutex_);
                wake_.wait_for(lock, std::chrono::milliseconds(100), [&] { return stop_ || !outgoing_.empty(); });
            }
            while (!stop_ && TakeOutgoing(m)) {
                if (WinHttpWebSocketSend(socket_, WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE, m.data(), DWORD(m.size())) != NO_ERROR) {
                    Fail(stop_ ? "" : "Lost the connection to the server.");
                    return;
                }
            }
        }
    }

    Url url_;
    std::mutex handleMutex_;
    HINTERNET session_ = nullptr, connect_ = nullptr, request_ = nullptr, socket_ = nullptr;
    std::thread reader_, writer_;
};

#else  // POSIX

class SocketConnection final : public QueuedConnection {
public:
    explicit SocketConnection(Url url) : url_(std::move(url)) {
        thread_ = std::thread([this] { Run(); });
    }
    ~SocketConnection() override { Close(); }

    void Close() override {
        stop_ = true;
        wake_.notify_all();
        if (thread_.joinable()) thread_.join();
        Fail("");
    }

private:
#if NETPLAY_HAVE_MBEDTLS
    struct Tls {
        mbedtls_ssl_context ssl;
        mbedtls_ssl_config conf;
        mbedtls_x509_crt ca;
        mbedtls_entropy_context entropy;
        mbedtls_ctr_drbg_context drbg;
        Tls() {
            mbedtls_ssl_init(&ssl); mbedtls_ssl_config_init(&conf); mbedtls_x509_crt_init(&ca);
            mbedtls_entropy_init(&entropy); mbedtls_ctr_drbg_init(&drbg);
        }
        ~Tls() {
            mbedtls_ssl_free(&ssl); mbedtls_ssl_config_free(&conf); mbedtls_x509_crt_free(&ca);
            mbedtls_ctr_drbg_free(&drbg); mbedtls_entropy_free(&entropy);
        }
    };
    std::unique_ptr<Tls> tls_;

    static int TlsSend(void* ctx, const unsigned char* p, size_t n) {
        const auto r = ::send(*static_cast<int*>(ctx), p, n, sock::kNoSignal);
        if (r < 0) return sock::WouldBlock() ? MBEDTLS_ERR_SSL_WANT_WRITE : MBEDTLS_ERR_NET_SEND_FAILED;
        return int(r);
    }
    static int TlsRecv(void* ctx, unsigned char* p, size_t n) {
        const auto r = ::recv(*static_cast<int*>(ctx), p, n, 0);
        if (r < 0) return sock::WouldBlock() ? MBEDTLS_ERR_SSL_WANT_READ : MBEDTLS_ERR_NET_RECV_FAILED;
        return int(r);
    }

    std::string StartTls() {
        tls_ = std::make_unique<Tls>();
        Tls& t = *tls_;
        if (mbedtls_ctr_drbg_seed(&t.drbg, mbedtls_entropy_func, &t.entropy, nullptr, 0) != 0) return "TLS setup failed.";
        bool haveCa = false;
        for (const char* p : {"/etc/ssl/certs/ca-certificates.crt", "/etc/pki/tls/certs/ca-bundle.crt", "/etc/ssl/ca-bundle.pem",
                              "/etc/ssl/cert.pem", "/etc/pki/ca-trust/extracted/pem/tls-ca-bundle.pem"}) {
            std::error_code ec;
            if (std::filesystem::exists(p, ec) && mbedtls_x509_crt_parse_file(&t.ca, p) >= 0) { haveCa = true; break; }
        }
        if (!haveCa) return "No trusted certificates found on this system (install ca-certificates).";
        mbedtls_ssl_config_defaults(&t.conf, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT);
        mbedtls_ssl_conf_authmode(&t.conf, MBEDTLS_SSL_VERIFY_REQUIRED);
        mbedtls_ssl_conf_ca_chain(&t.conf, &t.ca, nullptr);
        mbedtls_ssl_conf_rng(&t.conf, mbedtls_ctr_drbg_random, &t.drbg);
        if (mbedtls_ssl_setup(&t.ssl, &t.conf) != 0) return "TLS setup failed.";
        mbedtls_ssl_set_hostname(&t.ssl, url_.host.c_str());
        mbedtls_ssl_set_bio(&t.ssl, &s_, TlsSend, TlsRecv, nullptr);
        const auto until = Clock::now() + std::chrono::seconds(10);
        for (;;) {
            const int r = mbedtls_ssl_handshake(&t.ssl);
            if (r == 0) return {};
            if (r != MBEDTLS_ERR_SSL_WANT_READ && r != MBEDTLS_ERR_SSL_WANT_WRITE) {
                if (r == MBEDTLS_ERR_X509_CERT_VERIFY_FAILED) return "The server's security certificate was refused.";
                char text[128];
                mbedtls_strerror(r, text, sizeof text);
                return std::string("Secure connection failed: ") + text;
            }
            if (stop_ || Clock::now() > until) return "The server didn't answer.";
            sock::PollFd fd{s_, short(r == MBEDTLS_ERR_SSL_WANT_READ ? POLLIN : POLLOUT), 0};
            sock::Poll(&fd, 1, 100);
        }
    }
#endif

    // >0 bytes, 0 nothing yet, -1 closed or failed.
    long RawRead(uint8_t* p, size_t n) {
#if NETPLAY_HAVE_MBEDTLS
        if (tls_) {
            const int r = mbedtls_ssl_read(&tls_->ssl, p, n);
            if (r > 0) return r;
            if (r == MBEDTLS_ERR_SSL_WANT_READ || r == MBEDTLS_ERR_SSL_WANT_WRITE) return 0;
            return -1;
        }
#endif
        const auto r = ::recv(s_, reinterpret_cast<char*>(p), n, 0);
        if (r > 0) return long(r);
        if (r < 0 && sock::WouldBlock()) return 0;
        return -1;
    }
    bool RawWrite(const uint8_t* p, size_t n) {
        const auto until = Clock::now() + std::chrono::seconds(10);
        while (n > 0) {
            long r;
#if NETPLAY_HAVE_MBEDTLS
            if (tls_) {
                const int t = mbedtls_ssl_write(&tls_->ssl, p, n);
                r = t > 0 ? t : ((t == MBEDTLS_ERR_SSL_WANT_READ || t == MBEDTLS_ERR_SSL_WANT_WRITE) ? 0 : -1);
            } else
#endif
            {
                const auto t = ::send(s_, reinterpret_cast<const char*>(p), n, sock::kNoSignal);
                r = t >= 0 ? long(t) : (sock::WouldBlock() ? 0 : -1);
            }
            if (r < 0) return false;
            if (r == 0) {
                if (stop_ || Clock::now() > until) return false;
                sock::PollFd fd{s_, POLLOUT, 0};
                sock::Poll(&fd, 1, 50);
                continue;
            }
            p += r;
            n -= size_t(r);
        }
        return true;
    }
    bool Pending() const {
#if NETPLAY_HAVE_MBEDTLS
        if (tls_) return mbedtls_ssl_get_bytes_avail(&tls_->ssl) > 0;
#endif
        return false;
    }

    std::string Connect() {
        if (url_.secure) {
#if !NETPLAY_HAVE_MBEDTLS
            return "This build can't make secure (wss://) connections.";
#endif
        }
        addrinfo hints{}, *res = nullptr;
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        if (getaddrinfo(url_.host.c_str(), std::to_string(url_.port).c_str(), &hints, &res) != 0 || !res) {
            return "Couldn't find the server " + url_.host + ". Check the address and your internet connection.";
        }
        std::string error = "Couldn't reach " + url_.host + ":" + std::to_string(url_.port) + ". Is the room's PC running and its port open?";
        for (addrinfo* a = res; a && s_ < 0 && !stop_; a = a->ai_next) {
            const int s = ::socket(a->ai_family, a->ai_socktype, a->ai_protocol);
            if (s < 0) continue;
            sock::SetNonBlocking(s);
            if (::connect(s, a->ai_addr, a->ai_addrlen) != 0 && !sock::WouldBlock()) { sock::Close(s); continue; }
            sock::PollFd fd{s, POLLOUT, 0};
            int err = 1;
            socklen_t len = sizeof err;
            if (sock::Poll(&fd, 1, 8000) == 1 && getsockopt(s, SOL_SOCKET, SO_ERROR, &err, &len) == 0 && err == 0) {
                s_ = s;
            } else {
                sock::Close(s);
            }
        }
        freeaddrinfo(res);
        if (s_ < 0) return stop_ ? "" : error;
        int on = 1;
        setsockopt(s_, IPPROTO_TCP, TCP_NODELAY, &on, sizeof on);
#if NETPLAY_HAVE_MBEDTLS
        if (url_.secure) {
            if (std::string e = StartTls(); !e.empty()) return e;
        }
#endif
        const std::string key = ws::RandomKey();
        std::string host = url_.host;
        if ((url_.secure && url_.port != 443) || (!url_.secure && url_.port != 80)) host += ":" + std::to_string(url_.port);
        const std::string req = "GET " + url_.path + " HTTP/1.1\r\nHost: " + host +
                                "\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Version: 13\r\n"
                                "User-Agent: ExcitePC-NetworkPlay/1\r\nSec-WebSocket-Key: " + key + "\r\n\r\n";
        if (!RawWrite(reinterpret_cast<const uint8_t*>(req.data()), req.size())) return "Lost the connection to the server.";
        std::string head;
        const auto until = Clock::now() + std::chrono::seconds(10);
        uint8_t buf[4096];
        while (head.find("\r\n\r\n") == std::string::npos) {
            if (stop_) return "";
            if (Clock::now() > until || head.size() > 16384) return "The server didn't answer.";
            const long r = RawRead(buf, sizeof buf);
            if (r < 0) return "The server closed the connection.";
            if (r == 0) {
                sock::PollFd fd{s_, POLLIN, 0};
                sock::Poll(&fd, 1, 50);
                continue;
            }
            head.append(reinterpret_cast<char*>(buf), size_t(r));
        }
        const size_t end = head.find("\r\n\r\n");
        const std::string status = head.substr(0, head.find("\r\n"));
        if (status.find(" 101") == std::string::npos) return "That address isn't a Network Play server (" + status + ").";
        if (ws::Header(head.substr(0, end + 2), "Sec-WebSocket-Accept") != ws::AcceptKey(key)) return "The server answered wrongly.";
        const std::string rest = head.substr(end + 4);
        parser_.Feed(reinterpret_cast<const uint8_t*>(rest.data()), rest.size());
        return {};
    }

    void Run() {
        std::string error = Connect();
        if (!error.empty() || stop_) {
            Fail(stop_ ? "" : error);
            if (s_ >= 0) sock::Close(s_);
            return;
        }
        Push(Event{Event::Connected, {}, {}});
        auto lastHeard = Clock::now(), lastPing = Clock::now();
        uint8_t buf[16384];
        std::vector<uint8_t> m;
        std::string reason;
        for (;;) {
            if (stop_) {
                auto close = ws::Frame(ws::Close, nullptr, 0, true);
                RawWrite(close.data(), close.size());
                break;
            }
            while (TakeOutgoing(m)) {
                auto f = ws::Frame(ws::Binary, m.data(), m.size(), true);
                if (!RawWrite(f.data(), f.size())) { reason = "Lost the connection to the server."; break; }
            }
            if (!reason.empty()) break;
            const auto now = Clock::now();
            if (now - lastPing > std::chrono::seconds(10)) {
                lastPing = now;
                auto f = ws::Frame(ws::Ping, nullptr, 0, true);
                RawWrite(f.data(), f.size());
            }
            if (now - lastHeard > std::chrono::seconds(30)) { reason = "The server stopped answering."; break; }
            if (!Pending()) {
                sock::PollFd fd{s_, POLLIN, 0};
                sock::Poll(&fd, 1, 15);
            }
            const long r = RawRead(buf, sizeof buf);
            if (r < 0) { reason = "Lost the connection to the server."; break; }
            if (r == 0) continue;
            lastHeard = now;
            parser_.Feed(buf, size_t(r));
            ws::Message msg;
            int k;
            while ((k = parser_.Next(msg)) == 1) {
                if (msg.opcode == ws::Binary) Push(Event{Event::Message, std::move(msg.data), {}});
                else if (msg.opcode == ws::Ping) {
                    auto f = ws::Frame(ws::Pong, msg.data.data(), msg.data.size(), true);
                    RawWrite(f.data(), f.size());
                } else if (msg.opcode == ws::Close) { reason = "The server closed the connection."; break; }
            }
            if (k < 0) reason = "The server sent something unexpected.";
            if (!reason.empty()) break;
        }
#if NETPLAY_HAVE_MBEDTLS
        if (tls_) mbedtls_ssl_close_notify(&tls_->ssl);
#endif
        sock::Close(s_);
        s_ = -1;
        Fail(stop_ ? "" : reason);
    }

    Url url_;
    int s_ = -1;
    ws::Parser parser_{kMaxMessageBytes + 16};
    std::thread thread_;
};

#endif

class FailedConnection final : public QueuedConnection {
public:
    explicit FailedConnection(std::string why) { Fail(std::move(why)); }
    void Close() override {}
};

} // namespace

std::unique_ptr<Connection> Connection::Open(const std::string& url) {
    Url u;
    if (!ParseUrl(url, u)) return std::make_unique<FailedConnection>("The server address \"" + url + "\" isn't valid.");
#ifdef _WIN32
    return std::make_unique<WinHttpConnection>(std::move(u));
#else
    return std::make_unique<SocketConnection>(std::move(u));
#endif
}

} // namespace netplay
