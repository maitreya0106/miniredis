#pragma once

#include <string>

#include "server/command_dispatcher.h"

namespace miniredis {

// Does not own the socket: the server must stop tracking an fd before it is closed and reused.
class Connection {
public:
    Connection(int socket, CommandDispatcher& dispatcher)
        : socket_(socket), dispatcher_(dispatcher) {}

    // Blocks until the client disconnects or sends a malformed request.
    void serve();

private:
    // Executes every complete request in the buffer; returns false if the connection must close.
    bool process_input();

    int socket_;
    CommandDispatcher& dispatcher_;
    std::string input_;
    std::string output_;
};

}  // namespace miniredis
