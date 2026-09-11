#pragma once
#include <string>
#include <vector>

namespace hls {

// Describes one output rendition in the ABR ladder.
struct RenditionSpec {
    std::string name;          // e.g. "high" -- also the output subfolder name
    int width = 0;
    int height = 0;
    int video_bitrate_kbps = 0;
    int audio_bitrate_kbps = 0;
};

// One physical segment file produced by an encoder.
struct SegmentInfo {
    std::string filename;       // e.g. "seg000.ts" (relative to rendition dir)
    double duration_seconds = 0.0;
};

// Result of encoding+segmenting a single rendition. This is the contract
// both the orchestration layer and the playlist layer depend on -- neither
// cares HOW the segments were produced.
struct EncodeResult {
    std::string rendition_name;
    bool success = false;
    std::string error_message;          // populated when success == false
    std::vector<SegmentInfo> segments;  // populated when success == true
};

// Abstraction over "turn a source video into a segmented rendition".
// A second implementation (e.g. an RTSP-fed live encoder, or a hardware
// encoder wrapper) only needs to satisfy this contract to be a drop-in
// replacement for the orchestrator and CLI.
class IEncoder {
public:
    virtual ~IEncoder() = default;

    // Must not throw for expected failures (bad input, ffmpeg non-zero
    // exit) -- those are reported via EncodeResult::success.
    virtual EncodeResult Encode(const std::string& input_path,
                                const std::string& output_dir,
                                const RenditionSpec& spec) = 0;
};

} // namespace hls