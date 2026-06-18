#include "varn/socket/SocketTransport.h"

#include <Poco/Net/DatagramSocket.h>
#include <Poco/Net/SocketAddress.h>
#include <Poco/Timespan.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

namespace varn::socket {

// receive is a poll loop guarded by an atomic close flag so close() never blocks behind an in-flight receiveFrom and never races the socket fd, with the same close strategy as PocoSocketTcp.cpp.
static const Poco::Timespan kUdpPollInterval(0, 200000); // 200 ms

class PocoUdpSocket final : public UdpSocket {
public:
    explicit PocoUdpSocket(Poco::Net::DatagramSocket socket) : socket_(std::move(socket)) {}

    void sendToBlocking(const std::string& host, int port, const std::string& data) override {
        std::lock_guard<std::mutex> lock(mutex_);

        if (closed_.load(std::memory_order_acquire)) {
            throw std::runtime_error("[PocoUdpSocket] The socket was closed.");
        }

        Poco::Net::SocketAddress destination(host, static_cast<std::uint16_t>(port));
        const int sent = socket_.sendTo(data.data(), static_cast<int>(data.size()), destination);

        if (sent < 0 || static_cast<std::size_t>(sent) != data.size()) {
            throw std::runtime_error("[PocoUdpSocket] The datagram could not be fully sent.");
        }
    }

    UdpDatagram receiveFromBlocking(int maxBytes) override {
        if (maxBytes <= 0) {
            return {};
        }

        // a datagram never exceeds this, so bound the buffer instead of trusting the requested size.
        constexpr int kMaxDatagramBytes = 65536;
        if (maxBytes > kMaxDatagramBytes) {
            maxBytes = kMaxDatagramBytes;
        }

        std::vector<char> buffer(static_cast<std::size_t>(maxBytes));

        while (true) {
            std::lock_guard<std::mutex> lock(mutex_);

            if (closed_.load(std::memory_order_acquire)) {
                return {};
            }

            if (!socket_.poll(kUdpPollInterval, Poco::Net::Socket::SELECT_READ)) {
                continue;
            }

            Poco::Net::SocketAddress sender;
            const int received = socket_.receiveFrom(buffer.data(), maxBytes, sender);

            UdpDatagram datagram;
            if (received > 0) {
                datagram.data.assign(buffer.data(), static_cast<std::size_t>(received));
            }

            datagram.host = sender.host().toString();
            datagram.port = static_cast<int>(sender.port());
            return datagram;
        }
    }

    void closeBlocking() override {
        closed_.store(true, std::memory_order_release);

        std::lock_guard<std::mutex> lock(mutex_);

        try {
            socket_.close();
        } catch (...) {
        }
    }

private:
    std::mutex mutex_;
    std::atomic<bool> closed_{false};
    Poco::Net::DatagramSocket socket_;
};

std::shared_ptr<UdpSocket> SocketTransport::bindUdpBlocking(const std::string& host, int port) {
    checkPort(port);
    Poco::Net::SocketAddress address(host, static_cast<std::uint16_t>(port));
    Poco::Net::DatagramSocket socket(address, false);
    return std::make_shared<PocoUdpSocket>(std::move(socket));
}

} // namespace varn::socket
