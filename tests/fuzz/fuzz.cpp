// Fuzz targets for the parsers that read bytes Moonbow does not control: tags inside media files,
// JSON (data files and the online services' answers), .lrc, skins, ASS subtitle text, .m3u and
// episode file names. Build and run with tests\fuzz\build-fuzz.bat.
//
// Each target has libFuzzer's shape. Built with /fsanitize=address,fuzzer (needs Visual Studio's
// C++ AddressSanitizer component) the same targets run coverage-guided, the target picked by
// MOONBOW_FUZZ_TARGET. Without it, main() below mutates the seed files itself while the debug CRT
// checks indexes, iterators and the heap.
//
//   fuzz.exe <target> <seed dir> [seconds] [crash dir]   mutate for that long (default 60 s)
//   fuzz.exe <target> <file or dir>                      replay: every file once (regressions)

#include "../../src/app/player/Episodes.h"
#include "../../src/app/player/Lyrics.h"
#include "../../src/app/player/SubText.h"
#include "../../src/core/Json.h"
#include "../../src/core/Skin.h"
#include "../../src/library/Playlists.h"
#include "../../src/library/TagReader.h"
#include "../../src/net/OnlineData.h"

#include <windows.h>

#include <crtdbg.h>
#include <dbghelp.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

using Bytes = std::vector<uint8_t>;

std::string_view Text(const uint8_t* d, size_t n) {
    return {reinterpret_cast<const char*>(d), n};
}

std::wstring Wide(const uint8_t* d, size_t n) {
    return app::DecodeLyricsFile(Text(d, n), 1251);  // UTF-8, UTF-16 with BOM or CP-1251, like real files
}

void FuzzTags(const uint8_t* d, size_t n) {
    library::MemorySource src(d, n);
    library::TagInfo info;
    library::TagOptions opt;
    opt.lyrics = opt.cover = true;
    opt.codePage = 1251;
    library::ReadTags(src, &info, opt);
}

void FuzzJson(const uint8_t* d, size_t n) {
    const std::optional<json::Value> v = json::Parse(Text(d, n));
    if (!v) return;
    json::Parse(json::Serialize(*v, true));
    net::ParseLrclibRecord(*v);
    net::PickLrclibRecord(*v, 180);
    net::ParseSubtitleSearch(*v);
    net::ParseSubtitleDownload(*v);
    net::ParseSubtitleLogin(*v);
    net::ParseSubtitleQuota(*v);
    net::ParseTmdbSearch(*v, false);
    net::ParseTmdbSearch(*v, true);
    net::ParseTmdbEpisode(*v);
}

void FuzzLyrics(const uint8_t* d, size_t n) {
    const app::Lyrics l = app::ParseLyrics(Wide(d, n));
    for (double t : {-1.0, 0.0, 12.5, 1e9}) {
        const int line = l.LineAt(t);
        l.SungFraction(line < 0 ? 0 : line, t);
    }
}

void FuzzSkin(const uint8_t* d, size_t n) {
    skin::Parse(Text(d, n));
}

void FuzzAss(const uint8_t* d, size_t n) {
    const std::wstring w = Wide(d, n);
    app::subtext::ParseEvents(w);
    app::subtext::ParseAssHeader(w);
}

void FuzzM3u(const uint8_t* d, size_t n) {
    static const std::wstring path = [] {
        wchar_t tmp[MAX_PATH];
        GetTempPathW(MAX_PATH, tmp);
        return std::wstring(tmp) + L"moonbow-fuzz-" + std::to_wstring(GetCurrentProcessId()) + L".m3u";
    }();
    {
        std::ofstream f(path, std::ios::binary | std::ios::trunc);
        f.write(reinterpret_cast<const char*>(d), static_cast<std::streamsize>(n));
    }
    std::vector<library::PlaylistItem> items;
    library::ReadM3u(path, &items, 1251);
}

void FuzzEpisode(const uint8_t* d, size_t n) {
    const std::wstring name = L"C:\\Media\\" + Wide(d, n) + L".mkv";
    app::episodes::Parse(name);
    app::episodes::DisplayName(name);
    app::episodes::Title(name);
    app::episodes::KindOf(Wide(d, n));
    app::episodes::IsNext(name, L"C:\\Media\\Show S01E02.mkv");
}

struct Target {
    const char* name;
    void (*fn)(const uint8_t*, size_t);
};

constexpr Target kTargets[] = {
    {"tags", FuzzTags}, {"json", FuzzJson}, {"lyrics", FuzzLyrics}, {"skin", FuzzSkin},
    {"ass", FuzzAss},   {"m3u", FuzzM3u},   {"episode", FuzzEpisode},
};

const Target* Find(const char* name) {
    for (const Target& t : kTargets)
        if (name && std::strcmp(t.name, name) == 0) return &t;
    return nullptr;
}

}  // namespace

