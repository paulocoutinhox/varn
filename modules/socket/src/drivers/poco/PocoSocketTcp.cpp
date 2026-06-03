#include "varn/socket/SocketTransport.h"

#include <Poco/Net/ServerSocket.h>
#include <Poco/Net/SocketAddress.h>
#include <Poco/Net/StreamSocket.h>

#include <algorithm>
#include <cstdint>
#include <climits>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

namespace varn::socket {

class PocoTcpConnection final : public TcpConnection {
public:
    explicit PocoTcpConnection(Poco::Net::StreamSocket socket) : socket_(std::move(socket)) {}

    void sendBlocking(const std::string& data) override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (data.empty()) {
            return;
        }
        std::size_t offset = 0;
        while (offset < data.size()) {
            const std::size_t remaining = data.size() - offset;
            const int chunk = static_cast<int>(std::min(remaining, static_cast<std::size_t>(INT_MAX)));
            const int n = socket_.sendBytes(data.data() + offset, chunk);
            if (n <= 0) {
                throw std::runtime_error("[socket] The connection was closed before all data could be sent.");
            }
            offset += static_cast<std::size_t>(n);
        }
    }

    std::string receiveBlocking(int maxBytes) override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (maxBytes <= 0) {
            return {};
        }
        std::vector<char> buffer(static_cast<std::size_t>(maxBytes));
        const int received = socket_.receiveBytes(buffer.data(), maxBytes);
        if (received <= 0) {
            return {};
        }
        return std::string(buffer.data(), static_cast<std::size_t>(received));
    }

    void closeBlocking() override {
        std::lock_guard<std::mutex> lock(mutex_);
        try {
            socket_.shutdown();
        } catch (...) {
        }
        try {
            socket_.close();
        } catch (...) {
        }
    }

private:
    std::mutex mutex_;
    Poco::Net::StreamSocket socket_;
};

class PocoTcpListener final : public TcpListener {
public:
    explicit PocoTcpListener(Poco::Net::ServerSocket server) : server_(std::move(server)) {}

    std::shared_ptr<TcpConnection> acceptBlocking() override {
        std::lock_guard<std::mutex> lock(mutex_);
        Poco::Net::StreamSocket accepted = server_.acceptConnection();
        return std::make_shared<PocoTcpConnection>(std::move(accepted));
    }

    void closeBlocking() override {
        std::lock_guard<std::mutex> lock(mutex_);
        try {
            server_.close();
        } catch (...) {
        }
    }

private:
    std::mutex mutex_;
    Poco::Net::ServerSocket server_;
};

void SocketTransport::checkPort(int port) {
    if (port <= 0 || port > 65535) {
        throw std::runtime_error("[socket] Port must be between 1 and 65535.");
    }
}

void SocketTransport::checkBacklog(int backlog) {
    if (backlog <= 0 || backlog > 4096) {
        throw std::runtime_error("[socket] Backlog must be between 1 and 4096.");
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
