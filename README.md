# HLS ABR Packager

A command-line C++17 tool that transcodes a source video into a 3-rendition
HLS ABR ladder (1080p/720p/360p), segments each into 6-second `.ts` chunks
on keyframe boundaries, and generates RFC 8216-compliant media and master
playlists.

## Build Instructions

**Prerequisites:** CMake ≥ 3.16, a C++17 compiler, `ffmpeg`/`ffprobe` on PATH.

```powershell
# Windows (Developer PowerShell for VS 2022)
cmake -B build -G Ninja
cmake --build build
```

```bash
# Linux / macOS
cmake -B build
cmake --build build
```

No third-party libraries are used beyond the C++ standard library and the
system `Threads` package (for `std::thread`, `std::async`). `ffmpeg` and
`ffprobe` are invoked as subprocesses, not linked against — this was done
on purpose to keep the build trivial to reproduce and to keep the assigment
more about what I actually wrote and less about what ffmpeg do.

## Run Instructions

```bash
./hls_packager --input <source_video> --output <output_dir>
```

Example:
```bash
./hls_packager --input test_clip.mp4 --output output
```

Produces:
```
output/
├── master.m3u8
├── high/    (1920x1080, 4000kbps video / 128kbps audio)
│   ├── index.m3u8
│   └── seg000.ts, seg001.ts, ...
├── medium/  (1280x720, 2000kbps / 128kbps)
└── low/     (640x360, 800kbps / 96kbps)
```

## Architecture

```
Orchestrator  ──depends on──>  IEncoder            (encoding abstraction)
              ──depends on──>  IPlaylistWriter      (playlist abstraction)

FfmpegEncoder      : IEncoder        (shells out to ffmpeg/ffprobe)
HlsPlaylistWriter  : IPlaylistWriter (writes RFC 8216 .m3u8 text)
```

- **`IEncoder`** — contract: source + `RenditionSpec` in, segmented `.ts`
  files + per-segment durations out (`EncodeResult`), or a structured
  failure. No knowledge of playlists or of other renditions.
- **`IPlaylistWriter`** — pure function of metadata → `.m3u8` text. No
  knowledge of ffmpeg, subprocesses, or encoding. Trivially unit-testable.
- **`Orchestrator`** — owns the job lifecycle: fans out one `IEncoder::Encode`
  call per rendition in parallel, collects results, decides what to do about
  partial failure, and drives `IPlaylistWriter` with whatever succeeded.

Both interfaces are consumed by `Orchestrator` only through their abstract
base — a second implementation of either (a live RTSP encoder, a DASH MPD
writer) is a drop-in replacement with zero changes to `Orchestrator` or
`main.cpp`.

## Design Questions

### 1. Extensibility — live RTSP input

**What stays the same:** `IEncoder` and `IPlaylistWriter` as contracts,
and `Orchestrator`'s core loop of "fan out, collect, write what succeeded."

**What changes:**
- A new `RtspLiveEncoder : IEncoder` replaces `FfmpegEncoder` for live
  sources. Its `Encode()` wouldn't run to completion and return once — it
  would need to emit segments incrementally as they become available. This
  actually means `IEncoder`'s contract needs to grow a streaming variant
  (e.g. a callback or an async iterator interface) rather than the current
  single blocking `EncodeResult` return, since "encode everything, then
  return" doesn't fit a source with no natural end.
- `IPlaylistWriter` needs a live mode: `#EXT-X-PLAYLIST-TYPE:EVENT` (or no
  type tag at all for a sliding window), an incrementing
  `#EXT-X-MEDIA-SEQUENCE` as old segments roll off, and no
  `#EXT-X-ENDLIST` until the stream actually ends. `WriteMediaPlaylist`
  would need to *append/rewrite* rather than write-once.
- `Orchestrator::Run` currently returns once after one encode pass; a live
  variant would loop, re-invoking playlist writes on a timer or per-segment
  event, and would need a stop/shutdown signal.
- The CLI would need a `--live` / `--duration` style flag, since "run until
  the RTSP source disconnects" is a different operational mode than "run
  once on a file."

The reason this decomposition holds up under that change is that the
*boundary* was drawn at "how do you get bytes" vs "how do you describe
them as HLS," not at "file" vs "stream" — so swapping the source doesn't
ripple into playlist logic, and extending the playlist writer for live
semantics doesn't ripple into how any given rendition gets its bits.

### 2. Error handling — one rendition fails mid-job

