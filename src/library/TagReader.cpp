#include "TagReader.h"  // relative: the unit tests build without /I src

#include "../core/Json.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <cwctype>

namespace library {

namespace {

constexpr size_t kTextCap = 1u << 20;      // one text field or comment block
constexpr size_t kLyricsCap = 1u << 20;    // USLT / SYLT
constexpr size_t kCoverCap = 16u << 20;    // a copied picture
constexpr uint32_t kMaxReads = 4096;       // read operations per file
constexpr uint64_t kMaxBytes = 48ull << 20;  // bytes per file (covers included)

// --- bounded reading ---------------------------------------------------------------------------

class Reader {
public:
    explicit Reader(ByteSource& s) : src_(s), size_(s.Size()) {}
    uint64_t Size() const { return size_; }
    bool Spent() const { return reads_ >= kMaxReads || bytes_ >= kMaxBytes; }

    // Exactly `n` bytes at `off`, or false.
    bool Get(uint64_t off, void* buf, size_t n) {
        if (n == 0) return true;
        if (Spent() || off > size_ || n > size_ - off) return false;
        ++reads_;
        bytes_ += n;
        return src_.Read(off, buf, n) == n;
    }
    // Up to `n` bytes (fewer at the end of the file); empty on failure.
    std::vector<uint8_t> Bytes(uint64_t off, size_t n) {
        std::vector<uint8_t> out;
        if (off >= size_) return out;
        n = static_cast<size_t>(std::min<uint64_t>(n, size_ - off));
        if (Spent() || n == 0) return out;
        out.resize(n);
        ++reads_;
        bytes_ += n;
        out.resize(src_.Read(off, out.data(), n));
        return out;
    }

private:
    ByteSource& src_;
    uint64_t size_;
    uint32_t reads_ = 0;
    uint64_t bytes_ = 0;
};

uint32_t Be16(const uint8_t* p) { return (uint32_t{p[0]} << 8) | p[1]; }
uint32_t Be24(const uint8_t* p) { return (uint32_t{p[0]} << 16) | (uint32_t{p[1]} << 8) | p[2]; }
uint32_t Be32(const uint8_t* p) { return (uint32_t{p[0]} << 24) | (uint32_t{p[1]} << 16) | (uint32_t{p[2]} << 8) | p[3]; }
uint64_t Be64(const uint8_t* p) { return (uint64_t{Be32(p)} << 32) | Be32(p + 4); }
uint32_t Le16(const uint8_t* p) { return uint32_t{p[0]} | (uint32_t{p[1]} << 8); }
uint32_t Le32(const uint8_t* p) { return uint32_t{p[0]} | (uint32_t{p[1]} << 8) | (uint32_t{p[2]} << 16) | (uint32_t{p[3]} << 24); }
uint64_t Le64(const uint8_t* p) { return uint64_t{Le32(p)} | (uint64_t{Le32(p + 4)} << 32); }
uint32_t Syncsafe(const uint8_t* p) {
    return (uint32_t{p[0] & 0x7Fu} << 21) | (uint32_t{p[1] & 0x7Fu} << 14) | (uint32_t{p[2] & 0x7Fu} << 7) | (p[3] & 0x7Fu);
}

// --- text --------------------------------------------------------------------------------------

std::wstring Widen(const uint8_t* p, size_t n, UINT cp, DWORD flags) {
    std::wstring w;
    if (n == 0 || n > 0x7FFFFFFF) return w;
    const int len = MultiByteToWideChar(cp, flags, reinterpret_cast<const char*>(p), static_cast<int>(n), nullptr, 0);
    if (len <= 0) return w;
    w.resize(static_cast<size_t>(len));
    MultiByteToWideChar(cp, flags, reinterpret_cast<const char*>(p), static_cast<int>(n), w.data(), len);
    return w;
}

std::wstring Utf8(const uint8_t* p, size_t n) {
    return Widen(p, n, CP_UTF8, 0);
}

std::wstring Utf16(const uint8_t* p, size_t n, bool bigEndian) {
    std::wstring w(n / 2, L'\0');
    for (size_t i = 0; i < w.size(); ++i)
        w[i] = static_cast<wchar_t>(bigEndian ? (p[2 * i] << 8) | p[2 * i + 1] : p[2 * i] | (p[2 * i + 1] << 8));
    return w;
}

void TrimInPlace(std::wstring& s) {
    size_t b = 0, e = s.size();
    while (b < e && (std::iswspace(s[b]) || s[b] == 0 || s[b] == 0xFEFF)) ++b;
    while (e > b && (std::iswspace(s[e - 1]) || s[e - 1] == 0)) --e;
    s = s.substr(b, e - b);
}

// Values separated by NUL (ID3v2.4, APE): joined with "; " like the Windows property system does.
std::wstring JoinNulSeparated(std::wstring s) {
    std::wstring out;
    size_t pos = 0;
    while (pos <= s.size()) {
        size_t nul = s.find(L'\0', pos);
        if (nul == std::wstring::npos) nul = s.size();
        std::wstring part = s.substr(pos, nul - pos);
        TrimInPlace(part);
        if (!part.empty()) {
            if (!out.empty()) out += L"; ";
            out += part;
        }
        pos = nul + 1;
    }
    return out;
}

bool EqualsNoCase(std::wstring_view a, std::wstring_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (std::towupper(a[i]) != std::towupper(b[i])) return false;
    return true;
}

bool EqualsNoCaseA(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if ((a[i] >= 'a' && a[i] <= 'z' ? a[i] - 32 : a[i]) != (b[i] >= 'a' && b[i] <= 'z' ? b[i] - 32 : b[i])) return false;
    return true;
}

// "3", "03/12", " 7 of 9" -> 3 / 12, 7 / 9. Leading number only for the first.
void ParseNumberPair(const std::wstring& s, int* first, int* second) {
    size_t i = 0;
    while (i < s.size() && !std::iswdigit(s[i])) {
        if (!std::iswspace(s[i])) return;  // "A1" (vinyl sides) is no track number
        ++i;
    }
    long long a = 0;
    size_t digits = 0;
    while (i < s.size() && std::iswdigit(s[i]) && digits < 9) a = a * 10 + (s[i++] - L'0'), ++digits;
    if (digits == 0) return;
    if (first && *first <= 0 && a > 0) *first = static_cast<int>(a);
    const size_t slash = s.find(L'/', i);
    if (slash == std::wstring::npos || !second) return;
    i = slash + 1;
    while (i < s.size() && std::iswspace(s[i])) ++i;
    long long b = 0;
    digits = 0;
    while (i < s.size() && std::iswdigit(s[i]) && digits < 9) b = b * 10 + (s[i++] - L'0'), ++digits;
    if (digits && *second <= 0 && b > 0) *second = static_cast<int>(b);
}

int ParseYear(const std::wstring& s) {
    // "2004", "2004-05-01", "2004-05-01T12:00": the first run of four digits.
    for (size_t i = 0; i + 4 <= s.size(); ++i) {
        if (!std::iswdigit(s[i])) continue;
        size_t j = i;
        while (j < s.size() && std::iswdigit(s[j])) ++j;
        if (j - i == 4) {
            const int y = std::stoi(s.substr(i, 4));
            return y > 0 ? y : 0;
        }
        i = j;
    }
    return 0;
}

const wchar_t* const kGenres[] = {
    L"Blues", L"Classic Rock", L"Country", L"Dance", L"Disco", L"Funk", L"Grunge", L"Hip-Hop", L"Jazz", L"Metal",
    L"New Age", L"Oldies", L"Other", L"Pop", L"R&B", L"Rap", L"Reggae", L"Rock", L"Techno", L"Industrial",
    L"Alternative", L"Ska", L"Death Metal", L"Pranks", L"Soundtrack", L"Euro-Techno", L"Ambient", L"Trip-Hop",
    L"Vocal", L"Jazz+Funk", L"Fusion", L"Trance", L"Classical", L"Instrumental", L"Acid", L"House", L"Game",
    L"Sound Clip", L"Gospel", L"Noise", L"AlternRock", L"Bass", L"Soul", L"Punk", L"Space", L"Meditative",
    L"Instrumental Pop", L"Instrumental Rock", L"Ethnic", L"Gothic", L"Darkwave", L"Techno-Industrial",
    L"Electronic", L"Pop-Folk", L"Eurodance", L"Dream", L"Southern Rock", L"Comedy", L"Cult", L"Gangsta", L"Top 40",
    L"Christian Rap", L"Pop/Funk", L"Jungle", L"Native American", L"Cabaret", L"New Wave", L"Psychadelic", L"Rave",
    L"Showtunes", L"Trailer", L"Lo-Fi", L"Tribal", L"Acid Punk", L"Acid Jazz", L"Polka", L"Retro", L"Musical",
    L"Rock & Roll", L"Hard Rock", L"Folk", L"Folk-Rock", L"National Folk", L"Swing", L"Fast Fusion", L"Bebob",
    L"Latin", L"Revival", L"Celtic", L"Bluegrass", L"Avantgarde", L"Gothic Rock", L"Progressive Rock",
    L"Psychedelic Rock", L"Symphonic Rock", L"Slow Rock", L"Big Band", L"Chorus", L"Easy Listening", L"Acoustic",
    L"Humour", L"Speech", L"Chanson", L"Opera", L"Chamber Music", L"Sonata", L"Symphony", L"Booty Bass", L"Primus",
    L"Porn Groove", L"Satire", L"Slow Jam", L"Club", L"Tango", L"Samba", L"Folklore", L"Ballad", L"Power Ballad",
    L"Rhythmic Soul", L"Freestyle", L"Duet", L"Punk Rock", L"Drum Solo", L"A capella", L"Euro-House", L"Dance Hall",
    L"Goa", L"Drum & Bass", L"Club-House", L"Hardcore", L"Terror", L"Indie", L"BritPop", L"Negerpunk", L"Polsk Punk",
    L"Beat", L"Christian Gangsta Rap", L"Heavy Metal", L"Black Metal", L"Crossover", L"Contemporary Christian",
    L"Christian Rock", L"Merengue", L"Salsa", L"Thrash Metal", L"Anime", L"JPop", L"Synthpop", L"Abstract",
    L"Art Rock", L"Baroque", L"Bhangra", L"Big Beat", L"Breakbeat", L"Chillout", L"Downtempo", L"Dub", L"EBM",
    L"Eclectic", L"Electro", L"Electroclash", L"Emo", L"Experimental", L"Garage", L"Global", L"IDM", L"Illbient",
    L"Industro-Goth", L"Jam Band", L"Krautrock", L"Leftfield", L"Lounge", L"Math Rock", L"New Romantic", L"Nu-Breakz",
    L"Post-Punk", L"Post-Rock", L"Psytrance", L"Shoegaze", L"Space Rock", L"Trop Rock", L"World Music",
    L"Neoclassical", L"Audiobook", L"Audio Theatre", L"Neue Deutsche Welle", L"Podcast", L"Indie Rock", L"G-Funk",
    L"Dubstep", L"Garage Rock", L"Psybient",
};

// "(17)", "(17)Rock", "17", "Rock", "(RX)": the first genre as a name.
std::wstring ResolveId3Genre(const std::wstring& raw) {
    std::wstring s = raw;
    if (const size_t semi = s.find(L"; "); semi != std::wstring::npos) s.resize(semi);  // first value
    TrimInPlace(s);
    if (s.size() >= 3 && s[0] == L'(') {
        const size_t close = s.find(L')');
        if (close != std::wstring::npos) {
            std::wstring rest = s.substr(close + 1);
            TrimInPlace(rest);
            if (!rest.empty() && !(rest[0] == L'(')) return rest;
            const std::wstring ref = s.substr(1, close - 1);
            if (ref == L"RX") return L"Remix";
            if (ref == L"CR") return L"Cover";
            if (!ref.empty() && ref.size() <= 3 && std::all_of(ref.begin(), ref.end(), [](wchar_t c) { return std::iswdigit(c); }))
                return Id3GenreName(std::stoi(ref));
            return s;
        }
    }
    if (!s.empty() && s.size() <= 3 && std::all_of(s.begin(), s.end(), [](wchar_t c) { return std::iswdigit(c); })) {
        std::wstring name = Id3GenreName(std::stoi(s));
        return name.empty() ? s : name;
    }
    return s;
}

void SetIfEmpty(std::wstring& field, std::wstring value) {
    if (!field.empty()) return;
    TrimInPlace(value);
    field = std::move(value);
}

void AddExtra(TagInfo& t, std::wstring key, std::wstring value) {
    TrimInPlace(key);
    TrimInPlace(value);
    if (key.empty() || value.empty() || t.extra.size() >= 64) return;
    if (value.size() > 4096) value.resize(4096);
    t.extra.emplace_back(std::move(key), std::move(value));
}

// --- ID3v2 -------------------------------------------------------------------------------------

std::vector<uint8_t> Deunsync(const uint8_t* p, size_t n) {
    std::vector<uint8_t> out;
    out.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        out.push_back(p[i]);
        if (p[i] == 0xFF && i + 1 < n && p[i + 1] == 0x00) ++i;
    }
    return out;
}

// A string of encoding `enc` at p[0..n): decoded text and the bytes it took (with its terminator).
std::wstring Id3String(const uint8_t* p, size_t n, int enc, unsigned cp, size_t* used, bool terminated) {
    size_t end = n, next = n;
    if (terminated) {
        if (enc == 1 || enc == 2) {
            for (size_t i = 0; i + 1 < n; i += 2)
                if (p[i] == 0 && p[i + 1] == 0) {
                    end = i;
                    next = i + 2;
                    break;
                }
        } else {
            for (size_t i = 0; i < n; ++i)
                if (p[i] == 0) {
                    end = i;
                    next = i + 1;
                    break;
                }
        }
    }
    if (used) *used = next;
    switch (enc) {
        case 1: {  // UTF-16 with a BOM (each value of a 2.4 list has its own)
            std::wstring out;
            size_t i = 0;
            bool big = false;
            while (i + 1 < end) {
                size_t j = i;
                if (p[j] == 0xFF && p[j + 1] == 0xFE) big = false, j += 2;
                else if (p[j] == 0xFE && p[j + 1] == 0xFF) big = true, j += 2;
                size_t k = j;
                while (k + 1 < end && !(p[k] == 0 && p[k + 1] == 0)) k += 2;
                if (!out.empty()) out += L'\0';
                out += Utf16(p + j, k - j, big);
                i = k + 2;
            }
            return out;
        }
        case 2: return Utf16(p, end & ~size_t{1}, true);
        case 3: return Utf8(p, end);
        default: return DecodeLegacyText(p, end, cp);
    }
}

struct Id3Frame {
    std::string id;  // "TIT2" (2.2 ids are mapped to 2.3 ones)
    const uint8_t* data;
    size_t size;
};

const char* MapV22(const char* id) {
    static const char* const map[][2] = {
        {"TT2", "TIT2"}, {"TP1", "TPE1"}, {"TP2", "TPE2"}, {"TAL", "TALB"}, {"TRK", "TRCK"}, {"TPA", "TPOS"},
        {"TYE", "TYER"}, {"TCO", "TCON"}, {"ULT", "USLT"}, {"SLT", "SYLT"}, {"PIC", "APIC"}, {"TXX", "TXXX"},
        {"TCM", "TCOM"}, {"TOR", "TORY"}, {"TCP", "TCMP"},
    };
    for (const auto& m : map)
        if (strcmp(m[0], id) == 0) return m[1];
    return nullptr;
}

class Id3Parser {
public:
    Id3Parser(TagInfo& t, const TagOptions& o, unsigned cp) : t_(t), o_(o), cp_(cp) {}

