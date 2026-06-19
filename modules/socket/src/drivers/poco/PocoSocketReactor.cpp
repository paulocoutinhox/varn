#include "PocoSocketReactor.h"

#include "varn/runtime/Runtime.h"

#include <Poco/Timespan.h>

#include <utility>

namespace varn::socket {

std::shared_ptr<PocoSocketReactor> PocoSocketReactor::forRuntime(varn::runtime::Runtime& runtime) {
    if (auto* existing = runtime.ioService()) {
        return static_cast<PocoSocketReactor*>(existing)->shared_from_this();
    }
    auto reactor = std::make_shared<PocoSocketReactor>();
    reactor->start();
    runtime.setIoService(reactor);
    return reactor;
}

namespace {
constexpr int kPollRead = Poco::Net::PollSet::POLL_READ;
constexpr int kPollWrite = Poco::Net::PollSet::POLL_WRITE;
constexpr int kPollError = Poco::Net::PollSet::POLL_ERROR;
const Poco::Timespan kPollTimeout(1, 0); // a one-second tick bounds the wait if a wakeUp is ever missed.
} // namespace

PocoSocketReactor::PocoSocketReactor() = default;

PocoSocketReactor::~PocoSocketReactor() {
    stop();
}

void PocoSocketReactor::start() {
    active.store(true, std::memory_order_release);
    thread = std::thread([this] { run(); });
}

void PocoSocketReactor::stop() {
    if (!active.exchange(false)) {
        return;
    }
    pollSet.wakeUp();
    if (thread.joinable()) {
        thread.join();
    }

    // with the i/o thread joined, its state is cleared here without a race, dropping the handlers to release the captured handles and promises and break the reactor/handle cycle.
    {
        std::lock_guard<std::mutex> lock(commandMutex);
        commands.clear();
    }
    entries.clear();
    pollSet.clear();
}

void PocoSocketReactor::submit(std::function<void()> command) {
    {
        std::lock_guard<std::mutex> lock(commandMutex);
        commands.push_back(std::move(command));
    }
    pollSet.wakeUp();
}

void PocoSocketReactor::watchRead(const Poco::Net::Socket& socket, Handler handler) {
    submit([this, socket, handler = std::move(handler)]() mutable {
        entries[socket].readers.push_back(std::move(handler));
        refreshMode(socket);
    });
}

void PocoSocketReactor::watchWrite(const Poco::Net::Socket& socket, Handler handler) {
    submit([this, socket, handler = std::move(handler)]() mutable {
        entries[socket].writers.push_back(std::move(handler));
        refreshMode(socket);
    });
}

void PocoSocketReactor::closeSocket(const Poco::Net::Socket& socket) {
    submit([this, socket]() mutable {
        // close the fd first so each pending handler's next i/o call fails, then run the handlers so their promises reject and their keep-alive is released instead of being abandoned.
        try {
            socket.impl()->close();
        } catch (...) {
        }

        auto it = entries.find(socket);
        if (it != entries.end()) {
            Entry drained = std::move(it->second);
            entries.erase(it);
            for (auto& reader : drained.readers) {
                try {
                    reader();
                } catch (...) {
                }
            }
            for (auto& writer : drained.writers) {
                try {
                    writer();
                } catch (...) {
                }
            }
        }
        pollSet.remove(socket);
    });
}

void PocoSocketReactor::refreshMode(const Poco::Net::Socket& socket) {
    auto it = entries.find(socket);
    if (it == entries.end()) {
        return;
    }

    int mode = 0;
    if (!it->second.readers.empty()) {
        mode |= kPollRead;
    }
    if (!it->second.writers.empty()) {
        mode |= kPollWrite;
    }

    if (mode == 0) {
        entries.erase(it);
        pollSet.remove(socket);
        return;
    }

    if (pollSet.has(socket)) {
        pollSet.update(socket, mode);
    } else {
        pollSet.add(socket, mode);
    }
}

void PocoSocketReactor::drainCommands() {
    std::vector<std::function<void()>> pending;
    {
        std::lock_guard<std::mutex> lock(commandMutex);
        pending.swap(commands);
    }
    for (auto& command : pending) {
        command();
    }
}

void PocoSocketReactor::run() {
    while (active.load(std::memory_order_acquire)) {
        drainCommands();

        Poco::Net::PollSet::SocketModeMap ready;
        try {
            ready = pollSet.poll(kPollTimeout);
        } catch (...) {
            continue;
        }

        if (!active.load(std::memory_order_acquire)) {
            break;
        }

        for (const auto& readyEntry : ready) {
            const Poco::Net::Socket& socket = readyEntry.first;
            const int modes = readyEntry.second;

            auto it = entries.find(socket);
            if (it == entries.end()) {
                continue;
            }

            const bool errored = (modes & kPollError) != 0;

            // serve the front operation of each ready direction, containing any handler defect so a throw drops that operation rather than killing the i/o thread.
            if (!it->second.readers.empty() && ((modes & kPollRead) || errored)) {
                bool done = true;
                try {
                    done = it->second.readers.front()();
                } catch (...) {
                }
                if (done) {
                    it->second.readers.pop_front();
                }
            }
            if (!it->second.writers.empty() && ((modes & kPollWrite) || errored)) {
                bool done = true;
                try {
                    done = it->second.writers.front()();
                } catch (...) {
                }
                if (done) {
                    it->second.writers.pop_front();
                }
            }

            refreshMode(socket);
        }
    }
}

} // namespace varn::socket
