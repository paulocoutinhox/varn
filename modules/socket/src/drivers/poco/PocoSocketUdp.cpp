#include "varn/socket/SocketTransport.h"

#include <Poco/Net/DatagramSocket.h>
#include <Poco/Net/SocketAddress.h>

#include <cstdint>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

namespace varn::socket {

namespace {

class PocoUdpSocket final : public UdpSocket {
public:
    explicit PocoUdpSocket(Poco::Net::DatagramSocket socket) : socket_(std::move(socket)) {}

    void sendToBlocking(const std::string& host, int port, const std::string& data) override {
        std::lock_guard<std::mutex> lock(mutex_);
        Poco::Net::SocketAddress destination(host, static_cast<std::uint16_t>(port));
        const int sent = socket_.sendTo(data.data(), static_cast<int>(data.size()), destination);
        if (sent < 0 || static_cast<std::size_t>(sent) != data.size()) {
            throw std::runtime_error("[socket] The datagram could not be fully sent.");
        }
    }

    UdpDatagram receiveFromBlocking(int maxBytes) override {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<char> buffer(static_cast<std::size_t>(maxBytes));
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

    void closeBlocking() override {
        std::lock_guard<std::mutex> lock(mutex_);
        try {
            socket_.close();
        } catch (...) {
        }
    }

private:
    std::mutex mutex_;
    Poco::Net::DatagramSocket socket_;
};

} // namespace

std::shared_ptr<UdpSocket> SocketTransport::bindUdpBlocking(const std::string& host, int port) {
    checkPort(port);
    Poco::Net::SocketAddress address(host, static_cast<std::uint16_t>(port));
    Poco::Net::DatagramSocket socket(address, false);
    return std::make_shared<PocoUdpSocket>(std::move(socket));
}

} // namespace varn::socket