    // The tag at `pos`. Returns the offset after it (pos when there is none).
    uint64_t Parse(Reader& r, uint64_t pos) {
        uint8_t h[10];
        if (!r.Get(pos, h, 10) || memcmp(h, "ID3", 3) != 0) return pos;
        if (h[3] < 2 || h[3] > 4 || h[4] == 0xFF || (h[6] | h[7] | h[8] | h[9]) & 0x80) return pos;
        const int major = h[3];
        const uint8_t flags = h[5];
        uint64_t size = Syncsafe(h + 6);
        const uint64_t end = pos + 10 + size + (major == 4 && (flags & 0x10) ? 10 : 0);
        size = std::min<uint64_t>(size, r.Size() - std::min(r.Size(), pos + 10));  // a cut-off tag: what exists
        if (!o_.tags) return end;
        found_ = true;

        // Frames are walked by their headers in the file; only wanted bodies are read, so a large
        // cover in front of the text costs nothing. Whole-tag unsynchronisation (2.2 / 2.3)
        // changes every offset: such tags (rare) are read whole.
        const bool unsyncAll = (flags & 0x80) && major < 4;
        std::vector<uint8_t> whole;
        uint64_t body = pos + 10, bodySize = size;
        if (unsyncAll) {
            whole = r.Bytes(body, static_cast<size_t>(std::min<uint64_t>(size, o_.cover ? kMaxBytes / 2 : 4 * kTextCap)));
            whole = Deunsync(whole.data(), whole.size());
            bodySize = whole.size();
        }
        auto get = [&](uint64_t off, void* buf, size_t n) {
            if (off > bodySize || n > bodySize - off) return false;
            if (unsyncAll) {
                memcpy(buf, whole.data() + off, n);
                return true;
            }
            return r.Get(body + off, buf, n);
        };
        auto bytes = [&](uint64_t off, size_t n) {
            n = static_cast<size_t>(std::min<uint64_t>(n, bodySize > off ? bodySize - off : 0));
            if (unsyncAll) return std::vector<uint8_t>(whole.begin() + static_cast<ptrdiff_t>(off), whole.begin() + static_cast<ptrdiff_t>(off + n));
            return r.Bytes(body + off, n);
        };

        uint64_t off = 0;
        if (flags & 0x40 && major >= 3) {  // extended header
            uint8_t e[4];
            if (!get(0, e, 4)) return end;
            off = major == 4 ? Syncsafe(e) : uint64_t{Be32(e)} + 4;
            if (major == 4 && off < 6) return end;
        }
        const size_t hdr = major == 2 ? 6 : 10;
        for (int frames = 0; off + hdr <= bodySize && frames < 2048 && !r.Spent(); ++frames) {
            uint8_t fh[10];
            if (!get(off, fh, hdr)) break;
            if (fh[0] == 0) break;  // padding
            char id[5] = {};
            for (size_t i = 0; i < (major == 2 ? 3u : 4u); ++i) {
                const char c = static_cast<char>(fh[i]);
                if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))) return end;
                id[i] = c;
            }
            uint64_t fsize = major == 2 ? Be24(fh + 3) : major == 3 ? Be32(fh + 4) : Syncsafe(fh + 4);
            // iTunes wrote 2.4 tags with plain 32-bit frame sizes: take the reading after which
            // the next frame header (or the end) makes sense.
            if (major == 4 && Be32(fh + 4) >= 0x80) {
                const uint64_t plain = Be32(fh + 4);
                if ((fh[4] | fh[5] | fh[6] | fh[7]) & 0x80) fsize = plain;
                else if (!FrameStartsAt(get, off + hdr + fsize, bodySize) && FrameStartsAt(get, off + hdr + plain, bodySize))
                    fsize = plain;
            }
            const uint16_t fflags = major == 2 ? 0 : static_cast<uint16_t>(Be16(fh + 8));
            const uint64_t start = off + hdr;
            fsize = std::min<uint64_t>(fsize, bodySize - start);
            off = start + fsize;
            std::string fid = id;
            if (major == 2) {
                const char* m = MapV22(id);
                if (!m) continue;
                fid = m;
            }
            if (!Wanted(fid) || fsize == 0) continue;
            // Compressed or encrypted frames are skipped; grouping and length prefixes dropped.
            bool deunsync = false;
            uint64_t skip = 0;
            if (major == 3) {
                if (fflags & 0x00C0) continue;
                if (fflags & 0x0020) skip = 1;
            } else if (major == 4) {
                if (fflags & 0x000C) continue;
                if (fflags & 0x0040) ++skip;
                if (fflags & 0x0001) skip += 4;
                deunsync = (fflags & 0x0002) != 0 && !unsyncAll;
            }
            if (skip >= fsize) continue;
            if (fid == "APIC") t_.hasCover = true;
            if (fid == "APIC" && !o_.cover) continue;
            const size_t cap = fid == "APIC" ? kCoverCap : (fid == "USLT" || fid == "SYLT") ? kLyricsCap : kTextCap;
            std::vector<uint8_t> data = bytes(start + skip, static_cast<size_t>(std::min<uint64_t>(fsize - skip, cap)));
            if (deunsync) data = Deunsync(data.data(), data.size());
            Frame(fid, data.data(), data.size(), major);
        }
        return end;
    }

    bool Found() const { return found_; }