#ifdef MOONBOW_LIBFUZZER

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* d, size_t n) {
    static const Target* t = Find(std::getenv("MOONBOW_FUZZ_TARGET"));
    if (!t) std::abort();
    t->fn(d, n);
    return 0;
}

#else

namespace {

// The input being run, saved by the handlers below when it brings the process down.
const Bytes* g_current = nullptr;
std::string g_crashDir;
const char* g_target = "";

// Where it broke: the stack of this thread, by function and line (fuzz.pdb sits next to fuzz.exe).
void PrintStack() {
    void* frames[48];
    const USHORT n = CaptureStackBackTrace(2, 48, frames, nullptr);
    HANDLE process = GetCurrentProcess();
    SymSetOptions(SYMOPT_UNDNAME | SYMOPT_LOAD_LINES);
    SymInitialize(process, nullptr, TRUE);
    alignas(SYMBOL_INFO) char buf[sizeof(SYMBOL_INFO) + 256];
    auto* sym = reinterpret_cast<SYMBOL_INFO*>(buf);
    for (USHORT i = 0; i < n; ++i) {
        sym->SizeOfStruct = sizeof(SYMBOL_INFO);
        sym->MaxNameLen = 255;
        DWORD64 disp = 0;
        DWORD lineDisp = 0;
        IMAGEHLP_LINE64 line{sizeof(IMAGEHLP_LINE64)};
        const DWORD64 at = reinterpret_cast<DWORD64>(frames[i]);
        const bool named = SymFromAddr(process, at, &disp, sym) != FALSE;
        const bool lined = SymGetLineFromAddr64(process, at, &lineDisp, &line) != FALSE;
        std::printf("  %s  %s:%lu\n", named ? sym->Name : "?", lined ? line.FileName : "?", lined ? line.LineNumber : 0);
    }
}

void SaveCurrentAndExit(const char* why) {
    static volatile LONG once = 0;
    if (InterlockedExchange(&once, 1)) Sleep(INFINITE);
    PrintStack();
    char path[MAX_PATH * 2];
    snprintf(path, sizeof path, "%s\\crash-%s-%llu.bin", g_crashDir.c_str(), g_target, GetTickCount64());
    if (FILE* f = nullptr; g_current && fopen_s(&f, path, "wb") == 0 && f) {
        fwrite(g_current->data(), 1, g_current->size(), f);
        fclose(f);
    }
    std::printf("\nCRASH (%s): input saved to %s\n", why, path);
    std::fflush(stdout);
    TerminateProcess(GetCurrentProcess(), 99);
}

LONG WINAPI OnException(EXCEPTION_POINTERS* ep) {
    char why[64];
    snprintf(why, sizeof why, "exception 0x%08lX", ep->ExceptionRecord->ExceptionCode);
    SaveCurrentAndExit(why);
    return EXCEPTION_EXECUTE_HANDLER;
}

void InstallHandlers() {
    SetUnhandledExceptionFilter(OnException);
    std::set_terminate([] { SaveCurrentAndExit("std::terminate"); });
    _set_invalid_parameter_handler([](const wchar_t*, const wchar_t*, const wchar_t*, unsigned, uintptr_t) {
        SaveCurrentAndExit("CRT invalid parameter");
    });
    _CrtSetReportHook2(_CRT_RPTHOOK_INSTALL, [](int type, char* message, int*) -> int {
        if (type == _CRT_WARN) return FALSE;
        std::printf("\n%s", message ? message : "");
        SaveCurrentAndExit("debug CRT check");
        return TRUE;
    });
    // Catches heap corruption near where it happens, not at the end of the run.
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_CHECK_EVERY_128_DF);
}

