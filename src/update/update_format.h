#pragma once
// The update manifest: a small line-oriented file whose exact bytes are what
// the signature covers.
//
// Signing the file as it stands, rather than a re-serialised form of it, means
// there is no canonicalisation step to get wrong - the classic source of
// "the signature does not verify and nobody knows why". The first line names
// the purpose, so a signature issued for this file cannot be replayed against
// anything else.

#include <string>

namespace update {

inline constexpr const char* kDomain = "potaskojo-update-manifest-v1";

struct Manifest {
  std::string version;  // dotted numbers, e.g. "0.0.7"
  std::string url;      // https://... where the executable lives
  std::string sha256;   // lower-case hex, 64 chars, of that executable
};

// Parses manifest bytes. False on anything unexpected: a failed parse must
// never be read as "no update available". Unknown keys are refused rather than
// ignored, so adding one means bumping kDomain instead of silently changing
// what older clients accept.
bool parse_manifest(const std::string& text, Manifest& out);

// True when |candidate| is a later version than |current|, comparing each
// dotted component numerically so 0.0.10 beats 0.0.9.
bool is_newer(const std::string& current, const std::string& candidate);

}  // namespace update
