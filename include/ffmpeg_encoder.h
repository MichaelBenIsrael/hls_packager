#pragma once
#include "i_encoder.h"

namespace hls {

// IEncoder implementation that shells out to `ffmpeg`/`ffprobe` on PATH.
// Deliberately uses the `segment` muxer, not the `hls` muxer -- ffmpeg only
// produces raw .ts files here. Playlist generation stays entirely owned by
// IPlaylistWriter, even though ffmpeg is technically capable of both.
class FfmpegEncoder : public IEncoder {
public:
    explicit FfmpegEncoder(int segment_duration_seconds = 6);

    EncodeResult Encode(const std::string& input_path,
                        const std::string& output_dir,
                        const RenditionSpec& spec) override;

private:
    int segment_duration_seconds_;

    double ProbeFrameRate(const std::string& input_path) const;
    std::string BuildFfmpegCommand(const std::string& input_path,
                                   const std::string& output_dir,
                                   const RenditionSpec& spec) const;
    std::vector<SegmentInfo> ProbeSegments(const std::string& output_dir) const;
};

} // namespace hls