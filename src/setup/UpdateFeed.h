#pragma once
// The update feed: moonbow-update.json, attached to every GitHub release (and copied to the mirror on
// moonbow.angleford.co when that is set up). tools/release/make-update-feed.ps1 writes it:
//
//   {"payload": "<manifest JSON as a string>", "signature": "<base64 of r|s>"}
//   manifest: {"product": "Moonbow", "version": "0.2.0", "date": "2026-12-01",
//              "page": "https://github.com/.../releases/tag/v0.2.0",
//              "setup": {"name": "...-setup.exe", "size": 48670126, "sha256": "<hex>", "urls": ["https://..."]}}
//
// The payload is signed byte for byte with ECDSA P-256 over SHA-256; Moonbow trusts the keys in
// kUpdateKeys (UpdateFeed.cpp) only. A feed with a bad signature is ignored as a whole, so neither
// GitHub nor the mirror can make Moonbow download and run anything else. The downloaded setup is
// then checked against the manifest's size and SHA-256 before it runs.
//
// No UI and no network here (app/Updater does that): this part is shared with the unit tests.

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace setup {

struct UpdateInfo {
    std::wstring version;            // "0.2.0"
    std::wstring date;               // "2026-12-01"
    std::wstring page;               // the release page, for "What's new"
    std::wstring setupName;          // "Moonbow-windows-x64-setup.exe"
    uint64_t setupSize = 0;
    std::string setupSha256;         // 64 lower-case hex digits
    std::vector<std::wstring> urls;  // https only, tried in order
};

// Checks the signature against Moonbow's keys and reads the manifest. False with a short reason in
// `error` (for the log) when the feed is malformed, unsigned or signed by another key.
bool ParseUpdateFeed(std::string_view feed, UpdateInfo* out, std::string* error);
// The same with explicit public keys (128 hex digits: X then Y), for tests.
bool ParseUpdateFeed(std::string_view feed, const std::vector<std::string_view>& keys, UpdateInfo* out,
                     std::string* error);

// ECDSA P-256 / SHA-256: `signature` is r|s (64 bytes), `publicKeyHex` X|Y in hex.
bool VerifySignature(std::string_view data, std::string_view signature, std::string_view publicKeyHex);

// SHA-256 in lower-case hex; FileSha256 is empty when the file cannot be read.
std::string Sha256Hex(std::string_view data);
std::string FileSha256(const std::wstring& path);

// The downloaded setup is the one the manifest describes (size, then SHA-256).
bool SetupMatches(const std::wstring& path, const UpdateInfo& info);

// Where Moonbow looks for the feed, in order: GitHub's latest release, then the mirror.
// MOONBOW_UPDATE_FEED (one URL) replaces the list, for tests and for trying a feed before a release.
std::vector<std::wstring> UpdateFeedUrls();

}  // namespace setup
