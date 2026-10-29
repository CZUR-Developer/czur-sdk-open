// Copyright (c) 2026 CZUR Tech. All rights reserved.

#include "image_write_utils.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <unistd.h>
#include <vector>

#include "opencv2/imgcodecs.hpp"
#include "opencv2/imgproc.hpp"

namespace {

struct ScenarioResult {
    long long old_avg_ms;
    long long new_avg_ms;
    std::vector<long long> old_runs_ms;
    std::vector<long long> new_runs_ms;
    std::size_t old_bytes;
    std::size_t new_bytes;
};

struct SampleConfig {
    std::string label;
    int width;
    int height;
    int quality;
    std::size_t target_min_bytes;
    std::size_t target_max_bytes;
};

struct PreparedSample {
    std::string label;
    int width;
    int height;
    std::vector<unsigned char> jpeg_bytes;
};

long long ElapsedMs(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now() - start)
        .count();
}

std::string TempPath(const std::string& name, int index) {
    std::ostringstream stream;
    stream << "/tmp/" << name << "-" << ::getpid() << "-" << index << ".jpg";
    return stream.str();
}

bool WriteBytes(const std::string& path, const std::vector<unsigned char>& bytes) {
    std::ofstream out(path.c_str(), std::ios::binary | std::ios::trunc);
    if (!out.good()) {
        return false;
    }
    if (!bytes.empty()) {
        out.write(reinterpret_cast<const char*>(&bytes[0]), static_cast<std::streamsize>(bytes.size()));
    }
    return out.good();
}

