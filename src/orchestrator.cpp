#include "orchestrator.h"
#include <algorithm>
#include <filesystem>
#include <future>
#include <iostream>

namespace fs = std::filesystem;

namespace hls {

Orchestrator::Orchestrator(IEncoder& encoder, IPlaylistWriter& playlist_writer,
                           std::vector<RenditionSpec> ladder, int min_required_renditions)
    : encoder_(encoder), playlist_writer_(playlist_writer),
      ladder_(std::move(ladder)), min_required_renditions_(min_required_renditions) {}

JobResult Orchestrator::Run(const std::string& input_path, const std::string& output_dir) {
    JobResult job_result;
    fs::create_directories(output_dir);

    // Fan out: std::async is sufficient because job count == ladder size
    // (small, fixed), each task is independent, and we want results via
    // futures anyway. A hand-rolled thread pool would add code without
    // adding capability at THIS scale (see README Q3 for concurrent
    // *packaging jobs*, which is a different axis of scale).
    std::vector<std::future<EncodeResult>> futures;
    for (const auto& spec : ladder_) {
        futures.push_back(std::async(std::launch::async,
            [this, &input_path, &output_dir, spec]() {
                return encoder_.Encode(input_path, output_dir + "/" + spec.name, spec);
            }));
    }

    std::vector<EncodeResult> results;
    for (auto& f : futures) results.push_back(f.get());

    // Strategy: continue-and-omit, gated by a minimum-ladder-integrity
    // check. Every rendition gets a chance to finish independently; a
    // failure never blocks its siblings. But the master playlist is only
    // written if enough renditions survived to form a usable ABR ladder --
    // see README Q2/Q4.
    std::vector<MasterRenditionEntry> master_entries;
    for (const auto& result : results) {
        if (!result.success) {
            job_result.failed_renditions.push_back(result.rendition_name);
            job_result.errors.push_back(result.rendition_name + ": " + result.error_message);
            std::cerr << "[WARN] rendition '" << result.rendition_name
                      << "' failed: " << result.error_message << "\n";
            continue;
        }

        std::string rendition_dir = output_dir + "/" + result.rendition_name;
        playlist_writer_.WriteMediaPlaylist(rendition_dir + "/index.m3u8", result);
        job_result.succeeded_renditions.push_back(result.rendition_name);

        auto spec_it = std::find_if(ladder_.begin(), ladder_.end(),
            [&](const RenditionSpec& s) { return s.name == result.rendition_name; });

        MasterRenditionEntry entry;
        entry.name = result.rendition_name;
        entry.relative_playlist_path = result.rendition_name + "/index.m3u8";
        entry.width = spec_it->width;
        entry.height = spec_it->height;
        // Configured bitrate + 5% container/mux overhead margin as a
        // BANDWIDTH estimate. A production system would measure actual
        // peak bitrate via ffprobe instead -- see README improvements.
        entry.bandwidth_bps = static_cast<int>(
            (spec_it->video_bitrate_kbps + spec_it->audio_bitrate_kbps) * 1000 * 1.05);
        master_entries.push_back(entry);
    }

    if (static_cast<int>(master_entries.size()) < min_required_renditions_) {
        job_result.success = false;
        job_result.errors.push_back(
            "Ladder integrity violated: only " + std::to_string(master_entries.size()) +
            " of " + std::to_string(ladder_.size()) + " renditions succeeded (minimum " +
            std::to_string(min_required_renditions_) + "). Master playlist NOT written.");
        return job_result;
    }

    playlist_writer_.WriteMasterPlaylist(output_dir + "/master.m3u8", master_entries);
    job_result.success = true;
    return job_result;
}

} // namespace hls