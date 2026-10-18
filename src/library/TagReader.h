#pragma once
// Lumen's own tag reader, used by the library scan before the Windows property system.
//
// Formats (sniffed from the content, not the extension):
// - MP3: ID3v2.2/2.3/2.4 (all text encodings, unsynchronisation, TXXX, USLT, SYLT, APIC), APE and
//   ID3v1 at the end; duration from the Xing/Info/VBRI header or the CBR bitrate;
// - FLAC: STREAMINFO, VORBIS_COMMENT, PICTURE (an ID3v2 tag in front is skipped);
// - Ogg Vorbis and Opus: the comment packet, duration from the last page's granule position;
// - MP4/M4A/MOV: mvhd duration and the ilst atoms (©nam, ©ART, aART, ©alb, trkn, disk, ©gen, gnre,
//   ©day, ©lyr, covr); a file without a readable moov is reported damaged;
// - WAV (LIST INFO, "id3 " chunk) and AIFF/AIFC (COMM, "ID3 " chunk), duration from the header;
// - APE tags (and ID3v1) at the end of Monkey's Audio, WavPack and Musepack files.
//
// Safety: only headers and tag regions are read, through bounded reads; every size field is
// checked against the bytes that exist and every loop advances, so truncated or garbage files end
// the parse early instead of crashing or spinning. A read budget (operations and bytes) stops a
// pathological file even when each step looks valid.

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace library {

// Random-access bytes: a file, or memory in tests.
class ByteSource {
public:
    virtual ~ByteSource() = default;
    virtual uint64_t Size() const = 0;
    // Up to `n` bytes at `offset`; returns how many were read (fewer at the end or on an error).
    virtual size_t Read(uint64_t offset, void* buf, size_t n) = 0;
};

class MemorySource : public ByteSource {
public:
    MemorySource(const void* data, size_t size) : data_(static_cast<const uint8_t*>(data)), size_(size) {}
    uint64_t Size() const override { return size_; }
    size_t Read(uint64_t offset, void* buf, size_t n) override;

private:
    const uint8_t* data_;
    size_t size_;
};

// A file opened for shared reading, with a small block cache (most parses touch the first and the
// last few kilobytes only).
class FileSource : public ByteSource {
public:
    explicit FileSource(const std::wstring& path);
    ~FileSource() override;
    FileSource(const FileSource&) = delete;
    FileSource& operator=(const FileSource&) = delete;

    bool Ok() const { return file_ != nullptr; }
    uint64_t Size() const override { return size_; }
    size_t Read(uint64_t offset, void* buf, size_t n) override;

private:
    struct Block {
        uint64_t offset = ~0ull;
        size_t size = 0;
        uint32_t used = 0;
        std::vector<uint8_t> data;
    };
    const Block* Fetch(uint64_t blockOffset);

    void* file_ = nullptr;  // HANDLE
    uint64_t size_ = 0;
    Block blocks_[4];
    uint32_t clock_ = 0;
};

enum class TagFormat : uint8_t { Unknown, Mp3, Flac, Ogg, Opus, Mp4, Wav, Aiff, Ape };

struct SyncedLyric {
    double time = 0;  // seconds
    std::wstring text;
};

struct TagInfo {
    TagFormat format = TagFormat::Unknown;
    double duration = 0;   // seconds, 0 = not known cheaply
    bool damaged = false;  // the container is broken (an MP4 without its index)

    std::wstring title, artist, album, albumArtist, genre;
    int year = 0;
    int track = 0, trackTotal = 0;
    int disc = 0, discTotal = 0;

    std::wstring lyrics;                     // unsynced: USLT, LYRICS / UNSYNCEDLYRICS, ©lyr
    std::vector<SyncedLyric> syncedLyrics;   // ID3 SYLT with millisecond stamps, in file order
    std::vector<std::pair<std::wstring, std::wstring>> extra;  // TXXX and other named fields

    bool hasCover = false;
    int coverType = -1;         // ID3 / FLAC picture type of `cover` (3 = front)
    std::string coverMime;      // "image/jpeg", when known
    std::vector<uint8_t> cover;  // the front cover (else the first picture), with TagOptions::cover
};

struct TagOptions {
    bool tags = true;     // false: only the format, the duration and the damaged flag
    bool lyrics = false;  // USLT / SYLT / lyrics fields (large; the scan doesn't need them)
    bool cover = false;   // copy the cover bytes (up to 16 MB)
    unsigned codePage = 0;  // 8-bit legacy text (ID3v1, Latin-1 ID3v2 frames, RIFF INFO); 0 = the system's
};

// True when the content is a format above (`out->format` says which), even if it has no tags.
bool ReadTags(ByteSource& src, TagInfo* out, const TagOptions& opt = {});
bool ReadTagsFromFile(const std::wstring& path, TagInfo* out, const TagOptions& opt = {});

// ID3v1 / Winamp genre number -> name ("Rock" for 17), empty when out of range.
std::wstring Id3GenreName(int index);

// Legacy 8-bit text: ASCII as is, valid UTF-8 as UTF-8 (taggers write it into Latin-1 frames),
// anything else in `codePage` (0 = the system's ANSI code page, CP-1251 on Russian Windows).
std::wstring DecodeLegacyText(const uint8_t* data, size_t size, unsigned codePage = 0);

}  // namespace library
