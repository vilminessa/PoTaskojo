#include "update/update_crypto.h"

#include <windows.h>
#include <bcrypt.h>

#include <vector>

namespace update::crypto {
namespace {

std::vector<uint8_t> from_hex(const std::string& hex) {
  std::vector<uint8_t> out;
  if (hex.size() % 2 != 0) return {};
  auto nibble = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
  };
  out.reserve(hex.size() / 2);
  for (size_t i = 0; i < hex.size(); i += 2) {
    const int hi = nibble(hex[i]);
    const int lo = nibble(hex[i + 1]);
    if (hi < 0 || lo < 0) return {};
    out.push_back(static_cast<uint8_t>((hi << 4) | lo));
  }
  return out;
}

// SHA-256 of |message| as CNG wants it. The provider and the hash object are
// both opened here and released on the way out, because the caller is on the
// update path and a leak there would persist for the life of the process.
bool sha256(const std::string& message, std::vector<uint8_t>& out) {
  BCRYPT_ALG_HANDLE sha = nullptr;
  if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&sha,
                                                  BCRYPT_SHA256_ALGORITHM,
                                                  nullptr, 0)))
    return false;

  bool ok = false;
  BCRYPT_HASH_HANDLE hash = nullptr;
  ULONG object_len = 0;
  ULONG written = 0;
  // The byte-count out-parameter is not optional in practice: passing null for
  // it makes CNG answer STATUS_INVALID_PARAMETER and the whole hash silently
  // fails, which is a miserable thing to track down from "verification failed".
  if (BCRYPT_SUCCESS(BCryptGetProperty(sha, BCRYPT_OBJECT_LENGTH,
                                       reinterpret_cast<PUCHAR>(&object_len),
                                       sizeof(object_len), &written, 0))) {
    std::vector<uint8_t> object(object_len);
    if (BCRYPT_SUCCESS(BCryptCreateHash(sha, &hash, object.data(), object_len,
                                        nullptr, 0, 0))) {
      const std::vector<uint8_t> data(message.begin(), message.end());
      if (BCRYPT_SUCCESS(BCryptHashData(hash,
                                        const_cast<PUCHAR>(data.data()),
                                        static_cast<ULONG>(data.size()), 0))) {
        out.resize(32);
        ok = BCRYPT_SUCCESS(
            BCryptFinishHash(hash, out.data(), static_cast<ULONG>(out.size()),
                             0));
      }
    }
  }
  if (hash) BCryptDestroyHash(hash);
  BCryptCloseAlgorithmProvider(sha, 0);
  return ok;
}

}  // namespace

bool verify(const std::string& message, const std::string& signature_hex) {
  const std::vector<uint8_t> public_blob = from_hex(kPublicKeyHex);
  const std::vector<uint8_t> signature = from_hex(signature_hex);
  if (public_blob.empty() || signature.empty()) return false;

  BCRYPT_ALG_HANDLE ecc = nullptr;
  if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&ecc,
                                                  BCRYPT_ECDSA_P256_ALGORITHM,
                                                  nullptr, 0)))
    return false;

  bool ok = false;
  BCRYPT_KEY_HANDLE key = nullptr;
  if (BCRYPT_SUCCESS(BCryptImportKeyPair(
          ecc, nullptr, BCRYPT_ECCPUBLIC_BLOB, &key,
          const_cast<PUCHAR>(public_blob.data()),
          static_cast<ULONG>(public_blob.size()), 0))) {
    std::vector<uint8_t> digest;
    if (sha256(message, digest)) {
      // A null padding info is what ECDSA wants: the digest is the message
      // representative as it stands, with nothing wrapped around it.
      ok = BCRYPT_SUCCESS(BCryptVerifySignature(
          key, nullptr, digest.data(), static_cast<ULONG>(digest.size()),
          const_cast<PUCHAR>(signature.data()),
          static_cast<ULONG>(signature.size()), 0));
    }
  }
  if (key) BCryptDestroyKey(key);
  BCryptCloseAlgorithmProvider(ecc, 0);
  return ok;
}

}  // namespace update::crypto
