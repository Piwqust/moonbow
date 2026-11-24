#include "net/ServiceKeys.h"

// build.bat generates the header into its object folder; without it (other build setups) the
// build simply carries no keys.
#if __has_include("ServiceKeys.gen.h")
#include "ServiceKeys.gen.h"
#define MOONBOW_SERVICE_KEYS 1
#endif

namespace net::keys {

namespace {

[[maybe_unused]] std::string Decode(const unsigned char* data, const unsigned char* mask, unsigned len) {
    std::string s(len, '\0');
    for (unsigned i = 0; i < len; ++i) s[i] = static_cast<char>(data[i] ^ mask[i]);
    return s;
}

}  // namespace

#ifdef MOONBOW_SERVICE_KEYS
#define MOONBOW_KEY(name) \
    std::string name() { return Decode(gen::k##name, gen::k##name##Mask, gen::k##name##Len); }
#else
#define MOONBOW_KEY(name) \
    std::string name() { return {}; }
#endif

MOONBOW_KEY(OpenSubtitles)
MOONBOW_KEY(Tmdb)
MOONBOW_KEY(LastfmKey)
MOONBOW_KEY(LastfmSecret)
MOONBOW_KEY(DiscordApp)

#undef MOONBOW_KEY

}  // namespace net::keys
