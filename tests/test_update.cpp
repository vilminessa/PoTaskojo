// The update path decides whether to replace the running executable, so the
// three things it rests on are covered here: reading the manifest, telling a
// newer version from an older one, and accepting a signature only from the
// release key.
//
// The signature vectors were produced by the release key itself. Tests cannot
// make their own signatures - verification-only code has no private key - which
// is deliberate: a test that could sign would not prove the shipped public key
// is the right one.

#include <string>

#include "update/update_crypto.h"
#include "update/update_format.h"
#include "update/update_install.h"
#include "test_support.h"

namespace {

// Byte-for-byte what the release signs, LF line endings included.
const std::string kManifest =
    "potaskojo-update-manifest-v1\n"
    "version 0.0.7\n"
    "url https://github.com/vilminessa/PoTaskojo/releases/download/"
    "v0.0.7/potaskojo.exe\n"
    "sha256 0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\n";

const std::string kSignature =
    "7d54ccfa70707a4e36e954f5cc364373d22d6a68a38edefae9105d0f8edec63"
    "5d9c04ea407ad649c31ab2de78492e792372f1227447b204e4d8f0bc04a56347e";

}  // namespace

TEST_CASE(update_signature_accepts_the_release_key) {
  CHECK(update::crypto::verify(kManifest, kSignature));
}

TEST_CASE(update_signature_rejects_a_changed_manifest) {
  // A bumped version with the old signature is exactly the attack: taking a
  // legitimately signed manifest and repointing it.
  const std::string other =
      "potaskojo-update-manifest-v1\n"
      "version 9.9.9\n"
      "url https://evil.example/payload.exe\n"
      "sha256 0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\n";
  CHECK(!update::crypto::verify(other, kSignature));
}

TEST_CASE(update_signature_rejects_a_changed_signature) {
  std::string flipped = kSignature;
  flipped.back() = flipped.back() == '2' ? '3' : '2';
  CHECK(!update::crypto::verify(kManifest, flipped));

  CHECK(!update::crypto::verify(kManifest, ""));
  CHECK(!update::crypto::verify(kManifest, "not hex at all"));
  CHECK(!update::crypto::verify("", kSignature));
}

TEST_CASE(update_signature_rejects_the_wrong_public_key_shape) {
  // A signature that is the right length in the wrong encoding must not pass
  // by accident: 128 hex chars is 64 bytes, which is the correct size.
  const std::string long_enough(128, '0');
  CHECK(!update::crypto::verify(kManifest, long_enough));
}

TEST_CASE(update_manifest_parses) {
  update::Manifest m;
  CHECK(update::parse_manifest(kManifest, m));
  CHECK_EQ(m.version, std::string("0.0.7"));
  CHECK_EQ(m.sha256,
           std::string("0123456789abcdef0123456789abcdef0123456789abcdef01"
                       "23456789abcdef"));
  CHECK(m.url.rfind("https://", 0) == 0);
}

TEST_CASE(update_manifest_refuses_anything_it_does_not_expect) {
  update::Manifest m;

  // Wrong first line: this is not our manifest at all.
  CHECK(!update::parse_manifest("something-else\nversion 1.0.0\n", m));
  CHECK(!update::parse_manifest("", m));

  // Every field is required; a partial manifest must not read as "no update".
  CHECK(!update::parse_manifest("potaskojo-update-manifest-v1\nversion 1.0.0\n",
                                m));
  CHECK(!update::parse_manifest(
      "potaskojo-update-manifest-v1\n"
      "url https://example.com/a.exe\n"
      "sha256 0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\n",
      m));

  // Plain http is refused: the signature covers the content, but transport
  // security is not something to give up for free.
  CHECK(!update::parse_manifest(
      "potaskojo-update-manifest-v1\n"
      "version 1.0.0\n"
      "url http://example.com/a.exe\n"
      "sha256 0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\n",
      m));

  // A hash that is not a hash.
  CHECK(!update::parse_manifest(
      "potaskojo-update-manifest-v1\nversion 1.0.0\n"
      "url https://example.com/a.exe\nsha256 nope\n",
      m));

  // An unknown key is a format change; older clients must refuse rather than
  // silently ignore a field they were never told about.
  CHECK(!update::parse_manifest(
      "potaskojo-update-manifest-v1\nversion 1.0.0\n"
      "url https://example.com/a.exe\n"
      "sha256 0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\n"
      "channel beta\n",
      m));
}

TEST_CASE(update_version_ordering_is_numeric_not_lexical) {
  // The reason this is not a string compare: 0.0.10 must beat 0.0.9.
  CHECK(update::is_newer("0.0.9", "0.0.10"));
  CHECK(update::is_newer("0.0.5", "0.0.6"));
  CHECK(update::is_newer("1.0.0", "2.0.0"));
  CHECK(update::is_newer("0.9.0", "1.0.0"));
  CHECK(update::is_newer("1.2.3", "1.2.3.1"));

  CHECK(!update::is_newer("0.0.10", "0.0.9"));
  CHECK(!update::is_newer("2.0.0", "1.0.0"));
  CHECK(!update::is_newer("0.0.6", "0.0.6"));
}

TEST_CASE(update_swap_target_is_beside_the_running_image) {
  const std::wstring exe = update::install::exe_path();
  CHECK(!exe.empty());

  // Every name used by the swap shares the image's directory: a download that
  // landed elsewhere could not be renamed over the executable.
  const std::wstring fresh = update::install::sibling(L".new");
  const std::wstring retired = update::install::sibling(L".old");
  CHECK(fresh != exe);
  CHECK(retired != exe);
  CHECK_EQ(fresh.substr(fresh.size() - 4), std::wstring(L".new"));
  CHECK_EQ(retired.substr(retired.size() - 4), std::wstring(L".old"));

  const auto strip = [](std::wstring s) {
    const auto dot = s.find_last_of(L'.');
    return dot == std::wstring::npos ? s : s.substr(0, dot);
  };
  CHECK_EQ(strip(fresh), strip(exe));
  CHECK_EQ(strip(retired), strip(exe));
}
