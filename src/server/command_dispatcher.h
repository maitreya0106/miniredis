#pragma once

#include "protocol/command.h"
#include "protocol/reply.h"
#include "store/store.h"

namespace miniredis {

class CommandDispatcher {
public:
    explicit CommandDispatcher(Store& store) : store_(store) {}

    Reply execute(const Command& command);

private:
    Store& store_;
};

}  // namespace miniredis
