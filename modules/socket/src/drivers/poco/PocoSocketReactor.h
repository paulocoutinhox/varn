#pragma once

#include "varn/runtime/IoService.h"

#include <Poco/Net/PollSet.h>
#include <Poco/Net/Socket.h>

#include <atomic>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace varn::runtime {
class Runtime;
}

namespace varn::socket {

class PocoSocketReactor : public varn::runtime::IoService,
                          public std::enable_shared_from_this<PocoSocketReactor> {
public:
    PocoSocketReactor();
    ~PocoSocketReactor() override;

    static std::shared_ptr<PocoSocketReactor> forRuntime(varn::runtime::Runtime& runtime);

    PocoSocketReactor(const PocoSocketReactor&) = delete;
    PocoSocketReactor& operator=(const PocoSocketReactor&) = delete;

    void start();
    void stop() override;

    bool running() const { return active.load(std::memory_order_acquire); }

    using Handler = std::function<bool()>;

    void watchRead(const Poco::Net::Socket& socket, Handler handler);
    void watchWrite(const Poco::Net::Socket& socket, Handler handler);
    void closeSocket(const Poco::Net::Socket& socket);

private:
    struct Entry {
        std::deque<Handler> readers;
        std::deque<Handler> writers;
    };

    void submit(std::function<void()> command);
    void run();
    void drainCommands();
    void refreshMode(const Poco::Net::Socket& socket);

    std::thread thread;
    Poco::Net::PollSet pollSet;
    std::mutex commandMutex;
    std::vector<std::function<void()>> commands;
    std::map<Poco::Net::Socket, Entry> entries;
    std::atomic<bool> active{false};
};

} // namespace varn::socket