private:
    template <typename Get>
    static bool FrameStartsAt(Get& get, uint64_t off, uint64_t bodySize) {
        if (off == bodySize) return true;
        uint8_t h[4];
        if (!get(off, h, 4)) return off >= bodySize;
        if (h[0] == 0) return true;  // padding
        for (uint8_t c : h)
            if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))) return false;
        return true;
    }

    bool Wanted(const std::string& id) const {
        if (id[0] == 'T') return true;
        if (id == "APIC") return true;
        return o_.lyrics && (id == "USLT" || id == "SYLT");
    }

    void Frame(const std::string& id, const uint8_t* p, size_t n, int major) {
        if (n < 1) return;
        const int enc = p[0] <= 3 ? p[0] : 0;
        if (id == "TXXX") {
            size_t used = 0;
            std::wstring key = Id3String(p + 1, n - 1, enc, cp_, &used, true);
            std::wstring value = JoinNulSeparated(Id3String(p + 1 + used, n - 1 - used, enc, cp_, nullptr, false));
            if (EqualsNoCase(key, L"ALBUMARTIST") || EqualsNoCase(key, L"ALBUM ARTIST") || EqualsNoCase(key, L"ALBUM_ARTIST"))
                txxxAlbumArtist_ = value;
            // FFmpeg writes a "lyrics" field as TXXX:USLT.
            else if (o_.lyrics && (EqualsNoCase(key, L"LYRICS") || EqualsNoCase(key, L"UNSYNCEDLYRICS") || EqualsNoCase(key, L"USLT")))
                SetIfEmpty(t_.lyrics, value);
            AddExtra(t_, std::move(key), std::move(value));
            return;
        }
        if (id == "USLT") {
            if (n < 4) return;
            size_t used = 0;
            Id3String(p + 4, n - 4, enc, cp_, &used, true);  // content descriptor
            if (4 + used <= n) SetIfEmpty(t_.lyrics, Id3String(p + 4 + used, n - 4 - used, enc, cp_, nullptr, false));
            return;
        }
        if (id == "SYLT") {
            Sylt(p, n, enc);
            return;
        }
        if (id == "APIC") {
            Picture(p, n, enc, major);
            return;
        }
        if (id[0] != 'T') return;
        std::wstring v = JoinNulSeparated(Id3String(p + 1, n - 1, enc, cp_, nullptr, false));
        if (v.empty()) return;
        if (id == "TIT2") SetIfEmpty(t_.title, v);
        else if (id == "TPE1") SetIfEmpty(t_.artist, v);
        else if (id == "TPE2") SetIfEmpty(t_.albumArtist, v);
        else if (id == "TALB") SetIfEmpty(t_.album, v);
        else if (id == "TRCK") ParseNumberPair(v, &t_.track, &t_.trackTotal);
        else if (id == "TPOS") ParseNumberPair(v, &t_.disc, &t_.discTotal);
        else if (id == "TCON") SetIfEmpty(t_.genre, ResolveId3Genre(v));
        else if (id == "TYER" || id == "TDRC") {
            if (t_.year <= 0) t_.year = ParseYear(v);
        } else if (id == "TORY" || id == "TDOR") {
            if (originalYear_ <= 0) originalYear_ = ParseYear(v);
        }
    }

    void Sylt(const uint8_t* p, size_t n, int enc) {
        // enc, language[3], stamp format (2 = milliseconds), content type, descriptor, then
        // (text, 32-bit stamp) pairs. MPEG-frame stamps (format 1) are rare and skipped.
        if (n < 6 || p[4] != 2 || !t_.syncedLyrics.empty()) return;
        size_t used = 0;
        Id3String(p + 6, n - 6, enc, cp_, &used, true);
        size_t i = 6 + used;
        std::vector<SyncedLyric> out;
        while (i < n && out.size() < 20000) {
            std::wstring text = Id3String(p + i, n - i, enc, cp_, &used, true);
            if (used == 0 || i + used + 4 > n) break;
            i += used;
            out.push_back({Be32(p + i) / 1000.0, std::move(text)});
            i += 4;
        }
        // Karaoke tags stamp every word or syllable and start a new line with "\n".
        const bool words = std::any_of(out.begin(), out.end(), [](const SyncedLyric& l) {
            return !l.text.empty() && (l.text[0] == L'\n' || l.text[0] == L'\r');
        });
        if (words) {
            std::vector<SyncedLyric> lines;
            for (SyncedLyric& w : out) {
                const bool fresh = lines.empty() || (!w.text.empty() && (w.text[0] == L'\n' || w.text[0] == L'\r'));
                if (fresh) lines.push_back({w.time, {}});
                lines.back().text += w.text;
            }
            out = std::move(lines);
        }
        for (SyncedLyric& l : out) TrimInPlace(l.text);
        t_.syncedLyrics = std::move(out);
    }

    void Picture(const uint8_t* p, size_t n, int enc, int major) {
        size_t i = 1;
        std::string mime;
        if (major == 2) {  // "JPG" / "PNG"
            if (n < 5) return;
            mime = EqualsNoCaseA(std::string_view(reinterpret_cast<const char*>(p + 1), 3), "PNG") ? "image/png" : "image/jpeg";
            i = 4;
        } else {
            while (i < n && p[i] != 0 && mime.size() < 64) mime += static_cast<char>(p[i++]);
            while (i < n && p[i] != 0) ++i;
            ++i;
        }
        if (i >= n) return;
        const int type = p[i++];
        size_t used = 0;
        Id3String(p + i, n - i, enc, cp_, &used, true);
        i += used;
        if (i >= n) return;
        OfferCover(t_, type, mime, p + i, n - i);
    }