std::vector<unsigned char> ReadBytes(const std::string& path) {
    std::ifstream input(path.c_str(), std::ios::binary);
    if (!input.good()) {
        return std::vector<unsigned char>();
    }
    return std::vector<unsigned char>(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

unsigned short ReadBigEndian16(const std::vector<unsigned char>& data, std::size_t pos) {
    return static_cast<unsigned short>((static_cast<unsigned short>(data[pos]) << 8) |
                                       static_cast<unsigned short>(data[pos + 1]));
}

bool HasJfifDensity(const std::vector<unsigned char>& data, int expected_dpi) {
    if (data.size() < 20 || data[0] != 0xFF || data[1] != 0xD8) {
        return false;
    }
    std::size_t pos = 2;
    while (pos + 4 <= data.size()) {
        if (data[pos] != 0xFF) {
            break;
        }
        const unsigned char marker = data[pos + 1];
        if (marker == 0xD9 || marker == 0xDA) {
            break;
        }
        const unsigned short length = ReadBigEndian16(data, pos + 2);
        if (length < 2 || pos + 2 + length > data.size()) {
            break;
        }
        if (marker == 0xE0 && length >= 14 &&
            data[pos + 4] == 'J' && data[pos + 5] == 'F' && data[pos + 6] == 'I' &&
            data[pos + 7] == 'F' && data[pos + 8] == 0x00) {
            const unsigned char units = data[pos + 11];
            const int x_density = static_cast<int>(ReadBigEndian16(data, pos + 12));
            const int y_density = static_cast<int>(ReadBigEndian16(data, pos + 14));
            return units == 1 && x_density == expected_dpi && y_density == expected_dpi;
        }
        pos += 2 + length;
    }
    return false;
}

PreparedSample BuildSample(const SampleConfig& config) {
    cv::Mat image(config.height, config.width, CV_8UC3);
    for (int y = 0; y < image.rows; ++y) {
        cv::Vec3b* row = image.ptr<cv::Vec3b>(y);
        for (int x = 0; x < image.cols; ++x) {
            row[x][0] = static_cast<unsigned char>((x * 17 + y * 11) % 256);
            row[x][1] = static_cast<unsigned char>((x * 7 + y * 19) % 256);
            row[x][2] = static_cast<unsigned char>((x * 13 + y * 5) % 256);
        }
    }

    std::vector<unsigned char> encoded;
    std::vector<int> params;
    params.push_back(cv::IMWRITE_JPEG_QUALITY);
    params.push_back(config.quality);
    if (!cv::imencode(".jpg", image, encoded, params)) {
        return PreparedSample();
    }

    PreparedSample sample;
    sample.label = config.label;
    sample.width = config.width;
    sample.height = config.height;
    sample.jpeg_bytes = encoded;
    return sample;
}

long long RunOldScenario(const PreparedSample& sample, int dpi, int run_index, std::size_t* output_size) {
    const std::string input_path = TempPath("sdk-private-image-write-old-input", run_index);
    const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();

    if (!WriteBytes(input_path, sample.jpeg_bytes)) {
        return -1;
    }
    if (!editor::sdk::private_impl::WriteDpiToExistingImage(input_path, sample.width, sample.height, dpi)) {
        std::remove(input_path.c_str());
        return -1;
    }
    const std::vector<unsigned char> output = ReadBytes(input_path);
    if (!HasJfifDensity(output, dpi)) {
        std::remove(input_path.c_str());
        return -1;
    }
    if (output_size != NULL) {
        *output_size = output.size();
    }
    std::remove(input_path.c_str());
    return ElapsedMs(start);
}

long long RunNewScenario(const PreparedSample& sample, int dpi, int run_index, std::size_t* output_size) {
    const std::string output_path = TempPath("sdk-private-image-write-new-output", run_index);
    const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();

    if (!editor::sdk::private_impl::WriteJpegBytesWithDpi(
            output_path, sample.jpeg_bytes, sample.width, sample.height, dpi)) {
        return -1;
    }
    const std::vector<unsigned char> output = ReadBytes(output_path);
    if (!HasJfifDensity(output, dpi)) {
        std::remove(output_path.c_str());
        return -1;
    }
    if (output_size != NULL) {
        *output_size = output.size();
    }
    std::remove(output_path.c_str());
    return ElapsedMs(start);
}

ScenarioResult BenchmarkScenario(const PreparedSample& sample, int dpi, int runs) {
    ScenarioResult result;
    result.old_avg_ms = 0;
    result.new_avg_ms = 0;
    result.old_bytes = 0;
    result.new_bytes = 0;

    for (int i = 0; i < runs; ++i) {
        std::size_t output_size = 0;
        const long long elapsed = RunOldScenario(sample, dpi, i, &output_size);
        if (elapsed < 0) {
            result.old_avg_ms = -1;
            return result;
        }
        result.old_runs_ms.push_back(elapsed);
        result.old_avg_ms += elapsed;
        result.old_bytes = output_size;
    }
    result.old_avg_ms /= runs;

    for (int i = 0; i < runs; ++i) {
        std::size_t output_size = 0;
        const long long elapsed = RunNewScenario(sample, dpi, i, &output_size);
        if (elapsed < 0) {
            result.new_avg_ms = -1;
            return result;
        }
        result.new_runs_ms.push_back(elapsed);
        result.new_avg_ms += elapsed;
        result.new_bytes = output_size;
    }
    result.new_avg_ms /= runs;
    return result;
}

std::string FormatRuns(const std::vector<long long>& runs) {
    std::ostringstream stream;
    for (std::size_t i = 0; i < runs.size(); ++i) {
        if (i != 0) {
            stream << ",";
        }
        stream << runs[i];
    }
    return stream.str();
}

} // namespace

int main() {
    const int runs = 10;
    const int dpi = 300;
    const SampleConfig configs[] = {
        {"medium", 3200, 2400, 92, 2U * 1024U * 1024U, 8U * 1024U * 1024U},
        {"large", 5200, 3900, 95, 15U * 1024U * 1024U, 40U * 1024U * 1024U},
    };

    bool success = true;
    for (const SampleConfig& config : configs) {
        const PreparedSample sample = BuildSample(config);
        if (sample.jpeg_bytes.empty()) {
            std::cerr << "failed to build sample: " << config.label << std::endl;
            return 1;
        }
        if (sample.jpeg_bytes.size() < config.target_min_bytes || sample.jpeg_bytes.size() > config.target_max_bytes) {
            std::cerr << "sample size out of expected range: label=" << config.label
                      << " bytes=" << sample.jpeg_bytes.size() << std::endl;
        }

        const ScenarioResult result = BenchmarkScenario(sample, dpi, runs);
        if (result.old_avg_ms < 0 || result.new_avg_ms < 0) {
            std::cerr << "benchmark scenario failed: " << config.label << std::endl;
            return 1;
        }

        const double ratio = result.old_avg_ms > 0
                                 ? static_cast<double>(result.new_avg_ms) / static_cast<double>(result.old_avg_ms)
                                 : 0.0;
        std::cout << "scenario=" << config.label
                  << " input_bytes=" << sample.jpeg_bytes.size()
                  << " old_avg_ms=" << result.old_avg_ms
                  << " new_avg_ms=" << result.new_avg_ms
                  << " ratio=" << std::fixed << std::setprecision(3) << ratio
                  << " old_output_bytes=" << result.old_bytes
                  << " new_output_bytes=" << result.new_bytes
                  << " old_runs_ms=[" << FormatRuns(result.old_runs_ms) << "]"
                  << " new_runs_ms=[" << FormatRuns(result.new_runs_ms) << "]"
                  << std::endl;

        if (result.new_avg_ms > result.old_avg_ms) {
            success = false;
        }
    }

    return success ? 0 : 1;
}
