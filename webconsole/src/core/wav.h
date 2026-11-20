// Minimal RIFF/WAVE reader for sample metadata and waveform peaks. Bounded,
// no decoding of compressed formats; unsupported layouts report
// `supported=false` instead of guessing.
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "../util/common.h"

namespace dm {

struct WavInfo {
    bool valid = false;        // is a RIFF/WAVE file with fmt+data
    bool supported = false;    // PCM/float layout we can read for peaks
    std::uint16_t format = 0, channels = 0, bits = 0;
    std::uint32_t sample_rate = 0;
    std::uint64_t data_offset = 0, data_bytes = 0, frames = 0;
    double seconds = 0;
    std::string note;
    json to_json() const;
};

WavInfo wav_probe(int fd);
// Fills `peaks` with `buckets` pairs (min,max) in [-1,1] of the mono mix.
Status wav_peaks(int fd, const WavInfo& info, std::size_t buckets, std::vector<float>& peaks,
                 std::uint64_t max_bytes_to_read = 256ull << 20);

} // namespace dm