**Strategy implemented: continue-and-omit, gated by a minimum-ladder check.**

Each rendition is encoded independently via its own `std::async` task
(`Orchestrator::Run`). A failure in one never touches the others — they're
separate `ffmpeg` subprocesses with no shared mutable state. When
collecting results, a failed rendition is recorded in `JobResult` and
simply excluded from the master playlist, rather than aborting siblings
that may already be mid-encode or already succeeded.

**Trade-offs considered:**
- *Abort-all-on-first-failure* is simpler to reason about, but throws away
  completed/in-flight work on healthy renditions for a problem that's
  often isolated to one rendition (e.g. a resolution-specific encoder
  parameter issue) — and produces zero usable output even when 2 of 3
  renditions would have been fine.
- *Continue-and-include-partial-ladder-unconditionally* maximizes output
  but risks silently shipping something worse than no output at all: a
  master playlist is a promise to the player about what's available, and
  degrading that promise (see Q4) without any check is a availability bug
  disguised as resilience.
- What's implemented is deliberately in between: continue independently,
  but gate the *aggregate* decision (write master.m3u8 or not) behind an
  explicit `min_required_renditions_` threshold (defaults to 2 of 3 in
  `main.cpp`). This is checked once, after all results are in, in
  `Orchestrator::Run`.

### 3. Scale — 50 concurrent packaging jobs

**What breaks first:** thread/process oversubscription. Each job currently
spawns one OS thread per rendition via unbounded `std::async` — 3 threads
per job. At 50 concurrent jobs that's 150 concurrently-launched `ffmpeg`
subprocesses with no ceiling, all competing for CPU, memory bandwidth, and
disk I/O. `std::async(std::launch::async)` has no built-in concurrency cap;
the OS scheduler will thrash rather than queue gracefully. There's also no
job queue at all right now — `main()` runs one job synchronously to
completion; "50 concurrent jobs" isn't even expressible in the current CLI
shape.

**What I'd change:**
- Replace unbounded `std::async` with a **bounded thread/worker pool** sized
  to actual hardware concurrency (e.g. `std::thread::hardware_concurrency()`),
  with a work queue in front of it so excess encode tasks wait rather than
  all launching at once.
- Introduce a **job queue** above `Orchestrator` (in-memory queue for a
  single process, or a real message queue for a distributed deployment) so
  "50 jobs" becomes "50 items in a queue, N workers draining it," not "50
  simultaneous process trees."
- Add a **concurrency cap on live `ffmpeg` subprocesses specifically**
  (e.g. a counting semaphore), since CPU-bound encoding is the actual
  scarce resource, not thread count per se.
- Move from "one CLI invocation = one job" to a small **service/API layer**
  with persistent job state (status, retries, results) — 50 concurrent jobs
  implies something is tracking them, which a stateless CLI process can't
  do across restarts.
- At that point, horizontal scaling (multiple machines, each running a
  bounded pool, pulling from a shared queue) becomes the natural next step
  rather than trying to push one machine further.

### 4. ABR ladder integrity — medium rendition fails

This is handled at two layers, deliberately not left to the player:

- **Orchestration layer:** `Orchestrator::Run` will *not* write
  `master.m3u8` at all if fewer than `min_required_renditions_` renditions
  succeeded (currently 2 of 3). This prevents ever publishing a master
  playlist that references a rendition directory/playlist that doesn't
  exist — a dangling `#EXT-X-STREAM-INF` entry is worse for a player than
  a missing one, since it looks like a valid option until the player
  actually tries to fetch it mid-playback and fails.
- **Playlist layer:** `IPlaylistWriter::WriteMasterPlaylist` only ever
  receives `MasterRenditionEntry` objects for renditions that already
  succeeded (`Orchestrator` builds that list from successful results
  only) — so the playlist writer itself has no way to accidentally
  reference a broken rendition, even if orchestration logic changes later.
- **Caller layer:** `JobResult` exposes `succeeded_renditions`,
  `failed_renditions`, and `errors` explicitly, so whatever calls
  `Orchestrator::Run` (currently `main.cpp`, later a job service) can
  decide what to do with a degraded ladder — alert on-call, retry just the
  failed rendition without re-encoding the others, or accept a 2-rendition
  ladder as good enough to publish. That decision is a business/product
  call, not something the packaging pipeline should make silently either
  way.