public:
    static void OfferCover(TagInfo& t, int type, const std::string& mime, const uint8_t* p, size_t n) {
        t.hasCover = true;
        if (n == 0) return;
        // The front cover wins; otherwise the first picture.
        if (!t.cover.empty() && (t.coverType == 3 || type != 3)) return;
        t.coverType = type;
        t.cover.assign(p, p + std::min(n, kCoverCap));
        t.coverMime = mime.empty() ? "image/jpeg" : mime;
        if (EqualsNoCaseA(t.coverMime, "jpg")) t.coverMime = "image/jpeg";
        if (EqualsNoCaseA(t.coverMime, "png")) t.coverMime = "image/png";
    }

    void Finish() {
        if (t_.albumArtist.empty()) SetIfEmpty(t_.albumArtist, txxxAlbumArtist_);
        if (t_.year <= 0) t_.year = originalYear_;
    }

private:
    TagInfo& t_;
    const TagOptions& o_;
    unsigned cp_;
    bool found_ = false;
    std::wstring txxxAlbumArtist_;
    int originalYear_ = 0;
};

// All ID3v2 tags at `pos` (some files carry two in a row). Returns the offset after them.
uint64_t ParseId3Tags(Reader& r, uint64_t pos, TagInfo& t, const TagOptions& o, unsigned cp) {
    Id3Parser id3(t, o, cp);
    for (int i = 0; i < 4; ++i) {
        const uint64_t next = id3.Parse(r, pos);
        if (next == pos) break;
        pos = next;
    }
    id3.Finish();
    return pos;
}

// --- ID3v1 / APE at the end ---------------------------------------------------------------------

std::wstring V1Text(const uint8_t* p, size_t n, unsigned cp) {
    size_t e = 0;
    while (e < n && p[e] != 0) ++e;
    std::wstring s = DecodeLegacyText(p, e, cp);
    TrimInPlace(s);
    return s;
}

// Fills what the other tags left empty from the ID3v1 tag `b` (the last 128 bytes).
void MergeId3v1(const uint8_t* b, TagInfo& t, unsigned cp) {
    SetIfEmpty(t.title, V1Text(b + 3, 30, cp));
    SetIfEmpty(t.artist, V1Text(b + 33, 30, cp));
    SetIfEmpty(t.album, V1Text(b + 63, 30, cp));
    if (t.year <= 0) t.year = ParseYear(V1Text(b + 93, 4, cp));
    if (b[125] == 0 && b[126] != 0 && t.track <= 0) t.track = b[126];
    if (t.genre.empty() && b[127] != 0xFF) t.genre = Id3GenreName(b[127]);
}

// An APE tag ending at `end` (before ID3v1). Returns its size including header, or 0.
uint64_t ParseApe(Reader& r, uint64_t end, TagInfo& t, const TagOptions& o) {
    if (end < 32) return 0;
    uint8_t f[32];
    if (!r.Get(end - 32, f, 32) || memcmp(f, "APETAGEX", 8) != 0) return 0;
    const uint32_t tagSize = Le32(f + 12), count = Le32(f + 16), flags = Le32(f + 20);
    if (tagSize < 32 || tagSize > (16u << 20) || tagSize > end) return 0;
    const uint64_t start = end - tagSize;
    const uint64_t total = tagSize + ((flags & 0x80000000u) ? 32u : 0u);  // with its header
    if (!o.tags) return total;
    uint64_t pos = start;
    const uint64_t itemsEnd = end - 32;
    for (uint32_t i = 0; i < count && i < 512 && pos + 8 < itemsEnd && !r.Spent(); ++i) {
        uint8_t h[8];
        if (!r.Get(pos, h, 8)) break;
        const uint32_t vsize = Le32(h), iflags = Le32(h + 4);
        std::vector<uint8_t> key = r.Bytes(pos + 8, static_cast<size_t>(std::min<uint64_t>(256, itemsEnd - pos - 8)));
        const auto nul = std::find(key.begin(), key.end(), uint8_t{0});
        if (nul == key.end() || nul == key.begin()) break;
        const std::string name(key.begin(), nul);
        const uint64_t value = pos + 8 + name.size() + 1;
        if (value > itemsEnd || vsize > itemsEnd - value) break;
        pos = value + vsize;
        const bool binary = ((iflags >> 1) & 3) != 0;
        if (binary) {
            if (EqualsNoCaseA(name, "Cover Art (Front)") || EqualsNoCaseA(name, "Cover Art (Back)")) {
                t.hasCover = true;
                if (o.cover && vsize > 0) {
                    std::vector<uint8_t> d = r.Bytes(value, std::min<size_t>(vsize, kCoverCap));
                    const auto z = std::find(d.begin(), d.end(), uint8_t{0});  // "file name\0data"
                    if (z != d.end())
                        Id3Parser::OfferCover(t, EqualsNoCaseA(name, "Cover Art (Front)") ? 3 : 4, {}, &*z + 1,
                                              static_cast<size_t>(d.end() - z - 1));
                }
            }
            continue;
        }
        const bool lyrics = EqualsNoCaseA(name, "Lyrics") || EqualsNoCaseA(name, "UnsyncedLyrics");
        if (lyrics && !o.lyrics) continue;
        std::vector<uint8_t> d = r.Bytes(value, std::min<size_t>(vsize, kTextCap));
        std::wstring v = JoinNulSeparated(Utf8(d.data(), d.size()));
        if (v.empty()) continue;
        if (EqualsNoCaseA(name, "Title")) SetIfEmpty(t.title, v);
        else if (EqualsNoCaseA(name, "Artist")) SetIfEmpty(t.artist, v);
        else if (EqualsNoCaseA(name, "Album")) SetIfEmpty(t.album, v);
        else if (EqualsNoCaseA(name, "Album Artist") || EqualsNoCaseA(name, "AlbumArtist") || EqualsNoCaseA(name, "Album_Artist"))
            SetIfEmpty(t.albumArtist, v);
        else if (EqualsNoCaseA(name, "Track")) ParseNumberPair(v, &t.track, &t.trackTotal);
        else if (EqualsNoCaseA(name, "Disc")) ParseNumberPair(v, &t.disc, &t.discTotal);
        else if (EqualsNoCaseA(name, "Year") || EqualsNoCaseA(name, "Date")) { if (t.year <= 0) t.year = ParseYear(v); }
        else if (EqualsNoCaseA(name, "Genre")) SetIfEmpty(t.genre, ResolveId3Genre(v));
        else if (lyrics) SetIfEmpty(t.lyrics, v);
        else AddExtra(t, Utf8(reinterpret_cast<const uint8_t*>(name.data()), name.size()), v);
    }
    return total;
}

// --- WavPack / Monkey's Audio headers -----------------------------------------------------------

double WavPackDuration(Reader& r) {
    uint8_t h[32];
    if (!r.Get(0, h, 32) || memcmp(h, "wvpk", 4) != 0) return 0;
    static const int kRates[15] = {6000, 8000, 9600, 11025, 12000, 16000, 22050, 24000,
                                   32000, 44100, 48000, 64000, 88200, 96000, 192000};
    const uint32_t total = Le32(h + 12), flags = Le32(h + 24);
    const uint32_t idx = (flags >> 23) & 0xF;
    if (total == 0xFFFFFFFFu || total == 0 || idx >= 15) return 0;
    return static_cast<double>(total) / kRates[idx];
}

double MonkeyDuration(Reader& r) {
    uint8_t h[32];
    if (!r.Get(0, h, 32) || memcmp(h, "MAC ", 4) != 0) return 0;
    const uint32_t version = Le16(h + 4);
    uint32_t blocksPerFrame = 0, finalBlocks = 0, frames = 0, rate = 0;
    if (version >= 3980) {
        const uint32_t descriptor = Le32(h + 8);
        uint8_t d[24];
        if (descriptor < 32 || descriptor > 4096 || !r.Get(descriptor, d, 24)) return 0;
        blocksPerFrame = Le32(d + 4);
        finalBlocks = Le32(d + 8);
        frames = Le32(d + 12);
        rate = Le32(d + 20);
    } else {
        const uint32_t compression = Le16(h + 6);
        rate = Le32(h + 12);
        frames = Le32(h + 24);
        finalBlocks = Le32(h + 28);
        blocksPerFrame = version >= 3950 ? 73728 * 4 : (version >= 3900 || (version >= 3800 && compression == 4000)) ? 73728 : 9216;
    }
    if (frames == 0 || rate == 0) return 0;
    return (static_cast<double>(frames - 1) * blocksPerFrame + finalBlocks) / rate;
}

// --- MPEG audio --------------------------------------------------------------------------------

struct MpegHeader {
    int version = 0;  // 1, 2, 25
    int layer = 0;    // 1..3
    int bitrate = 0;  // kbit/s
    int sampleRate = 0;
    int samples = 0;  // per frame
    int length = 0;   // bytes
    bool mono = false;
};

