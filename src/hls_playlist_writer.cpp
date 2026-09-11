#include "hls_playlist_writer.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <stdexcept>

namespace hls {

void HlsPlaylistWriter::WriteMediaPlaylist(const std::string& output_path,
                                           const EncodeResult& result) {
    if (!result.success || result.segments.empty()) {
        throw std::invalid_argument(
            "WriteMediaPlaylist called with a failed/empty EncodeResult for '" +
            result.rendition_name + "'");
    }

    double max_duration = 0.0;
    for (const auto& seg : result.segments) {
        max_duration = std::max(max_duration, seg.duration_seconds);
    }
    int target_duration = static_cast<int>(std::ceil(max_duration));

    std::ofstream out(output_path);
    if (!out) throw std::runtime_error("Failed to open " + output_path);

    out << "#EXTM3U\n";
    out << "#EXT-X-VERSION:3\n";
    out << "#EXT-X-TARGETDURATION:" << target_duration << "\n";
    out << "#EXT-X-MEDIA-SEQUENCE:0\n";
    out << "#EXT-X-PLAYLIST-TYPE:VOD\n";

    for (const auto& seg : result.segments) {
        out << "#EXTINF:" << seg.duration_seconds << ",\n";
        out << seg.filename << "\n";
    }
    out << "#EXT-X-ENDLIST\n";
}

void HlsPlaylistWriter::WriteMasterPlaylist(const std::string& output_path,
                                            const std::vector<MasterRenditionEntry>& renditions) {
    std::ofstream out(output_path);
    if (!out) throw std::runtime_error("Failed to open " + output_path);

    out << "#EXTM3U\n";
    out << "#EXT-X-VERSION:3\n";

    for (const auto& r : renditions) {
        out << "#EXT-X-STREAM-INF:BANDWIDTH=" << r.bandwidth_bps
            << ",RESOLUTION=" << r.width << "x" << r.height << "\n";
        out << r.relative_playlist_path << "\n";
    }
}

} // namespace hls