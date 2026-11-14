// Minimal socket portability for the PC relay and the Linux client.
#pragma once

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
namespace netplay::sock {
using Socket = SOCKET;
constexpr Socket kInvalid = INVALID_SOCKET;
inline int Poll(WSAPOLLFD* fds, unsigned long n, int timeoutMs) { return WSAPoll(fds, n, timeoutMs); }
using PollFd = WSAPOLLFD;
inline void Close(Socket s) { closesocket(s); }
inline bool WouldBlock() { const int e = WSAGetLastError(); return e == WSAEWOULDBLOCK || e == WSAEINPROGRESS; }
inline void SetNonBlocking(Socket s) { u_long on = 1; ioctlsocket(s, FIONBIO, &on); }
inline bool Startup() { WSADATA d; return WSAStartup(MAKEWORD(2, 2), &d) == 0; }
using ssize = int;
constexpr int kNoSignal = 0;
} // namespace netplay::sock
#else
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
namespace netplay::sock {
using Socket = int;
constexpr Socket kInvalid = -1;
using PollFd = pollfd;
inline int Poll(pollfd* fds, unsigned long n, int timeoutMs) { return ::poll(fds, nfds_t(n), timeoutMs); }
inline void Close(Socket s) { ::close(s); }
inline bool WouldBlock() { return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINPROGRESS; }
inline void SetNonBlocking(Socket s) { fcntl(s, F_SETFL, fcntl(s, F_GETFL, 0) | O_NONBLOCK); }
inline bool Startup() { return true; }
using ssize = ssize_t;
#ifdef MSG_NOSIGNAL
constexpr int kNoSignal = MSG_NOSIGNAL;
#else
constexpr int kNoSignal = 0;
#endif
} // namespace netplay::sock
#endif
