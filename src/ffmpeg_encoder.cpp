#include "ffmpeg_encoder.h"
#include "process_runner.h"

#include <algorithm>
#include <filesystem>
#include <sstream>
#include <iostream>
#include <mutex>
#include <cmath>


namespace fs = std::filesystem;

namespace {
    std::mutex g_log_mutex;
}

namespace hls {

FfmpegEncoder::FfmpegEncoder(int segment_duration_seconds)
    : segment_duration_seconds_(segment_duration_seconds) {}

std::string FfmpegEncoder::BuildFfmpegCommand(const std::string& input_path,
                                              const std::string& output_dir,
                                              const RenditionSpec& spec) const {
    double fps = ProbeFrameRate(input_path);
    int gop_frames = static_cast<int>(std::round(fps * segment_duration_seconds_));

    std::ostringstream cmd;
    cmd << "ffmpeg -y -i \"" << input_path << "\""
    << " -vf scale=" << spec.width << ":" << spec.height << ",format=yuv420p"
    << " -c:v libx264 -profile:v main -preset veryfast"
    << " -b:v " << spec.video_bitrate_kbps << "k"
    << " -maxrate " << spec.video_bitrate_kbps << "k"
    << " -bufsize " << (spec.video_bitrate_kbps * 2) << "k"
    << " -g " << gop_frames << " -keyint_min " << gop_frames
    << " -bf 0"
    << " -sc_threshold 0"
    << " -c:a aac -b:a " << spec.audio_bitrate_kbps << "k -ar 48000"
    << " -f segment -segment_time " << segment_duration_seconds_
    << " -segment_format mpegts"
    << " \"" << output_dir << "/seg%03d.ts\"";
    return cmd.str();
}

std::vector<SegmentInfo> FfmpegEncoder::ProbeSegments(const std::string& output_dir) const {
    std::vector<SegmentInfo> segments;
    std::vector<std::string> filenames;

    for (const auto& entry : fs::directory_iterator(output_dir)) {
        if (entry.path().extension() == ".ts") {
            filenames.push_back(entry.path().filename().string());
        }
    }
    std::sort(filenames.begin(), filenames.end());

    for (const auto& filename : filenames) {
        // The spec requires ACTUAL segment duration in #EXTINF, not the
        // nominal 6s target -- the last segment of any rendition is almost
        // always shorter. ffprobe gives us ground truth per file.
        std::string probe_cmd = "ffprobe -v error -show_entries format=duration "
                                 "-of csv=p=0 \"" + output_dir + "/" + filename + "\"";
        ProcessResult probe_result = RunProcess(probe_cmd);
        double duration = static_cast<double>(segment_duration_seconds_); // fallback
        try {
            duration = std::stod(probe_result.output);
        } catch (const std::exception&) {
            // keep fallback
        }
        segments.push_back(SegmentInfo{filename, duration});
    }
    return segments;
}

double FfmpegEncoder::ProbeFrameRate(const std::string& input_path) const {
    std::string probe_cmd = "ffprobe -v error -select_streams v:0 "
                             "-show_entries stream=r_frame_rate -of csv=p=0 \"" +
                             input_path + "\"";
    ProcessResult probe_result = RunProcess(probe_cmd);

    // r_frame_rate comes back as "num/den" e.g. "30/1" or "30000/1001"
    double fps = 30.0; // sane fallback
    auto slash_pos = probe_result.output.find('/');
    if (slash_pos != std::string::npos) {
        try {
            double num = std::stod(probe_result.output.substr(0, slash_pos));
            double den = std::stod(probe_result.output.substr(slash_pos + 1));
            if (den > 0.0) fps = num / den;
        } catch (const std::exception&) {
            // keep fallback
        }
    }
    return fps;
}

EncodeResult FfmpegEncoder::Encode(const std::string& input_path,
                                   const std::string& output_dir,
                                   const RenditionSpec& spec) {
    EncodeResult result;
    result.rendition_name = spec.name;

    fs::create_directories(output_dir);

    // [After Debug] Clean any stale segments from a previous run before encoding --
    // ProbeSegments() globs *.ts from this directory, ptherwise leftovers from an
    // earlier attempt (different segment count/boundaries) would silently
    // get included in the new playlist.
    for (const auto& entry : fs::directory_iterator(output_dir)) {
        if (entry.path().extension() == ".ts") {
            fs::remove(entry.path());
        }
    }

    std::string command = BuildFfmpegCommand(input_path, output_dir, spec);
    // Synchronized cerr writing
    {
        std::lock_guard<std::mutex> lock(g_log_mutex);
        std::cerr << "[DEBUG] " << command << "\n";
    }
    ProcessResult process_result = RunProcess(command);

    if (process_result.exit_code != 0) {
    result.success = false;
    // Keep the TAIL of ffmpeg's output, not the head -- the banner is
    // always ~400+ chars of boilerplate; the actual error line is near
    // the end.
    std::string tail = process_result.output.size() > 1500
        ? process_result.output.substr(process_result.output.size() - 1500)
        : process_result.output;
    result.error_message = "ffmpeg exited with code " +
        std::to_string(process_result.exit_code) + ": " + tail;
    return result;
}

    result.segments = ProbeSegments(output_dir);
    if (result.segments.empty()) {
        result.success = false;
        result.error_message = "ffmpeg reported success but produced no .ts segments";
        return result;
    }

    result.success = true;
    return result;
}

} // namespace hls