std::vector<Bytes> ReadFiles(const std::filesystem::path& p) {
    std::vector<Bytes> out;
    auto read = [&](const std::filesystem::path& f) {
        std::ifstream in(f, std::ios::binary);
        out.emplace_back(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    };
    if (std::filesystem::is_directory(p)) {
        for (const auto& e : std::filesystem::directory_iterator(p))
            if (e.is_regular_file()) read(e.path());
    } else if (std::filesystem::exists(p)) {
        read(p);
    }
    return out;
}

struct Rng {
    uint64_t s = 0x9E3779B97F4A7C15ull ^ GetTickCount64();
    uint64_t Next() {
        s ^= s << 13;
        s ^= s >> 7;
        s ^= s << 17;
        return s;
    }
    size_t Below(size_t n) { return n ? static_cast<size_t>(Next() % n) : 0; }
};

// Values that break length fields and loop bounds.
constexpr uint32_t kInteresting[] = {0, 1, 0x7F, 0x80, 0xFF, 0x100, 0x7FFF, 0xFFFF, 0x10000, 0x7FFFFFFF,
                                     0x80000000, 0xFFFFFFFF, 0xFFFFFFFE, 0x00FFFFFF};

void Mutate(Bytes& b, const std::vector<Bytes>& seeds, Rng& r) {
    const int count = 1 + static_cast<int>(r.Below(4));
    for (int i = 0; i < count; ++i) {
        switch (r.Below(8)) {
            case 0:
                if (!b.empty()) b[r.Below(b.size())] ^= static_cast<uint8_t>(1u << r.Below(8));
                break;
            case 1:
                if (!b.empty()) b[r.Below(b.size())] = static_cast<uint8_t>(kInteresting[r.Below(std::size(kInteresting))]);
                break;
            case 2:
                if (b.size() >= 4) {
                    const uint32_t v = kInteresting[r.Below(std::size(kInteresting))];
                    const size_t at = r.Below(b.size() - 3);
                    const bool big = r.Below(2) != 0;  // ID3, MP4 and Matroska are big-endian, RIFF and FLAC blocks little
                    for (int k = 0; k < 4; ++k) b[at + k] = static_cast<uint8_t>(v >> (big ? 24 - 8 * k : 8 * k));
                }
                break;
            case 3: {  // insert random bytes
                const size_t len = 1 + r.Below(64);
                Bytes add(len);
                for (auto& x : add) x = static_cast<uint8_t>(r.Next());
                b.insert(b.begin() + static_cast<ptrdiff_t>(r.Below(b.size() + 1)), add.begin(), add.end());
                break;
            }
            case 4:  // delete a chunk
                if (!b.empty()) {
                    const size_t at = r.Below(b.size());
                    const size_t len = 1 + r.Below(std::min<size_t>(b.size() - at, 256));
                    b.erase(b.begin() + static_cast<ptrdiff_t>(at), b.begin() + static_cast<ptrdiff_t>(at + len));
                }
                break;
            case 5:  // duplicate a chunk
                if (!b.empty() && b.size() < (1u << 20)) {
                    const size_t at = r.Below(b.size());
                    const size_t len = 1 + r.Below(std::min<size_t>(b.size() - at, 512));
                    const Bytes chunk(b.begin() + static_cast<ptrdiff_t>(at), b.begin() + static_cast<ptrdiff_t>(at + len));
                    b.insert(b.begin() + static_cast<ptrdiff_t>(r.Below(b.size() + 1)), chunk.begin(), chunk.end());
                }
                break;
            case 6:  // truncate
                b.resize(r.Below(b.size() + 1));
                break;
            default: {  // splice the tail of another seed
                const Bytes& other = seeds[r.Below(seeds.size())];
                if (other.empty()) break;
                const size_t from = r.Below(other.size());
                b.resize(r.Below(b.size() + 1));
                b.insert(b.end(), other.begin() + static_cast<ptrdiff_t>(from), other.end());
                break;
            }
        }
    }
    if (b.size() > (1u << 20)) b.resize(1u << 20);
}

}  // namespace

int main(int argc, char** argv) {
    const Target* t = argc > 2 ? Find(argv[1]) : nullptr;
    if (!t) {
        std::printf("Usage: fuzz.exe <target> <seed dir> [seconds] [crash dir]\n       fuzz.exe <target> <file or dir>  (replay)\nTargets:");
        for (const Target& k : kTargets) std::printf(" %s", k.name);
        std::printf("\n");
        return 2;
    }
    g_target = t->name;
    g_crashDir = argc > 4 ? argv[4] : ".";
    InstallHandlers();
    const std::vector<Bytes> seeds = ReadFiles(argv[2]);
    if (seeds.empty()) {
        std::printf("No input files in %s\n", argv[2]);
        return 2;
    }
    if (argc == 3) {  // replay
        for (const Bytes& s : seeds) {
            g_current = &s;
            t->fn(s.data(), s.size());
        }
        std::printf("%s: %zu inputs replayed, no crash\n", t->name, seeds.size());
        return 0;
    }
    const double seconds = std::atof(argv[3]);
    const auto end = std::chrono::steady_clock::now() + std::chrono::duration<double>(seconds > 0 ? seconds : 60);
    Rng rng;
    Bytes input;
    g_current = &input;
    uint64_t runs = 0;
    for (const Bytes& s : seeds) {  // the seeds themselves first
        input = s;
        t->fn(input.data(), input.size());
    }
    while (std::chrono::steady_clock::now() < end) {
        for (int i = 0; i < 64; ++i, ++runs) {
            input = seeds[rng.Below(seeds.size())];
            Mutate(input, seeds, rng);
            t->fn(input.data(), input.size());
        }
    }
    std::printf("%s: %llu runs, no crash\n", t->name, static_cast<unsigned long long>(runs));
    return 0;
}

#endif
