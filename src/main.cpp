#include "ffmpeg_encoder.h"
#include "hls_playlist_writer.h"
#include "orchestrator.h"

#include <iostream>
#include <string>
#include <vector>

namespace {

struct CliArgs {
    std::string input;
    std::string output;
};

bool ParseArgs(int argc, char** argv, CliArgs& out_args) {
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--input" && i + 1 < argc) out_args.input = argv[++i];
        else if (arg == "--output" && i + 1 < argc) out_args.output = argv[++i];
    }
    return !out_args.input.empty() && !out_args.output.empty();
}

} // namespace

int main(int argc, char** argv) {
    CliArgs args;
    if (!ParseArgs(argc, argv, args)) {
        std::cerr << "Usage: hls_packager --input <source_video> --output <output_dir>\n";
        return 1;
    }

    // Fixed per the assignment spec. In production this would come from a
    // config file/flag -- see README improvements.
    std::vector<hls::RenditionSpec> ladder = {
        {"high",   1920, 1080, 4000, 128},
        {"medium", 1280, 720,  2000, 128},
        {"low",    640,  360,  800,  96},
    };

    hls::FfmpegEncoder encoder;
    hls::HlsPlaylistWriter playlist_writer;
    // Require at least 2 of 3 renditions for a usable ABR ladder.
    hls::Orchestrator orchestrator(encoder, playlist_writer, ladder, /*min_required_renditions=*/2);

    hls::JobResult result = orchestrator.Run(args.input, args.output);

    for (const auto& name : result.succeeded_renditions) std::cout << "[OK]   " << name << "\n";
    for (const auto& err : result.errors) std::cerr << "[FAIL] " << err << "\n";

    if (!result.success) {
        std::cerr << "Packaging job failed.\n";
        return 1;
    }
    std::cout << "Packaging complete: " << args.output << "/master.m3u8\n";
    return 0;
}