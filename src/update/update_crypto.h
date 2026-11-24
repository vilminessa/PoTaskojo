#pragma once
// ECDSA P-256 over SHA-256, through CNG, for the update manifest.
//
// Verification only. The private half never appears in this repository - it
// lives in CI and signs the manifest at release time - so the code that runs on
// a user's machine has no way to produce a signature, only to check one. That
// is also why there is nothing here to test with a generated key: the tests
// use fixed vectors produced by the release key.

#include <string>

namespace update::crypto {

// The release key's public half as a CNG blob (magic, key size, X, Y),
// hex-encoded. Rotating the key means replacing this constant and the CI
// secret together; an old binary keeps verifying against the old key.
inline constexpr const char* kPublicKeyHex =
    "4543533120000000322a1cabb062d51ff048e3afc6b8034177458fb2957d62a7"
    "0d62d0ce44dc8e2d908bb4f69056dd1036edb4cdd3e6bbb4f92a436287c30f52"
    "e9550abafa67afa5";

// |signature_hex| is the raw r||s pair (64 bytes for P-256), hex-encoded.
// False for a malformed key, a malformed signature, or a mismatch.
bool verify(const std::string& message, const std::string& signature_hex);

}  // namespace update::crypto
