#pragma once

#include "protocol/command.h"

namespace miniredis {

// Receives every mutation as the command that reproduces it, in the exact order the store
// applies them. Called with the store lock held, so implementations need no locking of their own.
class WriteLog {
public:
    WriteLog() = default;
    virtual ~WriteLog() = default;

    WriteLog(const WriteLog&) = delete;
    WriteLog& operator=(const WriteLog&) = delete;
    WriteLog(WriteLog&&) = delete;
    WriteLog& operator=(WriteLog&&) = delete;

    virtual void append(const Command& command) = 0;
};

}  // namespace miniredis