bool ParseMpeg(const uint8_t* h, MpegHeader* m) {
    if (h[0] != 0xFF || (h[1] & 0xE0) != 0xE0) return false;
    const int vbits = (h[1] >> 3) & 3, lbits = (h[1] >> 1) & 3;
    const int bidx = h[2] >> 4, sidx = (h[2] >> 2) & 3, pad = (h[2] >> 1) & 1;
    if (vbits == 1 || lbits == 0 || bidx == 0 || bidx == 15 || sidx == 3) return false;
    static const int kRates[3] = {44100, 48000, 32000};
    static const int kV1[3][15] = {{0, 32, 64, 96, 128, 160, 192, 224, 256, 288, 320, 352, 384, 416, 448},
                                   {0, 32, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 384},
                                   {0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320}};
    static const int kV2[2][15] = {{0, 32, 48, 56, 64, 80, 96, 112, 128, 144, 160, 176, 192, 224, 256},
                                   {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160}};
    m->version = vbits == 3 ? 1 : vbits == 2 ? 2 : 25;
    m->layer = 4 - lbits;
    m->sampleRate = kRates[sidx] / (m->version == 1 ? 1 : m->version == 2 ? 2 : 4);
    m->bitrate = m->version == 1 ? kV1[m->layer - 1][bidx] : kV2[m->layer == 1 ? 0 : 1][bidx];
    m->mono = (h[3] >> 6) == 3;
    if (m->layer == 1) {
        m->samples = 384;
        m->length = (12 * m->bitrate * 1000 / m->sampleRate + pad) * 4;
    } else {
        m->samples = m->layer == 3 && m->version != 1 ? 576 : 1152;
        m->length = m->samples / 8 * m->bitrate * 1000 / m->sampleRate + pad;
    }
    return m->length >= 4;
}

double MpegDuration(Reader& r, uint64_t start, uint64_t audioEnd) {
    if (audioEnd <= start) return 0;
    const std::vector<uint8_t> b = r.Bytes(start, static_cast<size_t>(std::min<uint64_t>(64 * 1024, audioEnd - start)));
    for (size_t i = 0; i + 4 <= b.size(); ++i) {
        MpegHeader m;
        if (!ParseMpeg(b.data() + i, &m)) continue;
        // A real frame is followed by another of the same kind (or by the end of the audio).
        const uint64_t at = start + i;
        if (at + m.length < audioEnd) {
            uint8_t n[4];
            MpegHeader m2;
            const bool inBuf = i + m.length + 4 <= b.size();
            if (inBuf) memcpy(n, b.data() + i + m.length, 4);
            else if (!r.Get(at + m.length, n, 4)) continue;
            if (!ParseMpeg(n, &m2) || m2.version != m.version || m2.layer != m.layer || m2.sampleRate != m.sampleRate) continue;
        }
        // Xing / Info (VBR and LAME CBR) and VBRI headers carry the frame count; LAME's extension
        // after Xing also gives the encoder delay and padding (the gapless length players show).
        const size_t side = m.version == 1 ? (m.mono ? 17 : 32) : (m.mono ? 9 : 17);
        uint8_t x[200] = {};
        const size_t got = std::min<size_t>(sizeof(x), b.size() - i);
        memcpy(x, b.data() + i, got);
        const size_t xi = 4 + side;
        if (got >= xi + 12 && (memcmp(x + xi, "Xing", 4) == 0 || memcmp(x + xi, "Info", 4) == 0)) {
            const uint32_t flags = Be32(x + xi + 4);
            const uint32_t frames = (flags & 1) ? Be32(x + xi + 8) : 0;
            if (frames > 0) {
                double samples = static_cast<double>(frames) * m.samples;
                const size_t lame = xi + 12 + ((flags & 2) ? 4 : 0) + ((flags & 4) ? 100 : 0) + ((flags & 8) ? 4 : 0);
                if (lame + 24 <= got && (memcmp(x + lame, "LAME", 4) == 0 || memcmp(x + lame, "Lavc", 4) == 0 ||
                                         memcmp(x + lame, "Lavf", 4) == 0 || memcmp(x + lame, "L3.9", 4) == 0)) {
                    const uint32_t dp = Be24(x + lame + 21);
                    const double trim = static_cast<double>((dp >> 12) + (dp & 0xFFF));
                    if (trim < samples) samples -= trim;
                }
                return samples / m.sampleRate;
            }
        } else if (got >= 4 + 32 + 18 && memcmp(x + 36, "VBRI", 4) == 0) {
            const uint32_t frames = Be32(x + 36 + 14);
            if (frames > 0) return static_cast<double>(frames) * m.samples / m.sampleRate;
        }
        return static_cast<double>(audioEnd - at) * 8.0 / (m.bitrate * 1000.0);
    }
    return 0;
}

// --- Vorbis comments (FLAC, Ogg) ----------------------------------------------------------------

void Base64Picture(TagInfo& t, const TagOptions& o, const uint8_t* p, size_t n);

void FlacPicture(TagInfo& t, const TagOptions& o, const uint8_t* p, size_t n) {
    t.hasCover = true;
    if (!o.cover || n < 32) return;
    const uint32_t type = Be32(p);
    const uint32_t mimeLen = Be32(p + 4);
    if (mimeLen > n - 8) return;
    const std::string mime(reinterpret_cast<const char*>(p + 8), mimeLen);
    size_t i = 8 + mimeLen;
    if (i + 4 > n) return;
    const uint32_t descLen = Be32(p + i);
    if (descLen > n - i - 4) return;
    i += 4 + descLen;
    if (i + 20 > n) return;
    const uint32_t dataLen = Be32(p + i + 16);
    i += 20;
    if (dataLen > n - i) return;
    Id3Parser::OfferCover(t, static_cast<int>(type), mime, p + i, dataLen);
}

void ParseVorbisComments(const uint8_t* p, size_t n, TagInfo& t, const TagOptions& o) {
    if (n < 8) return;
    const uint32_t vendor = Le32(p);
    if (vendor > n - 8) return;
    size_t i = 4 + vendor;
    const uint32_t count = Le32(p + i);
    i += 4;
    std::wstring trackTotal, discTotal, albumArtist2;
    for (uint32_t k = 0; k < count && k < 4096 && i + 4 <= n; ++k) {
        const uint32_t len = Le32(p + i);
        i += 4;
        if (len > n - i) break;
        const uint8_t* e = p + i;
        i += len;
        const uint8_t* eq = static_cast<const uint8_t*>(memchr(e, '=', len));
        if (!eq) continue;
        const std::string key(reinterpret_cast<const char*>(e), static_cast<size_t>(eq - e));
        const uint8_t* val = eq + 1;
        const size_t vlen = len - (key.size() + 1);
        if (EqualsNoCaseA(key, "METADATA_BLOCK_PICTURE") || EqualsNoCaseA(key, "COVERART")) {
            t.hasCover = true;
            if (o.cover) Base64Picture(t, o, val, vlen);
            continue;
        }
        const bool lyrics = EqualsNoCaseA(key, "LYRICS") || EqualsNoCaseA(key, "UNSYNCEDLYRICS");
        if (lyrics && !o.lyrics) continue;
        std::wstring v = Utf8(val, std::min(vlen, kTextCap));
        TrimInPlace(v);
        if (v.empty()) continue;
        auto multi = [&](std::wstring& field) {  // repeated fields: "A; B" like the property system
            if (field.empty()) field = v;
            else if (field.size() < 4096) field += L"; " + v;
        };
        if (EqualsNoCaseA(key, "TITLE")) SetIfEmpty(t.title, v);
        else if (EqualsNoCaseA(key, "ARTIST")) multi(t.artist);
        else if (EqualsNoCaseA(key, "ALBUM")) SetIfEmpty(t.album, v);
        else if (EqualsNoCaseA(key, "ALBUMARTIST") || EqualsNoCaseA(key, "ALBUM ARTIST")) multi(t.albumArtist);
        else if (EqualsNoCaseA(key, "ALBUM_ARTIST")) SetIfEmpty(albumArtist2, v);
        else if (EqualsNoCaseA(key, "TRACKNUMBER")) ParseNumberPair(v, &t.track, &t.trackTotal);
        else if (EqualsNoCaseA(key, "TRACKTOTAL") || EqualsNoCaseA(key, "TOTALTRACKS")) SetIfEmpty(trackTotal, v);
        else if (EqualsNoCaseA(key, "DISCNUMBER")) ParseNumberPair(v, &t.disc, &t.discTotal);
        else if (EqualsNoCaseA(key, "DISCTOTAL") || EqualsNoCaseA(key, "TOTALDISCS")) SetIfEmpty(discTotal, v);
        else if (EqualsNoCaseA(key, "DATE") || EqualsNoCaseA(key, "YEAR")) { if (t.year <= 0) t.year = ParseYear(v); }
        else if (EqualsNoCaseA(key, "GENRE")) SetIfEmpty(t.genre, v);
        else if (lyrics) SetIfEmpty(t.lyrics, v);
        else AddExtra(t, Utf8(reinterpret_cast<const uint8_t*>(key.data()), key.size()), v);
    }
    if (t.albumArtist.empty()) t.albumArtist = albumArtist2;
    ParseNumberPair(trackTotal, &t.trackTotal, nullptr);
    ParseNumberPair(discTotal, &t.discTotal, nullptr);
}

