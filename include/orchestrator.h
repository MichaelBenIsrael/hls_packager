#pragma once
#include "i_encoder.h"
#include "i_playlist_writer.h"
#include <string>
#include <vector>

namespace hls {

struct JobResult {
    bool success = false;
    std::vector<std::string> errors;
    std::vector<std::string> succeeded_renditions;
    std::vector<std::string> failed_renditions;
};

// Coordinates encoding all renditions in parallel, then builds the master +
// media playlists from whatever succeeded. Depends only on IEncoder and
// IPlaylistWriter -- never on FfmpegEncoder/HlsPlaylistWriter directly, so
// both can be swapped (unit tests, a live pipeline) without touching this
// class.
class Orchestrator {
public:
    Orchestrator(IEncoder& encoder, IPlaylistWriter& playlist_writer,
                std::vector<RenditionSpec> ladder, int min_required_renditions = 1);

    JobResult Run(const std::string& input_path, const std::string& output_dir);

private:
    IEncoder& encoder_;
    IPlaylistWriter& playlist_writer_;
    std::vector<RenditionSpec> ladder_;
    int min_required_renditions_;
};

} // namespace hls