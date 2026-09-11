#pragma once
#include <string>

namespace hls {

struct ProcessResult {
    int exit_code = -1;
    std::string output; // combined stdout+stderr
};

// Runs a shell command and captures its output. Throws only if the process
// could not be launched at all -- a non-zero exit code is a normal result.
ProcessResult RunProcess(const std::string& command);

} // namespace hls