void Base64Picture(TagInfo& t, const TagOptions& o, const uint8_t* p, size_t n) {
    std::vector<uint8_t> out;
    out.reserve(n / 4 * 3);
    uint32_t acc = 0;
    int bits = 0;
    for (size_t i = 0; i < n; ++i) {
        const uint8_t c = p[i];
        int v;
        if (c >= 'A' && c <= 'Z') v = c - 'A';
        else if (c >= 'a' && c <= 'z') v = c - 'a' + 26;
        else if (c >= '0' && c <= '9') v = c - '0' + 52;
        else if (c == '+') v = 62;
        else if (c == '/') v = 63;
        else continue;
        acc = (acc << 6) | static_cast<uint32_t>(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<uint8_t>(acc >> bits));
        }
    }
    // METADATA_BLOCK_PICTURE holds a FLAC picture block; the old COVERART field the bare image.
    if (out.size() >= 32 && Be32(out.data()) <= 20 && Be32(out.data() + 4) < 128) FlacPicture(t, o, out.data(), out.size());
    else Id3Parser::OfferCover(t, 3, {}, out.data(), out.size());
}

bool ParseFlac(Reader& r, uint64_t pos, TagInfo& t, const TagOptions& o) {
    uint8_t m[4];
    if (!r.Get(pos, m, 4) || memcmp(m, "fLaC", 4) != 0) return false;
    t.format = TagFormat::Flac;
    pos += 4;
    for (int blocks = 0; blocks < 256 && !r.Spent(); ++blocks) {
        uint8_t h[4];
        if (!r.Get(pos, h, 4)) break;
        const int type = h[0] & 0x7F;
        const uint32_t len = Be24(h + 1);
        const uint64_t data = pos + 4;
        if (type == 0 && len >= 18) {
            uint8_t s[18];
            if (r.Get(data, s, 18)) {
                const uint32_t rate = (uint32_t{s[10]} << 12) | (uint32_t{s[11]} << 4) | (s[12] >> 4);
                const uint64_t samples = (uint64_t{s[13] & 0x0Fu} << 32) | Be32(s + 14);
                if (rate > 0 && samples > 0) t.duration = static_cast<double>(samples) / rate;
            }
        } else if (type == 4 && o.tags) {
            const std::vector<uint8_t> c = r.Bytes(data, std::min<size_t>(len, o.cover ? kCoverCap * 2 : 4 * kTextCap));
            ParseVorbisComments(c.data(), c.size(), t, o);
        } else if (type == 6 && o.tags) {
            t.hasCover = true;
            if (o.cover) {
                const std::vector<uint8_t> c = r.Bytes(data, std::min<size_t>(len, kCoverCap + 4096));
                FlacPicture(t, o, c.data(), c.size());
            }
        } else if (type == 127) {
            break;  // invalid
        }
        pos = data + len;
        if (h[0] & 0x80) break;  // last block
    }
    return true;
}

// --- Ogg ----------------------------------------------------------------------------------------

bool ParseOgg(Reader& r, TagInfo& t, const TagOptions& o) {
    // The first two packets of the first stream: the identification and the comment header.
    std::vector<uint8_t> packets[2];
    int packet = 0;
    uint32_t serial = 0;
    uint64_t pos = 0;
    const size_t cap = o.cover ? kCoverCap * 2 : 4 * kTextCap;
    for (int pages = 0; pages < 512 && packet < 2 && !r.Spent(); ++pages) {
        uint8_t h[27];
        if (!r.Get(pos, h, 27) || memcmp(h, "OggS", 4) != 0) break;
        const uint32_t s = Le32(h + 14);
        if (pages == 0) serial = s;
        uint8_t seg[255];
        if (!r.Get(pos + 27, seg, h[26])) break;
        const uint64_t body = pos + 27 + h[26];
        size_t total = 0;
        for (int i = 0; i < h[26]; ++i) total += seg[i];
        if (s == serial) {
            const std::vector<uint8_t> page = r.Bytes(body, total);  // a cut-off page: what exists
            size_t off = 0;
            for (int i = 0; i < h[26] && packet < 2; ++i) {
                std::vector<uint8_t>& p = packets[packet];
                const size_t take = std::min<size_t>(seg[i], page.size() > off ? page.size() - off : 0);
                if (p.size() + take <= cap) p.insert(p.end(), page.begin() + static_cast<ptrdiff_t>(off), page.begin() + static_cast<ptrdiff_t>(off + take));
                off += seg[i];
                if (seg[i] < 255) ++packet;  // the packet ends here
            }
            if (page.size() < total) break;
        }
        pos = body + total;
    }
    const std::vector<uint8_t>& id = packets[0];
    double rate = 0;
    uint32_t preSkip = 0;
    size_t commentStart = 0;
    if (id.size() >= 16 && id[0] == 1 && memcmp(id.data() + 1, "vorbis", 6) == 0) {
        t.format = TagFormat::Ogg;
        rate = Le32(id.data() + 12);
        if (packets[1].size() >= 7 && packets[1][0] == 3 && memcmp(packets[1].data() + 1, "vorbis", 6) == 0) commentStart = 7;
    } else if (id.size() >= 19 && memcmp(id.data(), "OpusHead", 8) == 0) {
        t.format = TagFormat::Opus;
        rate = 48000;
        preSkip = Le16(id.data() + 10);
        if (packets[1].size() >= 8 && memcmp(packets[1].data(), "OpusTags", 8) == 0) commentStart = 8;
    } else {
        return false;  // Ogg FLAC, Speex, Theora...: the property system
    }
    if (o.tags && commentStart) ParseVorbisComments(packets[1].data() + commentStart, packets[1].size() - commentStart, t, o);

    // Duration: the granule position of the stream's last page, near the end of the file.
    const uint64_t size = r.Size();
    const size_t tail = static_cast<size_t>(std::min<uint64_t>(size, 70000));
    const std::vector<uint8_t> b = r.Bytes(size - tail, tail);
    for (size_t i = b.size() >= 27 ? b.size() - 27 : 0; b.size() >= 27; --i) {
        if (b[i] == 'O' && memcmp(b.data() + i, "OggS", 4) == 0 && b[i + 4] == 0 && Le32(b.data() + i + 14) == serial) {
            const uint64_t g = Le64(b.data() + i + 6);
            if (g != ~0ull && rate > 0) {
                const double d = (static_cast<double>(g) - preSkip) / rate;
                if (d > 0) t.duration = d;
                break;
            }
        }
        if (i == 0) break;
    }
    return true;
}

// --- MP4 ----------------------------------------------------------------------------------------

struct Atom {
    uint64_t start = 0;  // header
    uint64_t data = 0;   // after the header
    uint64_t end = 0;    // may lie past the file end (cut-off file)
    char type[5] = {};
};

bool ReadAtom(Reader& r, uint64_t pos, uint64_t limit, Atom* a) {
    uint8_t h[16];
    if (pos + 8 > limit || !r.Get(pos, h, 8)) return false;
    uint64_t size = Be32(h);
    a->start = pos;
    memcpy(a->type, h + 4, 4);
    a->data = pos + 8;
    if (size == 1) {
        if (!r.Get(pos + 8, h + 8, 8)) return false;
        size = Be64(h + 8);
        a->data = pos + 16;
        if (size < 16) return false;
    } else if (size == 0) {
        size = limit - pos;  // to the end
    } else if (size < 8) {
        return false;
    }
    a->end = pos + size;
    return a->end > pos;  // overflow guard
}

class Mp4Parser {
public:
    Mp4Parser(Reader& r, TagInfo& t, const TagOptions& o) : r_(r), t_(t), o_(o) {}

    bool Parse() {
        uint64_t pos = 0;
        const uint64_t size = r_.Size();
        bool moov = false;
        for (int n = 0; n < 4096 && pos < size && !r_.Spent(); ++n) {
            Atom a;
            if (!ReadAtom(r_, pos, size, &a)) break;
            if (n == 0 && !IsTopLevel(a.type)) return false;
            if (memcmp(a.type, "moov", 4) == 0) {
                moov = true;
                // The index cut off by an interrupted download: nothing plays.
                if (a.end > size) {
                    t_.damaged = true;
                    break;
                }
                Moov(a);
                break;
            }
            pos = a.end;
        }
        t_.format = TagFormat::Mp4;
        if (!moov) t_.damaged = true;
        return true;
    }

private:
    static bool IsTopLevel(const char* type) {
        for (const char* t : {"ftyp", "moov", "mdat", "free", "skip", "wide", "pnot", "uuid", "styp", "sidx", "jP  "})
            if (memcmp(type, t, 4) == 0) return true;
        return false;
    }

