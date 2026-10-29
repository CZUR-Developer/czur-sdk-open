#include "image_write_utils.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <fstream>
#include <iterator>
#include <mutex>
#include <cstdio>
#include <cstdlib>
#include <sstream>

#if defined(__APPLE__)
#include <unistd.h>
#endif

#include "graphic/image_codec_lock.h"
#include "opencv2/imgcodecs.hpp"
#include "private_sdk_support.h"
#include "support/private_sdk_logger.h"
#include "tiffio.h"

#ifndef __APPLE__
#include <libexif/exif-data.h>
#endif

namespace editor {
namespace sdk {
namespace private_impl {

namespace {

std::string FileExtension(const std::string& path) {
    const std::string::size_type dot_pos = path.find_last_of('.');
    if (dot_pos == std::string::npos || dot_pos + 1 >= path.size()) {
        return "";
    }
    std::string extension = path.substr(dot_pos + 1);
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return extension;
}

bool IsJpegExtension(const std::string& extension) {
    return extension == "jpg" || extension == "jpeg";
}

bool IsPngExtension(const std::string& extension) {
    return extension == "png";
}

bool IsTiffExtension(const std::string& extension) {
    return extension == "tif" || extension == "tiff";
}

bool IsBmpExtension(const std::string& extension) {
    return extension == "bmp";
}

bool IsValidDpi(double dpi) {
    return dpi >= 10.0 && dpi <= 10000.0;
}

long long ElapsedMs(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now() - start)
        .count();
}

std::string JoinPath(const std::string& base, const std::string& leaf) {
    if (base.empty()) {
        return leaf;
    }
    if (leaf.empty()) {
        return base;
    }
    const char last = base[base.size() - 1];
    if (last == '/' || last == '\\') {
        return base + leaf;
    }
#if defined(_WIN32)
    return base + "\\" + leaf;
#else
    return base + "/" + leaf;
#endif
}

std::string TempJpegPath(const char* prefix) {
    std::string dir = JoinPath(JoinPath(GetSdkRuntimeDir(), "capture"), "tmp");
    if (!EnsureDirectoryRecursive(dir)) {
        dir.clear();
    }

    const char* temp_dir = NULL;
#if defined(_WIN32)
    const char* const temp_env_names[] = {"TEMP", "TMP"};
#else
    const char* const temp_env_names[] = {"TMPDIR", "TEMP", "TMP"};
#endif
    if (dir.empty()) {
        for (std::size_t i = 0; i < sizeof(temp_env_names) / sizeof(temp_env_names[0]); ++i) {
            const char* candidate = std::getenv(temp_env_names[i]);
            if (candidate != NULL && candidate[0] != '\0') {
                temp_dir = candidate;
                break;
            }
        }
    }

    if (dir.empty() && temp_dir != NULL) {
        dir = temp_dir;
    }
#if defined(__APPLE__)
    if (dir.empty()) {
        const std::size_t required_size = ::confstr(_CS_DARWIN_USER_TEMP_DIR, NULL, 0);
        if (required_size > 1) {
            std::vector<char> buffer(required_size, '\0');
            if (::confstr(_CS_DARWIN_USER_TEMP_DIR, &buffer[0], buffer.size()) > 0) {
                dir.assign(&buffer[0]);
            }
        }
    }
#endif
    if (dir.empty()) {
#if defined(_WIN32)
        dir = ".";
#else
        dir = "/tmp";
#endif
    }

    std::ostringstream path;
    path << dir;
    const char last = dir.empty() ? '\0' : dir[dir.size() - 1];
    if (last != '/' && last != '\\') {
#if defined(_WIN32)
        path << "\\";
#else
        path << "/";
#endif
    }
    path << prefix << "_" << static_cast<long long>(
                std::chrono::steady_clock::now().time_since_epoch().count())
         << "_" << static_cast<unsigned int>(std::rand()) << ".jpg";
    return path.str();
}

unsigned short ReadBigEndian16(const std::vector<unsigned char>& data, size_t pos) {
    return static_cast<unsigned short>((static_cast<unsigned short>(data[pos]) << 8) |
                                       static_cast<unsigned short>(data[pos + 1]));
}

std::uint32_t ReadBigEndian32(const std::vector<unsigned char>& data, size_t pos) {
    return (static_cast<std::uint32_t>(data[pos]) << 24) |
           (static_cast<std::uint32_t>(data[pos + 1]) << 16) |
           (static_cast<std::uint32_t>(data[pos + 2]) << 8) |
           static_cast<std::uint32_t>(data[pos + 3]);
}

std::uint32_t ReadLittleEndian32(const std::vector<unsigned char>& data, size_t pos) {
    return static_cast<std::uint32_t>(data[pos]) |
           (static_cast<std::uint32_t>(data[pos + 1]) << 8) |
           (static_cast<std::uint32_t>(data[pos + 2]) << 16) |
           (static_cast<std::uint32_t>(data[pos + 3]) << 24);
}

void WriteBigEndian16(std::vector<unsigned char>& data, size_t pos, unsigned short value) {
    data[pos] = static_cast<unsigned char>((value >> 8) & 0xFF);
    data[pos + 1] = static_cast<unsigned char>(value & 0xFF);
}

std::vector<unsigned char> ReadFileBytes(const std::string& path) {
    std::ifstream input(path.c_str(), std::ios::binary);
    if (!input) {
        return std::vector<unsigned char>();
    }
    return std::vector<unsigned char>(std::istreambuf_iterator<char>(input), {});
}

bool WriteFileBytesAtomically(const std::string& path, const std::vector<unsigned char>& bytes, const char* suffix) {
    if (path.empty() || bytes.empty()) {
        return false;
    }
    const std::string temp_path = path + suffix;
    std::ofstream output(temp_path.c_str(), std::ios::binary | std::ios::trunc);
    if (!output) {
        return false;
    }
    output.write(reinterpret_cast<const char*>(&bytes[0]), static_cast<std::streamsize>(bytes.size()));
    output.close();
    if (!output) {
        std::remove(temp_path.c_str());
        return false;
    }
#if defined(_WIN32)
    std::remove(path.c_str());
#endif
    if (std::rename(temp_path.c_str(), path.c_str()) != 0) {
        std::remove(temp_path.c_str());
        return false;
    }
    return true;
}

bool IsJpegData(const std::vector<unsigned char>& data) {
    return data.size() >= 4 && data[0] == 0xFF && data[1] == 0xD8;
}

bool IsPngData(const std::vector<unsigned char>& data) {
    static const unsigned char kPngSignature[] = {
        0x89, 0x50, 0x4E, 0x47,
        0x0D, 0x0A, 0x1A, 0x0A
    };
    return data.size() >= sizeof(kPngSignature) &&
           std::equal(kPngSignature, kPngSignature + sizeof(kPngSignature), data.begin());
}

std::uint32_t ComputeCrc32(const unsigned char* data, std::size_t size) {
    std::uint32_t crc = 0xFFFFFFFFu;
    for (std::size_t i = 0; i < size; ++i) {
        crc ^= static_cast<std::uint32_t>(data[i]);
        for (int bit = 0; bit < 8; ++bit) {
            const std::uint32_t mask = static_cast<std::uint32_t>(-(static_cast<std::int32_t>(crc & 1u)));
            crc = (crc >> 1) ^ (0xEDB88320u & mask);
        }
    }
    return crc ^ 0xFFFFFFFFu;
}

void WriteBigEndian32(std::vector<unsigned char>& data, size_t pos, std::uint32_t value) {
    data[pos] = static_cast<unsigned char>((value >> 24) & 0xFF);
    data[pos + 1] = static_cast<unsigned char>((value >> 16) & 0xFF);
    data[pos + 2] = static_cast<unsigned char>((value >> 8) & 0xFF);
    data[pos + 3] = static_cast<unsigned char>(value & 0xFF);
}

#pragma pack(push, 1)
struct BmpFileHeader {
    std::uint16_t bfType;
    std::uint32_t bfSize;
    std::uint16_t bfReserved1;
    std::uint16_t bfReserved2;
    std::uint32_t bfOffBits;
};

struct BmpInfoHeader {
    std::uint32_t biSize;
    std::int32_t biWidth;
    std::int32_t biHeight;
    std::uint16_t biPlanes;
    std::uint16_t biBitCount;
    std::uint32_t biCompression;
    std::uint32_t biSizeImage;
    std::int32_t biXPelsPerMeter;
    std::int32_t biYPelsPerMeter;
    std::uint32_t biClrUsed;
    std::uint32_t biClrImportant;
};
#pragma pack(pop)

double ReadJfifDpi(const std::string& path) {
    const std::vector<unsigned char> data = ReadFileBytes(path);
    if (!IsJpegData(data)) {
        return -1.0;
    }

    size_t pos = 2;
    while (pos + 3 < data.size()) {
        if (data[pos] != 0xFF) {
            break;
        }
        const unsigned char marker = data[pos + 1];
        if (marker == 0xDA || marker == 0xD9) {
            break;
        }
        if (marker == 0xD8 || marker == 0x01) {
            pos += 2;
            continue;
        }

        const unsigned short segment_length = ReadBigEndian16(data, pos + 2);
        if (segment_length < 2 || pos + 2 + segment_length > data.size()) {
            break;
        }
        if (marker == 0xE0 && segment_length >= 16 &&
            data[pos + 4] == 'J' && data[pos + 5] == 'F' &&
            data[pos + 6] == 'I' && data[pos + 7] == 'F' &&
            data[pos + 8] == 0x00) {
            const unsigned char density_unit = data[pos + 11];
            const unsigned short x_density = ReadBigEndian16(data, pos + 12);
            const unsigned short y_density = ReadBigEndian16(data, pos + 14);
            if (x_density == 0) {
                return -1.0;
            }

            double dpi_x = static_cast<double>(x_density);
            double dpi_y = static_cast<double>(y_density);
            if (density_unit == 2) {
                dpi_x *= 2.54;
                dpi_y *= 2.54;
            } else if (density_unit != 1) {
                return -1.0;
            }
            if (dpi_y > 0.0 && std::abs(dpi_x - dpi_y) <= 5.0) {
                return (dpi_x + dpi_y) / 2.0;
            }
            return dpi_x;
        }
        pos += 2 + segment_length;
    }
    return -1.0;
}

#ifndef __APPLE__
double ReadExifDpi(const std::string& path) {
    ExifData* exif = exif_data_new_from_file(path.c_str());
    if (!exif) {
        return -1.0;
    }

    double dpi = -1.0;
    ExifEntry* x_entry = exif_data_get_entry(exif, EXIF_TAG_X_RESOLUTION);
    if (x_entry && x_entry->format == EXIF_FORMAT_RATIONAL && x_entry->size >= sizeof(ExifRational)) {
        const ExifByteOrder byte_order = exif_data_get_byte_order(exif);
        const ExifRational x_res = exif_get_rational(x_entry->data, byte_order);
        if (x_res.denominator != 0) {
            dpi = static_cast<double>(x_res.numerator) / static_cast<double>(x_res.denominator);
            ExifEntry* y_entry = exif_data_get_entry(exif, EXIF_TAG_Y_RESOLUTION);
            if (y_entry && y_entry->format == EXIF_FORMAT_RATIONAL && y_entry->size >= sizeof(ExifRational)) {
                const ExifRational y_res = exif_get_rational(y_entry->data, byte_order);
                if (y_res.denominator != 0) {
                    const double y_dpi = static_cast<double>(y_res.numerator) / static_cast<double>(y_res.denominator);
                    if (y_dpi > 0.0 && std::abs(dpi - y_dpi) <= 5.0) {
                        dpi = (dpi + y_dpi) / 2.0;
                    }
                }
            }

            ExifEntry* unit_entry = exif_data_get_entry(exif, EXIF_TAG_RESOLUTION_UNIT);
            int unit = 2;
            if (unit_entry && unit_entry->format == EXIF_FORMAT_SHORT && unit_entry->size >= 2) {
                unit = exif_get_short(unit_entry->data, byte_order);
            }
            if (unit == 3) {
                dpi *= 2.54;
            }
        }
    }

    exif_data_unref(exif);
    return dpi;
}
#else
double ReadExifDpi(const std::string& path) {
    (void)path;
    return -1.0;
}
#endif

std::vector<unsigned char> BuildJfifSegment(int dpi) {
    if (dpi <= 0 || dpi > 65535) {
        return std::vector<unsigned char>();
    }
    const unsigned short density = static_cast<unsigned short>(dpi);
    const unsigned char jfif_segment[] = {
        0xFF, 0xE0, 0x00, 0x10,
        'J', 'F', 'I', 'F', 0x00,
        0x01, 0x01, 0x01,
        static_cast<unsigned char>((density >> 8) & 0xFF),
        static_cast<unsigned char>(density & 0xFF),
        static_cast<unsigned char>((density >> 8) & 0xFF),
        static_cast<unsigned char>(density & 0xFF),
        0x00, 0x00
    };
    return std::vector<unsigned char>(jfif_segment, jfif_segment + sizeof(jfif_segment));
}

bool IsJfifApp0(const std::vector<unsigned char>& data, size_t pos, unsigned short segment_length) {
    return segment_length >= 16 &&
           pos + 15 < data.size() &&
           data[pos + 4] == 'J' && data[pos + 5] == 'F' &&
           data[pos + 6] == 'I' && data[pos + 7] == 'F' &&
           data[pos + 8] == 0x00;
}

void AppendJfifDpiSegment(std::vector<unsigned char>& output,
                          const std::vector<unsigned char>& jfif_segment,
                          bool& jfif_written) {
    if (!jfif_written) {
        output.insert(output.end(), jfif_segment.begin(), jfif_segment.end());
        jfif_written = true;
    }
}

std::vector<unsigned char> BuildJpegWithDpiSegments(const std::vector<unsigned char>& jpeg_data,
                                                    int dpi) {
    const std::vector<unsigned char> jfif_segment = BuildJfifSegment(dpi);
    if (!IsJpegData(jpeg_data) || jfif_segment.empty()) {
        return std::vector<unsigned char>();
    }

    std::vector<unsigned char> output;
    output.reserve(jpeg_data.size() + jfif_segment.size());
    output.push_back(0xFF);
    output.push_back(0xD8);

    bool jfif_written = false;
    size_t pos = 2;
    while (pos + 3 < jpeg_data.size() && jpeg_data[pos] == 0xFF) {
        const unsigned char marker = jpeg_data[pos + 1];
        if (marker == 0xDA || marker == 0xD9) {
            break;
        }
        if (marker == 0xD8 || marker == 0x01) {
            pos += 2;
            continue;
        }

        const unsigned short segment_length = ReadBigEndian16(jpeg_data, pos + 2);
        if (segment_length < 2 || pos + 2 + segment_length > jpeg_data.size()) {
            break;
        }

        if (marker == 0xE0 && IsJfifApp0(jpeg_data, pos, segment_length)) {
            std::vector<unsigned char> updated_segment(jpeg_data.begin() + pos,
                                                       jpeg_data.begin() + pos + 2 + segment_length);
            updated_segment[11] = 0x01;
            WriteBigEndian16(updated_segment, 12, static_cast<unsigned short>(dpi));
            WriteBigEndian16(updated_segment, 14, static_cast<unsigned short>(dpi));
            output.insert(output.end(), updated_segment.begin(), updated_segment.end());
            jfif_written = true;
        } else {
            if (!jfif_written) {
                AppendJfifDpiSegment(output, jfif_segment, jfif_written);
            }
            output.insert(output.end(), jpeg_data.begin() + pos, jpeg_data.begin() + pos + 2 + segment_length);
        }
        pos += 2 + segment_length;
    }

    if (!jfif_written) {
        AppendJfifDpiSegment(output, jfif_segment, jfif_written);
    }
    output.insert(output.end(), jpeg_data.begin() + pos, jpeg_data.end());
    return output;
}

bool WriteVector(std::ofstream& output, const std::vector<unsigned char>& bytes) {
    if (bytes.empty()) {
        return true;
    }
    output.write(reinterpret_cast<const char*>(&bytes[0]), static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(output);
}

bool WritePendingDpiSegments(std::ofstream& output,
                             const std::vector<unsigned char>& jfif_segment,
                             bool& jfif_written) {
    if (!jfif_written) {
        if (!WriteVector(output, jfif_segment)) {
            return false;
        }
        jfif_written = true;
    }
    return true;
}

bool WriteJpegDpiStreaming(const std::string& path, int dpi) {
    const std::vector<unsigned char> jfif_segment = BuildJfifSegment(dpi);
    if (path.empty() || jfif_segment.empty()) {
        return false;
    }

    std::ifstream input(path.c_str(), std::ios::binary);
    if (!input) {
        return false;
    }

    const std::string temp_path = path + ".dpi.tmp";
    std::ofstream output(temp_path.c_str(), std::ios::binary | std::ios::trunc);
    if (!output) {
        return false;
    }

    unsigned char soi[2] = {0, 0};
    input.read(reinterpret_cast<char*>(soi), 2);
    if (input.gcount() != 2 || soi[0] != 0xFF || soi[1] != 0xD8) {
        std::remove(temp_path.c_str());
        return false;
    }
    output.write(reinterpret_cast<const char*>(soi), 2);

    bool jfif_written = false;
    while (true) {
        const std::streampos marker_start = input.tellg();
        unsigned char prefix = 0;
        input.read(reinterpret_cast<char*>(&prefix), 1);
        if (input.gcount() != 1) {
            break;
        }
        if (prefix != 0xFF) {
            input.clear();
            input.seekg(marker_start);
            break;
        }

        unsigned char marker = 0;
        do {
            input.read(reinterpret_cast<char*>(&marker), 1);
            if (input.gcount() != 1) {
                std::remove(temp_path.c_str());
                return false;
            }
        } while (marker == 0xFF);

        if (marker == 0xDA || marker == 0xD9) {
            if (!WritePendingDpiSegments(output, jfif_segment, jfif_written)) {
                std::remove(temp_path.c_str());
                return false;
            }
            output.put(static_cast<char>(0xFF));
            output.put(static_cast<char>(marker));
            break;
        }

        if (marker == 0xD8 || marker == 0x01) {
            output.put(static_cast<char>(0xFF));
            output.put(static_cast<char>(marker));
            continue;
        }

        unsigned char length_bytes[2] = {0, 0};
        input.read(reinterpret_cast<char*>(length_bytes), 2);
        if (input.gcount() != 2) {
            std::remove(temp_path.c_str());
            return false;
        }
        const unsigned short segment_length = static_cast<unsigned short>(
            (static_cast<unsigned short>(length_bytes[0]) << 8) |
            static_cast<unsigned short>(length_bytes[1]));
        if (segment_length < 2) {
            std::remove(temp_path.c_str());
            return false;
        }

        std::vector<unsigned char> segment_data(segment_length - 2, 0);
        if (!segment_data.empty()) {
            input.read(reinterpret_cast<char*>(&segment_data[0]), static_cast<std::streamsize>(segment_data.size()));
            if (input.gcount() != static_cast<std::streamsize>(segment_data.size())) {
                std::remove(temp_path.c_str());
                return false;
            }
        }

        std::vector<unsigned char> full_segment;
        full_segment.reserve(segment_length + 2);
        full_segment.push_back(0xFF);
        full_segment.push_back(marker);
        full_segment.push_back(length_bytes[0]);
        full_segment.push_back(length_bytes[1]);
        full_segment.insert(full_segment.end(), segment_data.begin(), segment_data.end());

        const bool is_jfif = marker == 0xE0 && segment_length >= 16 &&
                             segment_data.size() >= 14 &&
                             segment_data[0] == 'J' && segment_data[1] == 'F' &&
                             segment_data[2] == 'I' && segment_data[3] == 'F' &&
                             segment_data[4] == 0x00;

        if (is_jfif) {
            full_segment[11] = 0x01;
            WriteBigEndian16(full_segment, 12, static_cast<unsigned short>(dpi));
            WriteBigEndian16(full_segment, 14, static_cast<unsigned short>(dpi));
            if (!WriteVector(output, full_segment)) {
                std::remove(temp_path.c_str());
                return false;
            }
            jfif_written = true;
            continue;
        }

        if (!WritePendingDpiSegments(output, jfif_segment, jfif_written)) {
            std::remove(temp_path.c_str());
            return false;
        }
        if (!WriteVector(output, full_segment)) {
            std::remove(temp_path.c_str());
            return false;
        }
    }

    if (!WritePendingDpiSegments(output, jfif_segment, jfif_written)) {
        std::remove(temp_path.c_str());
        return false;
    }
    output << input.rdbuf();
    if (input.bad() || output.bad()) {
        std::remove(temp_path.c_str());
        return false;
    }

    output.close();
    input.close();
    if (!output) {
        std::remove(temp_path.c_str());
        return false;
    }
#if defined(_WIN32)
    std::remove(path.c_str());
#endif
    if (std::rename(temp_path.c_str(), path.c_str()) != 0) {
        std::remove(temp_path.c_str());
        return false;
    }
    return true;
}

double ReadJpegDpi(const std::string& path) {
    const double exif_dpi = ReadExifDpi(path);
    const double jfif_dpi = ReadJfifDpi(path);
    if (IsValidDpi(exif_dpi) && std::round(exif_dpi) != 72.0) {
        return exif_dpi;
    }
    if (IsValidDpi(jfif_dpi) && std::round(jfif_dpi) != 72.0) {
        return jfif_dpi;
    }
    if (IsValidDpi(exif_dpi)) {
        return exif_dpi;
    }
    if (IsValidDpi(jfif_dpi)) {
        return jfif_dpi;
    }
    return -1.0;
}

double ReadPngDpi(const std::string& path) {
    const std::vector<unsigned char> data = ReadFileBytes(path);
    if (!IsPngData(data)) {
        return -1.0;
    }

    std::size_t pos = 8;
    while (pos + 12 <= data.size()) {
        const std::uint32_t chunk_length = ReadBigEndian32(data, pos);
        if (pos + 12 + chunk_length > data.size()) {
            return -1.0;
        }

        const std::size_t type_pos = pos + 4;
        const bool is_phys = data[type_pos] == 'p' &&
                             data[type_pos + 1] == 'H' &&
                             data[type_pos + 2] == 'Y' &&
                             data[type_pos + 3] == 's';
        if (is_phys && chunk_length == 9) {
            const std::size_t chunk_data_pos = pos + 8;
            const std::uint32_t pixels_per_unit_x = ReadBigEndian32(data, chunk_data_pos);
            const std::uint32_t pixels_per_unit_y = ReadBigEndian32(data, chunk_data_pos + 4);
            const unsigned char unit_specifier = data[chunk_data_pos + 8];
            if (unit_specifier == 1 && pixels_per_unit_x > 0) {
                const double dpi_x = static_cast<double>(pixels_per_unit_x) / 39.3701;
                if (pixels_per_unit_y > 0) {
                    const double dpi_y = static_cast<double>(pixels_per_unit_y) / 39.3701;
                    if (std::abs(dpi_x - dpi_y) <= 5.0) {
                        return (dpi_x + dpi_y) / 2.0;
                    }
                }
                return dpi_x;
            }
            return -1.0;
        }

        const bool is_iend = data[type_pos] == 'I' &&
                             data[type_pos + 1] == 'E' &&
                             data[type_pos + 2] == 'N' &&
                             data[type_pos + 3] == 'D';
        if (is_iend) {
            break;
        }
        pos += 12 + chunk_length;
    }
    return -1.0;
}

double ReadTiffDpi(const std::string& path) {
    TIFF* tif = TIFFOpen(path.c_str(), "r");
    if (!tif) {
        return -1.0;
    }

    float x_resolution = 0.0f;
    float y_resolution = 0.0f;
    std::uint16_t resolution_unit = RESUNIT_NONE;
    const bool has_x_resolution = TIFFGetField(tif, TIFFTAG_XRESOLUTION, &x_resolution) == 1;
    const bool has_y_resolution = TIFFGetField(tif, TIFFTAG_YRESOLUTION, &y_resolution) == 1;
    if (TIFFGetField(tif, TIFFTAG_RESOLUTIONUNIT, &resolution_unit) != 1) {
        resolution_unit = RESUNIT_INCH;
    }
    TIFFClose(tif);

    if (!has_x_resolution || x_resolution <= 0.0f) {
        return -1.0;
    }

    double dpi_x = static_cast<double>(x_resolution);
    double dpi_y = has_y_resolution && y_resolution > 0.0f ? static_cast<double>(y_resolution) : dpi_x;
    if (resolution_unit == RESUNIT_CENTIMETER) {
        dpi_x *= 2.54;
        dpi_y *= 2.54;
    } else if (resolution_unit != RESUNIT_INCH && resolution_unit != RESUNIT_NONE) {
        return -1.0;
    }

    if (dpi_y > 0.0 && std::abs(dpi_x - dpi_y) <= 5.0) {
        return (dpi_x + dpi_y) / 2.0;
    }
    return dpi_x;
}

double ReadBmpDpi(const std::string& path) {
    const std::vector<unsigned char> data = ReadFileBytes(path);
    if (data.size() < sizeof(BmpFileHeader) + sizeof(BmpInfoHeader)) {
        return -1.0;
    }

    if (data[0] != 'B' || data[1] != 'M') {
        return -1.0;
    }

    const std::uint32_t info_header_size = ReadLittleEndian32(data, sizeof(BmpFileHeader));
    if (info_header_size < sizeof(BmpInfoHeader) ||
        data.size() < sizeof(BmpFileHeader) + info_header_size) {
        return -1.0;
    }

    const std::int32_t pixels_per_meter_x = static_cast<std::int32_t>(
        ReadLittleEndian32(data, sizeof(BmpFileHeader) + 24));
    const std::int32_t pixels_per_meter_y = static_cast<std::int32_t>(
        ReadLittleEndian32(data, sizeof(BmpFileHeader) + 28));
    if (pixels_per_meter_x <= 0 || pixels_per_meter_y <= 0) {
        return -1.0;
    }

    const double dpi_x = static_cast<double>(pixels_per_meter_x) / 39.3701;
    const double dpi_y = static_cast<double>(pixels_per_meter_y) / 39.3701;
    if (std::abs(dpi_x - dpi_y) <= 5.0) {
        return (dpi_x + dpi_y) / 2.0;
    }
    return dpi_x;
}

int ReadImageDpiFromMetadataInternal(const std::string& path) {
    const std::string extension = FileExtension(path);
    double dpi = -1.0;
    if (IsJpegExtension(extension)) {
        dpi = ReadJpegDpi(path);
    } else if (IsPngExtension(extension)) {
        dpi = ReadPngDpi(path);
    } else if (IsTiffExtension(extension)) {
        dpi = ReadTiffDpi(path);
    } else if (IsBmpExtension(extension)) {
        dpi = ReadBmpDpi(path);
    }

    if (IsValidDpi(dpi)) {
        return static_cast<int>(std::round(dpi));
    }
    return 0;
}

bool WriteJpegDpi(const std::string& path, int dpi, int width, int height) {
    (void)width;
    (void)height;
    return WriteJpegDpiStreaming(path, dpi);
}

bool WritePngDpi(const std::string& path, int dpi) {
    std::vector<unsigned char> data = ReadFileBytes(path);
    if (!IsPngData(data)) {
        return false;
    }

    const std::uint32_t pixels_per_meter = static_cast<std::uint32_t>(std::lround(dpi * 39.3701));
    std::vector<unsigned char> phys_chunk(4 + 4 + 9 + 4, 0);
    WriteBigEndian32(phys_chunk, 0, 9);
    phys_chunk[4] = 'p';
    phys_chunk[5] = 'H';
    phys_chunk[6] = 'Y';
    phys_chunk[7] = 's';
    WriteBigEndian32(phys_chunk, 8, pixels_per_meter);
    WriteBigEndian32(phys_chunk, 12, pixels_per_meter);
    phys_chunk[16] = 0x01;
    const std::uint32_t crc = ComputeCrc32(&phys_chunk[4], 13);
    WriteBigEndian32(phys_chunk, 17, crc);

    std::size_t insert_pos = 8;
    if (insert_pos + 8 > data.size()) {
        return false;
    }
    const std::uint32_t ihdr_length = ReadBigEndian32(data, insert_pos);
    if (insert_pos + 12 + ihdr_length > data.size()) {
        return false;
    }
    insert_pos += 12 + ihdr_length;

    std::size_t pos = insert_pos;
    while (pos + 8 <= data.size()) {
        const std::uint32_t chunk_length = ReadBigEndian32(data, pos);
        if (pos + 12 + chunk_length > data.size()) {
            return false;
        }
        const std::size_t type_pos = pos + 4;
        const bool is_phys = data[type_pos] == 'p' &&
                             data[type_pos + 1] == 'H' &&
                             data[type_pos + 2] == 'Y' &&
                             data[type_pos + 3] == 's';
        if (is_phys) {
            data.erase(data.begin() + static_cast<std::ptrdiff_t>(pos),
                       data.begin() + static_cast<std::ptrdiff_t>(pos + 12 + chunk_length));
            break;
        }

        const bool is_idat = data[type_pos] == 'I' &&
                             data[type_pos + 1] == 'D' &&
                             data[type_pos + 2] == 'A' &&
                             data[type_pos + 3] == 'T';
        if (is_idat) {
            insert_pos = pos;
            break;
        }
        pos += 12 + chunk_length;
    }

    data.insert(data.begin() + static_cast<std::ptrdiff_t>(insert_pos), phys_chunk.begin(), phys_chunk.end());
    return WriteFileBytesAtomically(path, data, ".dpi.tmp");
}

bool WriteTiffDpi(const std::string& path, int dpi, int width, int height) {
    TIFF* tif = TIFFOpen(path.c_str(), "r+");
    if (!tif) {
        return false;
    }

    const float resolution = static_cast<float>(dpi);
    bool success = TIFFSetField(tif, TIFFTAG_XRESOLUTION, resolution) == 1 &&
                   TIFFSetField(tif, TIFFTAG_YRESOLUTION, resolution) == 1 &&
                   TIFFSetField(tif, TIFFTAG_RESOLUTIONUNIT, RESUNIT_INCH) == 1;
    if (success && width > 0) {
        success = TIFFSetField(tif, TIFFTAG_IMAGEWIDTH, static_cast<std::uint32_t>(width)) == 1;
    }
    if (success && height > 0) {
        success = TIFFSetField(tif, TIFFTAG_IMAGELENGTH, static_cast<std::uint32_t>(height)) == 1;
    }
    if (success) {
        success = TIFFRewriteDirectory(tif) == 1;
    }
    TIFFClose(tif);
    return success;
}

bool WriteBmpDpi(const std::string& path, int dpi) {
    std::fstream file(path.c_str(), std::ios::in | std::ios::out | std::ios::binary);
    if (!file.is_open()) {
        return false;
    }

    BmpFileHeader file_header = {};
    BmpInfoHeader info_header = {};
    file.read(reinterpret_cast<char*>(&file_header), sizeof(file_header));
    file.read(reinterpret_cast<char*>(&info_header), sizeof(info_header));
    if (!file || file_header.bfType != 0x4D42 || info_header.biSize < sizeof(BmpInfoHeader)) {
        file.close();
        return false;
    }

    const std::int32_t pixels_per_meter = static_cast<std::int32_t>(std::lround(dpi * 39.3701));
    info_header.biXPelsPerMeter = pixels_per_meter;
    info_header.biYPelsPerMeter = pixels_per_meter;
    file.seekp(static_cast<std::streamoff>(sizeof(BmpFileHeader)), std::ios::beg);
    file.write(reinterpret_cast<const char*>(&info_header), sizeof(info_header));
    file.close();
    return static_cast<bool>(file);
}

bool WriteImageDpi(const std::string& path, int dpi, int width, int height) {
    const std::string extension = FileExtension(path);
    if (IsJpegExtension(extension)) {
        return WriteJpegDpi(path, dpi, width, height);
    }
    if (IsPngExtension(extension)) {
        return WritePngDpi(path, dpi);
    }
    if (IsTiffExtension(extension)) {
        return WriteTiffDpi(path, dpi, width, height);
    }
    if (IsBmpExtension(extension)) {
        return WriteBmpDpi(path, dpi);
    }
    return false;
}

} // namespace

int NormalizeImageDpi(int dpi) {
    return dpi > 0 ? dpi : 72;
}

int ReadImageDpiFromMetadata(const std::string& input_path) {
    return ReadImageDpiFromMetadataInternal(input_path);
}

int ResolveImageDpi(const std::string& input_path, int preferred_dpi) {
    if (preferred_dpi > 0) {
        return preferred_dpi;
    }
    if (!input_path.empty()) {
        const int source_dpi = ReadImageDpiFromMetadataInternal(input_path);
        if (source_dpi > 0) {
            return source_dpi;
        }
    }
    return NormalizeImageDpi(preferred_dpi);
}

// 与 WriteJpegBytesWithDpi 使用同一套 DPI 写入逻辑，但返回待写入文件的字节。
std::vector<unsigned char> BuildJpegBytesWithDpi(const std::vector<unsigned char>& jpeg_data,
                                                 int dpi) {
    return BuildJpegWithDpiSegments(jpeg_data, NormalizeImageDpi(dpi));
}

bool WriteJpegBytesWithDpi(const std::string& path,
                           const unsigned char* jpeg_data,
                           std::size_t jpeg_size,
                           int width,
                           int height,
                           int dpi) {
    if (path.empty() || !jpeg_data || jpeg_size == 0) {
        return false;
    }
    const std::vector<unsigned char> input(jpeg_data, jpeg_data + jpeg_size);
    (void)width;
    (void)height;
    const std::vector<unsigned char> output = BuildJpegBytesWithDpi(input, dpi);
    if (output.empty()) {
        return false;
    }
    return WriteFileBytesAtomically(path, output, ".write.tmp");
}

bool WriteJpegBytesWithDpi(const std::string& path,
                           const std::vector<unsigned char>& jpeg_data,
                           int width,
                           int height,
                           int dpi) {
    if (jpeg_data.empty()) {
        return false;
    }
    return WriteJpegBytesWithDpi(path, &jpeg_data[0], jpeg_data.size(), width, height, dpi);
}

bool EncodeJpegBytesLocked(const cv::Mat& image,
                           std::vector<unsigned char>* jpeg_data,
                           const std::vector<int>& params) {
    if (image.empty() || jpeg_data == NULL) {
        return false;
    }
    const auto total_start = std::chrono::steady_clock::now();
    const auto lock_start = std::chrono::steady_clock::now();
    std::lock_guard<std::mutex> lock(graphic::ImageCodecMutex());
    const long long lock_wait_ms = ElapsedMs(lock_start);
    const auto encode_start = std::chrono::steady_clock::now();
    const std::string temp_path = TempJpegPath("sdk_private_jpeg_encode");
    const bool success = params.empty() ? cv::imwrite(temp_path, image)
                                        : cv::imwrite(temp_path, image, params);
    std::vector<unsigned char> encoded;
    if (success) {
        encoded = ReadFileBytes(temp_path);
        jpeg_data->swap(encoded);
    } else {
        jpeg_data->clear();
    }
    std::remove(temp_path.c_str());
    SDK_PRIVATE_LOG_INFO("image.encode_jpeg success={} dimensions={}x{} channels={} params={} lock_wait_ms={} encode_ms={} total_ms={}",
                         success,
                         image.cols,
                         image.rows,
                         image.channels(),
                         params.size(),
                         lock_wait_ms,
                         ElapsedMs(encode_start),
                         ElapsedMs(total_start));
    return success && !jpeg_data->empty();
}

bool WriteImageLocked(const std::string& path,
                      const cv::Mat& image,
                      const std::vector<int>& params) {
    if (path.empty() || image.empty()) {
        SDK_PRIVATE_LOG_WARN("image.write skipped output={} empty_image={} params={}",
                             SafePathSummary(path),
                             image.empty(),
                             params.size());
        return false;
    }
    const auto total_start = std::chrono::steady_clock::now();
    const auto lock_start = std::chrono::steady_clock::now();
    std::lock_guard<std::mutex> lock(graphic::ImageCodecMutex());
    const long long lock_wait_ms = ElapsedMs(lock_start);
    const auto write_start = std::chrono::steady_clock::now();
    bool success = false;
    if (params.empty()) {
        success = cv::imwrite(path, image);
    } else {
        success = cv::imwrite(path, image, params);
    }
    const long long write_ms = ElapsedMs(write_start);
    SDK_PRIVATE_LOG_INFO("image.write output={} success={} dimensions={}x{} channels={} params={} lock_wait_ms={} imwrite_ms={} total_ms={}",
                         SafePathSummary(path),
                         success,
                         image.cols,
                         image.rows,
                         image.channels(),
                         params.size(),
                         lock_wait_ms,
                         write_ms,
                         ElapsedMs(total_start));
    return success;
}

bool WriteDpiToExistingImage(const std::string& path,
                             int width,
                             int height,
                             int dpi) {
    if (path.empty()) {
        return false;
    }
    const int normalized_dpi = NormalizeImageDpi(dpi);
    const auto start = std::chrono::steady_clock::now();
    const bool success = WriteImageDpi(path, normalized_dpi, width, height);
    SDK_PRIVATE_LOG_INFO("image.dpi_write output={} success={} dpi={} dimensions={}x{} elapsed_ms={}",
                         SafePathSummary(path),
                         success,
                         normalized_dpi,
                         width,
                         height,
                         ElapsedMs(start));
    return success;
}

bool WriteImageWithDpiLocked(const std::string& path,
                             const cv::Mat& image,
                             int dpi,
                             const std::vector<int>& params) {
    const auto total_start = std::chrono::steady_clock::now();
    if (IsJpegExtension(FileExtension(path))) {
        if (!WriteImageLocked(path, image, params)) {
            SDK_PRIVATE_LOG_WARN("image.write_with_dpi failed output={} stage=write total_ms={}",
                                 SafePathSummary(path),
                                 ElapsedMs(total_start));
            return false;
        }
        const bool success = WriteDpiToExistingImage(path, image.cols, image.rows, dpi);
        SDK_PRIVATE_LOG_INFO("image.write_with_dpi output={} success={} dpi={} total_ms={}",
                             SafePathSummary(path),
                             success,
                             NormalizeImageDpi(dpi),
                             ElapsedMs(total_start));
        return success;
    }
    if (!WriteImageLocked(path, image, params)) {
        SDK_PRIVATE_LOG_WARN("image.write_with_dpi failed output={} stage=write total_ms={}",
                             SafePathSummary(path),
                             ElapsedMs(total_start));
        return false;
    }
    const bool success = WriteDpiToExistingImage(path, image.cols, image.rows, dpi);
    SDK_PRIVATE_LOG_INFO("image.write_with_dpi output={} success={} dpi={} total_ms={}",
                         SafePathSummary(path),
                         success,
                         NormalizeImageDpi(dpi),
                         ElapsedMs(total_start));
    return success;
}

} // namespace private_impl
} // namespace sdk
} // namespace editor