What I did *not* implement, and would with more time: today, "medium
missing" produces a valid 2-rung ladder (high/low) with a big bitrate gap
between them, which can still cause poor initial ABR decisions even though
it's technically playable. A more complete fix would have
`WriteMasterPlaylist` (or the orchestrator building its input) flag a
"large gap between adjacent BANDWIDTH values" condition explicitly in the
`JobResult` so the caller knows the ladder is *technically valid but
qualitatively degraded*, not just pass/fail.

## Concurrency (Part 3)

`std::async(std::launch::async)` — one task per rendition, `.get()`-ed
after launch. Chosen over a hand-rolled thread pool because, at this
scale, job size is small and fixed (exactly `ladder_.size()` tasks per
job), each task is fully independent, and `std::future` gives result
propagation (including exceptions) for free. A thread pool would add code
without adding capability *at this scale* — see Q3 above for where that
trade-off flips once concurrent *job* count (not renditions per job)
becomes the bottleneck.

## Debugging Journey

Four real bugs surfaced while getting a clean end-to-end run, each with a
distinct root cause — kept here because the diagnosis process is arguably
more representative of real packaging-pipeline work than the happy path:

1. **`x264 [error]: main profile doesn't support 4:4:4`** — the test
   source's pixel format didn't match what H.264 Main profile supports.
   Fixed by explicitly normalizing to `yuv420p` in the filter chain rather
   than assuming input format — necessary in general, not just for the
   synthetic test source, since a real packager can't assume incoming
   uploads are already 4:2:0.
2. **Interleaved/corrupted debug log lines** (`-f segment` appearing as
   `-fsegment` in one log line) — traced to concurrent unsynchronized
   writes to `std::cerr` from parallel `std::async` tasks. Fixed with a
   `std::mutex` scoped tightly around just the log write, not the encode
   itself, preserving full parallelism.
3. **First segment double-length (12s instead of 6s)** — `-force_key_frames`
   with a timestamp-based expression raced against encoder lookahead/B-frame
   reordering under the `veryfast` preset, so the first requested keyframe
   didn't land where expected. Fixed by switching to deterministic
   frame-count-based GOP placement (`-g`/`-keyint_min`) and disabling
   B-frames (`-bf 0`) to remove reordering ambiguity entirely.
4. **Playback stutter/skip at segment boundaries** despite correct segment
   durations — traced to `-reset_timestamps 1` causing each `.ts` file to
   restart its PTS near zero independently, creating timestamp
   discontinuities between segments that some players (`ffplay` included)
   interpret as a seek/resync event rather than continuous playback. Fixed
   by removing `-reset_timestamps`, letting PTS run continuously across the
   whole encode since all segments originate from one continuous source
   pass — the standard approach for single-source VOD packaging.

## How I verified the output plays correctly

- Inspected each `index.m3u8` for correct `#EXTINF` values matching actual
  segment durations (cross-checked against `ffprobe`), and confirmed
  `master.m3u8` lists all three renditions with ascending `BANDWIDTH` and
  correct `RESOLUTION`.
- Played `master.m3u8` and each rendition's `index.m3u8` directly in VLC
  (Media → Open File) to confirm real decode/playback, not just
  well-formed playlist text.
- Used `ffplay` on individual rendition playlists as a stricter check,
  since it's less forgiving of stream discontinuities than VLC — this is
  what actually surfaced bug #4 above.
- Watched a visible on-screen counter in the synthetic test source across
  the full duration to confirm no frames were skipped or duplicated at
  segment boundaries.

## What I'd improve with more time

- Config-driven ladder (JSON/YAML) instead of the hardcoded `RenditionSpec`
  vector in `main.cpp`.
- Actual measured peak bitrate (via `ffprobe`) for `BANDWIDTH` instead of
  the configured-bitrate-plus-margin estimate currently used.
- Unit tests for `HlsPlaylistWriter` against fixed `EncodeResult` inputs
  (it's a pure function, so this is cheap) and a fake `IEncoder` for
  testing `Orchestrator`'s partial-failure logic without invoking real
  `ffmpeg`.
- The bounded worker pool / job queue described in Q3, plus a "large
  bitrate gap" flag described in Q4.
- Structured logging instead of `std::cerr` text (though the mutex fix
  makes the current approach at least correct under concurrency).

## Third-Party Libraries

None beyond the C++ standard library and `Threads::Threads` (CMake package
for `std::thread`/`std::async` support). `ffmpeg`/`ffprobe` are invoked as
external subprocesses, not linked.