    void Moov(const Atom& moov) {
        uint64_t timescale = 0, duration = 0, fragment = 0;
        ForEach(moov, [&](const Atom& a) {
            if (Is(a, "mvhd")) {
                uint8_t b[32];
                if (!r_.Get(a.data, b, 1)) return;
                if (b[0] == 1 && r_.Get(a.data, b, 32)) {
                    timescale = Be32(b + 20);
                    duration = Be64(b + 24);
                } else if (r_.Get(a.data, b, 20)) {
                    timescale = Be32(b + 12);
                    duration = Be32(b + 16);
                    if (duration == 0xFFFFFFFFu) duration = 0;
                }
            } else if (Is(a, "mvex")) {
                ForEach(a, [&](const Atom& m) {
                    uint8_t b[12];
                    if (Is(m, "mehd") && r_.Get(m.data, b, 1))
                        fragment = b[0] == 1 ? (r_.Get(m.data, b, 12) ? Be64(b + 4) : 0) : (r_.Get(m.data, b, 8) ? Be32(b + 4) : 0);
                });
            } else if (Is(a, "udta") && o_.tags) {
                ForEach(a, [&](const Atom& u) {
                    if (Is(u, "meta")) Meta(u);
                });
            } else if (Is(a, "meta") && o_.tags) {
                Meta(a);
            }
        });
        if (duration == 0) duration = fragment;
        if (timescale > 0 && duration > 0) t_.duration = static_cast<double>(duration) / static_cast<double>(timescale);
    }

    void Meta(const Atom& meta) {
        // ISO "meta" is a full box (4 bytes of version and flags); QuickTime's is not.
        uint8_t b[12];
        Atom inner = meta;
        if (r_.Get(meta.data, b, 12) && memcmp(b + 4, "hdlr", 4) != 0) inner.data += 4;
        ForEach(inner, [&](const Atom& a) {
            if (Is(a, "ilst")) Ilst(a);
        });
    }

    void Ilst(const Atom& ilst) {
        ForEach(ilst, [&](const Atom& item) {
            Atom data;
            bool found = false;
            ForEach(item, [&](const Atom& d) {
                if (!found && Is(d, "data")) data = d, found = true;
            });
            if (!found || data.end - data.data < 8) return;
            const uint64_t valueAt = data.data + 8;
            const uint64_t valueLen = std::min(data.end, r_.Size()) - std::min(valueAt, std::min(data.end, r_.Size()));
            uint8_t tb[4];
            if (!r_.Get(data.data, tb, 4)) return;
            const uint32_t type = Be32(tb) & 0xFFFFFF;
            const std::string key(item.type, 4);
            if (key == "covr") {
                t_.hasCover = true;
                if (o_.cover) {
                    const std::vector<uint8_t> v = r_.Bytes(valueAt, static_cast<size_t>(std::min<uint64_t>(valueLen, kCoverCap)));
                    Id3Parser::OfferCover(t_, 3, type == 14 ? "image/png" : type == 27 ? "image/bmp" : "image/jpeg", v.data(), v.size());
                }
                return;
            }
            if (key == "\xA9lyr" && !o_.lyrics) return;
            const std::vector<uint8_t> v = r_.Bytes(valueAt, static_cast<size_t>(std::min<uint64_t>(valueLen, kTextCap)));
            if (key == "trkn" || key == "disk") {
                if (v.size() >= 6) {
                    int* first = key == "trkn" ? &t_.track : &t_.disc;
                    int* second = key == "trkn" ? &t_.trackTotal : &t_.discTotal;
                    if (*first <= 0) *first = static_cast<int>(Be16(v.data() + 2));
                    if (*second <= 0) *second = static_cast<int>(Be16(v.data() + 4));
                }
                return;
            }
            if (key == "gnre") {
                if (v.size() >= 2 && t_.genre.empty()) t_.genre = Id3GenreName(static_cast<int>(Be16(v.data())) - 1);
                return;
            }
            if (type != 1 && type != 2) return;
            std::wstring s = type == 2 ? Utf16(v.data(), v.size() & ~size_t{1}, true) : Utf8(v.data(), v.size());
            TrimInPlace(s);
            if (s.empty()) return;
            if (key == "\xA9nam") SetIfEmpty(t_.title, s);
            else if (key == "\xA9" "ART") SetIfEmpty(t_.artist, s);
            else if (key == "aART") SetIfEmpty(t_.albumArtist, s);
            else if (key == "\xA9" "alb") SetIfEmpty(t_.album, s);
            else if (key == "\xA9gen") SetIfEmpty(t_.genre, s);
            else if (key == "\xA9" "day") { if (t_.year <= 0) t_.year = ParseYear(s); }
            else if (key == "\xA9lyr") SetIfEmpty(t_.lyrics, s);
            else if (key == "\xA9wrt") AddExtra(t_, L"COMPOSER", s);
        });
    }

    template <typename F>
    void ForEach(const Atom& parent, F&& f) {
        const uint64_t limit = std::min(parent.end, r_.Size());
        uint64_t pos = parent.data;
        for (int n = 0; n < 1024 && pos < limit && !r_.Spent(); ++n) {
            Atom a;
            if (!ReadAtom(r_, pos, limit, &a)) break;
            if (++depth_ < 16) f(a);
            --depth_;
            pos = a.end;
        }
    }

    static bool Is(const Atom& a, const char* type) { return memcmp(a.type, type, 4) == 0; }

    Reader& r_;
    TagInfo& t_;
    const TagOptions& o_;
    int depth_ = 0;
};

// --- RIFF WAVE / AIFF ---------------------------------------------------------------------------

double Extended80(const uint8_t* p) {
    // IEEE 754 80-bit extended: sign + 15-bit exponent, 64-bit mantissa with an explicit 1.
    const int exp = static_cast<int>(((p[0] & 0x7F) << 8) | p[1]) - 16383 - 63;
    const uint64_t mant = Be64(p + 2);
    if (mant == 0 || exp > 64 || exp < -128) return 0;
    double v = static_cast<double>(mant);
    return std::ldexp(v, exp);
}

bool ParseRiff(Reader& r, TagInfo& t, const TagOptions& o, unsigned cp) {
    uint8_t h[12];
    if (!r.Get(0, h, 12)) return false;
    const bool wav = memcmp(h, "RIFF", 4) == 0 && memcmp(h + 8, "WAVE", 4) == 0;
    const bool aiff = memcmp(h, "FORM", 4) == 0 && (memcmp(h + 8, "AIFF", 4) == 0 || memcmp(h + 8, "AIFC", 4) == 0);
    if (!wav && !aiff) return false;
    t.format = wav ? TagFormat::Wav : TagFormat::Aiff;
    const uint64_t size = r.Size();
    uint64_t pos = 12, byteRate = 0, dataSize = 0;
    bool haveData = false;
    std::wstring infoTitle_, infoArtist_, infoAlbum_, infoGenre_;
    int infoYear_ = 0, infoTrack_ = 0;
    for (int n = 0; n < 512 && pos + 8 <= size && !r.Spent(); ++n) {
        uint8_t c[8];
        if (!r.Get(pos, c, 8)) break;
        uint64_t len = wav ? Le32(c + 4) : Be32(c + 4);
        const uint64_t data = pos + 8;
        const uint64_t avail = size - data;
        if (wav && memcmp(c, "fmt ", 4) == 0 && len >= 16) {
            uint8_t f[16];
            if (r.Get(data, f, 16)) byteRate = Le32(f + 8);
        } else if (wav && memcmp(c, "data", 4) == 0) {
            haveData = true;
            dataSize = len == 0xFFFFFFFFu || len == 0 || len > avail ? avail : len;  // streamed or cut off
        } else if (aiff && memcmp(c, "COMM", 4) == 0 && len >= 18) {
            uint8_t f[18];
            if (r.Get(data, f, 18)) {
                const double rate = Extended80(f + 8);
                const uint32_t frames = Be32(f + 2);
                if (rate > 0 && frames > 0) t.duration = frames / rate;
            }
        } else if ((memcmp(c, "id3 ", 4) == 0 || memcmp(c, "ID3 ", 4) == 0) && o.tags) {
            ParseId3Tags(r, data, t, o, cp);
        } else if (wav && memcmp(c, "LIST", 4) == 0 && len >= 4 && o.tags) {
            const std::vector<uint8_t> l = r.Bytes(data, static_cast<size_t>(std::min<uint64_t>(len, kTextCap)));
            if (l.size() >= 4 && memcmp(l.data(), "INFO", 4) == 0) {
                // The ID3 chunk, when there is one, wins: INFO fills what it lacks (ParseId3Tags uses SetIfEmpty).
                for (size_t i = 4; i + 8 <= l.size();) {
                    const uint32_t sl = Le32(l.data() + i + 4);
                    if (sl > l.size() - i - 8) break;
                    std::wstring v = DecodeLegacyText(l.data() + i + 8, strnlen(reinterpret_cast<const char*>(l.data() + i + 8), sl), cp);
                    const std::string id(reinterpret_cast<const char*>(l.data() + i), 4);
                    if (id == "INAM") infoTitle_ = v;
                    else if (id == "IART") infoArtist_ = v;
                    else if (id == "IPRD") infoAlbum_ = v;
                    else if (id == "IGNR") infoGenre_ = v;
                    else if (id == "ICRD") infoYear_ = ParseYear(v);
                    else if (id == "ITRK" || id == "IPRT") ParseNumberPair(v, &infoTrack_, nullptr);
                    i += 8 + sl + (sl & 1);
                }
            }
        } else if (aiff && (memcmp(c, "NAME", 4) == 0 || memcmp(c, "AUTH", 4) == 0) && o.tags) {
            const std::vector<uint8_t> v = r.Bytes(data, static_cast<size_t>(std::min<uint64_t>(len, 4096)));
            (memcmp(c, "NAME", 4) == 0 ? infoTitle_ : infoArtist_) = DecodeLegacyText(v.data(), v.size(), cp);
        }
        if (len > avail) len = avail;
        pos = data + len + (len & 1);
    }
    if (wav && haveData && byteRate > 0) t.duration = static_cast<double>(dataSize) / static_cast<double>(byteRate);
    SetIfEmpty(t.title, infoTitle_);
    SetIfEmpty(t.artist, infoArtist_);
    SetIfEmpty(t.album, infoAlbum_);
    SetIfEmpty(t.genre, infoGenre_);
    if (t.year <= 0) t.year = infoYear_;
    if (t.track <= 0) t.track = infoTrack_;
    return true;
}

}  // namespace

