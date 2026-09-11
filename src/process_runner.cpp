#include "process_runner.h"
#include <array>
#include <cstdio>
#include <stdexcept>

#ifdef _WIN32
#define POPEN _popen
#define PCLOSE _pclose
#else
#include <sys/wait.h>
#define POPEN popen
#define PCLOSE pclose
#endif

namespace hls {

ProcessResult RunProcess(const std::string& command) {
    // Redirect stderr into stdout so we capture ffmpeg's logging too --
    // ffmpeg writes almost everything to stderr by default.
    std::string full_command = command + " 2>&1";

    std::array<char, 4096> buffer{};
    ProcessResult result;

    FILE* pipe = POPEN(full_command.c_str(), "r");
    if (!pipe) {
        throw std::runtime_error("Failed to launch process: " + command);
    }

    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
        result.output += buffer.data();
    }

    int status = PCLOSE(pipe);
#ifdef _WIN32
    result.exit_code = status;
#else
    result.exit_code = WEXITSTATUS(status);
#endif
    return result;
}

} // namespace hls