#pragma once
#include <string>
#include <vector>
#include "i_encoder.h"

namespace hls {

// Metadata needed to describe one rendition's entry in the master playlist.
struct MasterRenditionEntry {
    std::string name;
    std::string relative_playlist_path; // e.g. "high/index.m3u8"
    int bandwidth_bps = 0;
    int width = 0;
    int height = 0;
};

// Abstraction over "turn segment/rendition metadata into .m3u8 text".
// Pure function of its inputs plus an output path -- no knowledge of
// ffmpeg, subprocesses, or anything beyond the path it's given. That's
// what makes it trivially unit-testable and swappable (e.g. for LL-HLS
// or a DASH MPD writer later).
class IPlaylistWriter {
public:
    virtual ~IPlaylistWriter() = default;

    virtual void WriteMediaPlaylist(const std::string& output_path,
                                     const EncodeResult& result) = 0;

    virtual void WriteMasterPlaylist(const std::string& output_path,
                                      const std::vector<MasterRenditionEntry>& renditions) = 0;
};

} // namespace hls