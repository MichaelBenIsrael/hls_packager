#pragma once
#include "i_playlist_writer.h"

namespace hls {

// Writes RFC 8216-compliant VOD media and master playlists.
class HlsPlaylistWriter : public IPlaylistWriter {
public:
    void WriteMediaPlaylist(const std::string& output_path,
                            const EncodeResult& result) override;
    void WriteMasterPlaylist(const std::string& output_path,
                             const std::vector<MasterRenditionEntry>& renditions) override;
};

} // namespace hls