// --- public -------------------------------------------------------------------------------------

size_t MemorySource::Read(uint64_t offset, void* buf, size_t n) {
    if (offset >= size_) return 0;
    n = static_cast<size_t>(std::min<uint64_t>(n, size_ - offset));
    memcpy(buf, data_ + offset, n);
    return n;
}

FileSource::FileSource(const std::wstring& path) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(f, &size) || size.QuadPart < 0) {
        CloseHandle(f);
        return;
    }
    file_ = f;
    size_ = static_cast<uint64_t>(size.QuadPart);
}

FileSource::~FileSource() {
    if (file_) CloseHandle(file_);
}

namespace {
constexpr uint64_t kBlock = 16 * 1024;
}

const FileSource::Block* FileSource::Fetch(uint64_t blockOffset) {
    for (Block& b : blocks_)
        if (b.offset == blockOffset) {
            b.used = ++clock_;
            return &b;
        }
    Block* victim = &blocks_[0];
    for (Block& b : blocks_)
        if (b.used < victim->used) victim = &b;
    victim->data.resize(kBlock);
    OVERLAPPED ov{};
    ov.Offset = static_cast<DWORD>(blockOffset);
    ov.OffsetHigh = static_cast<DWORD>(blockOffset >> 32);
    DWORD got = 0;
    if (!ReadFile(file_, victim->data.data(), static_cast<DWORD>(kBlock), &got, &ov)) got = 0;
    victim->offset = blockOffset;
    victim->size = got;
    victim->used = ++clock_;
    return victim;
}

size_t FileSource::Read(uint64_t offset, void* buf, size_t n) {
    if (!file_ || offset >= size_) return 0;
    n = static_cast<size_t>(std::min<uint64_t>(n, size_ - offset));
    uint8_t* out = static_cast<uint8_t*>(buf);
    if (n > 2 * kBlock) {  // large bodies (covers, lyrics): one direct read
        OVERLAPPED ov{};
        ov.Offset = static_cast<DWORD>(offset);
        ov.OffsetHigh = static_cast<DWORD>(offset >> 32);
        DWORD got = 0;
        if (n > 0xFFFFFFFFu || !ReadFile(file_, out, static_cast<DWORD>(n), &got, &ov)) return 0;
        return got;
    }
    size_t done = 0;
    while (done < n) {
        const uint64_t at = offset + done;
        const uint64_t blockOffset = at - at % kBlock;
        const Block* b = Fetch(blockOffset);
        const size_t inBlock = static_cast<size_t>(at - blockOffset);
        if (inBlock >= b->size) break;
        const size_t take = std::min(n - done, b->size - inBlock);
        memcpy(out + done, b->data.data() + inBlock, take);
        done += take;
    }
    return done;
}

std::wstring Id3GenreName(int index) {
    if (index < 0 || index >= static_cast<int>(std::size(kGenres))) return {};
    return kGenres[index];
}

std::wstring DecodeLegacyText(const uint8_t* data, size_t size, unsigned codePage) {
    if (size == 0) return {};
    bool ascii = true;
    for (size_t i = 0; i < size && ascii; ++i) ascii = data[i] < 0x80;
    if (ascii) return std::wstring(data, data + size);
    std::wstring w = Widen(data, size, CP_UTF8, MB_ERR_INVALID_CHARS);
    if (!w.empty()) return w;
    // Not CP_ACP: the manifest makes that UTF-8 inside Lumen.exe.
    return Widen(data, size, codePage ? codePage : json::LegacyCodePage(), 0);
}

bool ReadTags(ByteSource& src, TagInfo* out, const TagOptions& opt) {
    if (!out) return false;
    *out = TagInfo{};
    TagInfo& t = *out;
    Reader r(src);
    const unsigned cp = opt.codePage ? opt.codePage : json::LegacyCodePage();
    uint8_t head[12] = {};
    const size_t got = src.Read(0, head, sizeof(head));
    if (got < 4) return false;

    if (memcmp(head, "fLaC", 4) == 0) return ParseFlac(r, 0, t, opt);
    if (memcmp(head, "OggS", 4) == 0) return ParseOgg(r, t, opt);
    if (got >= 12 && (memcmp(head, "RIFF", 4) == 0 || memcmp(head, "FORM", 4) == 0)) return ParseRiff(r, t, opt, cp);
    if (got >= 8) {
        for (const char* type : {"ftyp", "moov", "mdat", "free", "skip", "wide"})
            if (memcmp(head + 4, type, 4) == 0) return Mp4Parser(r, t, opt).Parse();
    }

    // MP3 (an ID3v2 tag or an MPEG frame first) and the APE family, with tags at the end.
    uint64_t audio = 0;
    if (memcmp(head, "ID3", 3) == 0) audio = ParseId3Tags(r, 0, t, opt, cp);
    uint8_t after[4] = {};
    if (audio > 0 && r.Get(audio, after, 4) && memcmp(after, "fLaC", 4) == 0) {
        TagInfo id3 = std::move(t);  // an ID3 tag in front of FLAC: the FLAC blocks win
        t = TagInfo{};
        ParseFlac(r, audio, t, opt);
        SetIfEmpty(t.title, id3.title);
        SetIfEmpty(t.artist, id3.artist);
        SetIfEmpty(t.album, id3.album);
        return true;
    }
    const bool apeFamily = audio == 0 && (memcmp(head, "MAC ", 4) == 0 || memcmp(head, "wvpk", 4) == 0 ||
                                          memcmp(head, "MPCK", 4) == 0 || memcmp(head, "MP+", 3) == 0);
    MpegHeader m;
    const bool mpeg = audio > 0 || (got >= 4 && ParseMpeg(head, &m));
    if (!mpeg && !apeFamily) return false;
    t.format = apeFamily ? TagFormat::Ape : TagFormat::Mp3;
    // Priority: ID3v2, then APE, then ID3v1 (each only fills what is still empty).
    uint8_t v1tag[128];
    const uint64_t v1 = r.Size() >= 128 && r.Get(r.Size() - 128, v1tag, 128) && memcmp(v1tag, "TAG", 3) == 0 ? 128 : 0;
    const uint64_t ape = ParseApe(r, r.Size() - v1, t, opt);
    if (v1 && opt.tags) MergeId3v1(v1tag, t, cp);
    if (!apeFamily) t.duration = MpegDuration(r, audio, r.Size() - v1 - std::min(ape, r.Size() - v1));
    else if (head[0] == 'w') t.duration = WavPackDuration(r);
    else if (head[0] == 'M' && head[1] == 'A') t.duration = MonkeyDuration(r);
    return true;
}

bool ReadTagsFromFile(const std::wstring& path, TagInfo* out, const TagOptions& opt) {
    FileSource f(path);
    if (!f.Ok()) {
        if (out) *out = TagInfo{};
        return false;
    }
    return ReadTags(f, out, opt);
}

}  // namespace library
