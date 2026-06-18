#include "varn/socket/SocketTransport.h"

#include <Poco/Net/ServerSocket.h>
#include <Poco/Net/SocketAddress.h>
#include <Poco/Net/StreamSocket.h>
#include <Poco/Timespan.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <climits>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

namespace varn::socket {

// close interrupts in-flight operations without racing the socket fd.
// a stream connection blocks in send/receive and is interrupted instantly by shutdown(), which only issues a syscall and never invalidates the fd.
// separate read and write mutexes let a duplex protocol send and receive at the same time, and close takes both before the final close().
// a listener or udp socket has no shutdown that reliably interrupts accept/recvFrom, so those poll on a short tick guarded by an atomic close flag, bounding close latency by the poll interval.
static const Poco::Timespan kSocketPollInterval(0, 200000); // 200 ms

class PocoTcpConnection final : public TcpConnection {
public:
    explicit PocoTcpConnection(Poco::Net::StreamSocket socket) : socket_(std::move(socket)) {}

    void sendBlocking(const std::string& data) override {
        std::lock_guard<std::mutex> lock(writeMutex_);

        if (data.empty()) {
            return;
        }

        std::size_t offset = 0;

        while (offset < data.size()) {
            if (closed_.load(std::memory_order_acquire)) {
                throw std::runtime_error("[PocoTcpConnection] The connection was closed.");
            }

            const std::size_t remaining = data.size() - offset;
            const int chunk = static_cast<int>(std::min(remaining, static_cast<std::size_t>(INT_MAX)));
            const int n = socket_.sendBytes(data.data() + offset, chunk);

            if (n <= 0) {
                throw std::runtime_error("[PocoTcpConnection] The connection was closed before all data could be sent.");
            }

            offset += static_cast<std::size_t>(n);
        }
    }

    std::string receiveBlocking(int maxBytes) override {
        std::lock_guard<std::mutex> lock(readMutex_);

        // receiving on a socket the caller already closed is an error, distinct from a peer eof.
        if (closed_.load(std::memory_order_acquire)) {
            throw std::runtime_error("[PocoTcpConnection] The connection is closed.");
        }

        if (maxBytes <= 0) {
            return {};
        }

        // the caller chooses how large a read to request and the buffer is sized to it, like node.
        std::vector<char> buffer(static_cast<std::size_t>(maxBytes));

        // blocks until data, eof, or shutdown() (from closeBlocking) interrupts it.
        const int received = socket_.receiveBytes(buffer.data(), maxBytes);

        // our own close() interrupting the read is an error, while a 0/eof from the peer is an empty result.
        if (closed_.load(std::memory_order_acquire)) {
            throw std::runtime_error("[PocoTcpConnection] The connection is closed.");
        }

        if (received <= 0) {
            return {};
        }

        return std::string(buffer.data(), static_cast<std::size_t>(received));
    }

    void closeBlocking() override {
        // shutdown without a lock interrupts an in-flight send/receive instantly.
        // taking both mutexes afterwards (released quickly once the syscalls return) closes with no thread inside the socket.
        closed_.store(true, std::memory_order_release);

        try {
            socket_.shutdown();
        } catch (...) {
        }

        std::lock_guard<std::mutex> rlock(readMutex_);
        std::lock_guard<std::mutex> wlock(writeMutex_);

        try {
            socket_.close();
        } catch (...) {
        }
    }

private:
    std::mutex readMutex_;
    std::mutex writeMutex_;
    std::atomic<bool> closed_{false};
    Poco::Net::StreamSocket socket_;
};

class PocoTcpListener final : public TcpListener {
public:
    explicit PocoTcpListener(Poco::Net::ServerSocket server) : server_(std::move(server)) {}

    std::shared_ptr<TcpConnection> acceptBlocking() override {
        while (true) {
            std::lock_guard<std::mutex> lock(mutex_);

            if (closed_.load(std::memory_order_acquire)) {
                return nullptr;
            }

            if (!server_.poll(kSocketPollInterval, Poco::Net::Socket::SELECT_READ)) {
                continue;
            }

            Poco::Net::StreamSocket accepted = server_.acceptConnection();
            return std::make_shared<PocoTcpConnection>(std::move(accepted));
        }
    }

    void closeBlocking() override {
        closed_.store(true, std::memory_order_release);
        std::lock_guard<std::mutex> lock(mutex_);
        try {
            server_.close();
        } catch (...) {
        }
    }

private:
    std::mutex mutex_;
    std::atomic<bool> closed_{false};
    Poco::Net::ServerSocket server_;
};

void SocketTransport::checkPort(int port) {
    if (port <= 0 || port > 65535) {
        throw std::runtime_error("[SocketTransport] Port must be between 1 and 65535.");
    }
}

void SocketTransport::checkBacklog(int backlog) {
    if (backlog <= 0 || backlog > 4096) {
        throw std::runtime_error("[SocketTransport] Backlog must be between 1 and 4096.");
    }
}

std::shared_ptr<TcpConnection> SocketTransport::connectBlocking(const std::string& host, int port) {
    checkPort(port);
    Poco::Net::SocketAddress address(host, static_cast<std::uint16_t>(port));
    Poco::Net::StreamSocket socket;
    socket.connect(address);
    return std::make_shared<PocoTcpConnection>(std::move(socket));
}

std::shared_ptr<TcpListener> SocketTransport::listenBlocking(const std::string& host, int port, int backlog) {
    checkPort(port);
    checkBacklog(backlog);
    Poco::Net::SocketAddress address(host, static_cast<std::uint16_t>(port));
    Poco::Net::ServerSocket server(address, backlog);
    return std::make_shared<PocoTcpListener>(std::move(server));
}

} // namespace varn::socket
