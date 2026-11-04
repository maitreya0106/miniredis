#include <pthread.h>

#include <atomic>
#include <csignal>
#include <exception>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <variant>
#include <vector>

#include "persistence/aof_loader.h"
#include "persistence/aof_writer.h"
#include "server/command_dispatcher.h"
#include "server/config.h"
#include "server/server.h"
#include "store/expiry_sweeper.h"
#include "store/store.h"

namespace {

using namespace miniredis;

// Must run before any thread starts: the mask is inherited, so every thread blocks these
// signals and only the SignalWaiter receives them, via sigwait().
sigset_t block_shutdown_signals() {
    sigset_t signals{};
    sigemptyset(&signals);
    sigaddset(&signals, SIGINT);
    sigaddset(&signals, SIGTERM);
    pthread_sigmask(SIG_BLOCK, &signals, nullptr);
    return signals;
}

// Runs the handler on an ordinary thread, so it may lock mutexes and use non-signal-safe code.
class SignalWaiter {
public:
    template <typename F>
    SignalWaiter(sigset_t signals, F&& handler)
        : signals_(signals), thread_([this, handler = std::forward<F>(handler)] {
              int signal = 0;
              sigwait(&signals_, &signal);
              received_ = true;
              handler();
          }) {}

    SignalWaiter(const SignalWaiter&) = delete;
    SignalWaiter& operator=(const SignalWaiter&) = delete;
    SignalWaiter(SignalWaiter&&) = delete;
    SignalWaiter& operator=(SignalWaiter&&) = delete;

    ~SignalWaiter() {
        if (!received_) {
            // Wakes sigwait() so the thread can exit; SIGTERM is blocked, so it terminates nothing.
            // NOLINTNEXTLINE(bugprone-bad-signal-to-kill-thread)
            pthread_kill(thread_.native_handle(), SIGTERM);
        }
        thread_.join();
    }

private:
    sigset_t signals_;
    std::atomic<bool> received_{false};
    std::thread thread_;
};

// Replays the log through the normal command path, then starts appending to it. The write log is
// attached only afterwards, so replayed commands are not written back to the file.
std::unique_ptr<AofWriter> restore_from_aof(const Config& config, Store& store,
                                            CommandDispatcher& dispatcher) {
    const AofLoadResult loaded = load_aof(config.aof_path, [&dispatcher](const Command& command) {
        const Reply reply = dispatcher.execute(command);
        if (const auto* failure = std::get_if<ErrorReply>(&reply)) {
            throw std::runtime_error("AOF replay failed: " + failure->message);
        }
    });
    std::cout << "Loaded " << loaded.commands << " commands from " << config.aof_path << '\n';
    if (loaded.truncated_bytes > 0) {
        std::cout << "Truncated " << loaded.truncated_bytes
                  << " bytes of incomplete trailing record (likely an interrupted write)\n";
    }

    auto writer = std::make_unique<AofWriter>(config.aof_path, config.fsync);
    store.set_write_log(writer.get());
    return writer;
}

int run(const Config& config) {
    const sigset_t shutdown_signals = block_shutdown_signals();

    StoreOptions store_options;
    store_options.max_keys = config.max_keys;
    Store store(store_options);
    CommandDispatcher dispatcher(store);
    // Declared before the threads that write to the store so it is destroyed after they stop.
    const std::unique_ptr<AofWriter> aof =
        config.aof_path.empty() ? nullptr : restore_from_aof(config, store, dispatcher);
    const ExpirySweeper sweeper(store);
    Server server(config.server, dispatcher);
    const SignalWaiter signal_waiter(shutdown_signals, [&server] { server.stop(); });

    std::cout << "MiniRedis listening on " << config.server.bind_address << ':' << server.port()
              << '\n'
              << std::flush;
    server.run();
    std::cout << "MiniRedis shut down" << '\n' << std::flush;
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const std::vector<std::string> args(argv + 1, argv + argc);
        for (const auto& arg : args) {
            if (arg == "-h" || arg == "--help") {
                std::cout << usage();
                return 0;
            }
        }
        return run(parse_config(args));
    } catch (const std::invalid_argument& e) {
        std::cerr << "miniredis: " << e.what() << "\n\n" << usage();
        return 2;
    } catch (const std::exception& e) {
        std::cerr << "miniredis: " << e.what() << '\n';
        return 1;
    }
}
