#pragma once
// Reaching the published manifest.
//
// The address is fixed and redirects to whatever the newest release is, so the
// check needs no API call, no token and no rate-limit budget - and it works for
// a build that was downloaded as a plain file with no repository access at all.

#include <string>

#include "update/update_format.h"

namespace update {

// Where the signed manifest lives. "/releases/latest/download/" is answered by
// GitHub with a redirect to the current release, so this address never goes
// stale between releases.
inline constexpr const char* kManifestUrl =
    "https://github.com/vilminessa/PoTaskojo/releases/latest/download/"
    "update.json";
inline constexpr const char* kSignatureUrl =
    "https://github.com/vilminessa/PoTaskojo/releases/latest/download/"
    "update.json.sig";

struct CheckResult {
  bool ok = false;     // the manifest arrived and parsed
  bool newer = false;  // it names a version past ours
  Manifest manifest;
  std::string error;   // filled when |ok| is false, for showing to a person
};

// Fetches |url| and returns its body. False with a human-readable reason on
// anything from "no network" to "the server said 404".
bool http_get(const std::string& url, std::string& body_out,
              std::string& error_out);

// Fetches and parses the manifest, then compares it with |current_version|.
// A missing manifest is an error, never "no update": silence must not be
// indistinguishable from "you are up to date".
CheckResult check_for_updates(const std::string& current_version);

}  // namespace update
