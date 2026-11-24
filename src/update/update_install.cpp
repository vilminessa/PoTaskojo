#include "update/update_install.h"

#include <windows.h>
#include <bcrypt.h>

#include <fstream>
#include <vector>

#include "core/log.h"
#include "update/update_check.h"

namespace update::install {
namespace {

std::string lower(std::string s) {
  for (char& c : s)
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
  return s;
}

std::string error_text(const char* what, DWORD code) {
  return std::string(what) + " (Windows error " + std::to_string(code) + ")";
}

// SHA-256 of a whole file, lower-case hex. Read in blocks so a large download
// does not have to fit in memory twice.
bool sha256_file(const std::wstring& path, std::string& out_hex,
                 std::string& error) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    error = "the downloaded file could not be opened";
    return false;
  }

  BCRYPT_ALG_HANDLE sha = nullptr;
  if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&sha,
                                                  BCRYPT_SHA256_ALGORITHM,
                                                  nullptr, 0))) {
    error = "the hash provider could not be opened";
    return false;
  }

  bool ok = false;
  BCRYPT_HASH_HANDLE hash = nullptr;
  ULONG object_len = 0;
  ULONG written = 0;
  // See update_crypto.cpp: the byte-count out-parameter has to be supplied or
  // CNG refuses the query and the hash never happens.
  if (BCRYPT_SUCCESS(BCryptGetProperty(sha, BCRYPT_OBJECT_LENGTH,
                                       reinterpret_cast<PUCHAR>(&object_len),
                                       sizeof(object_len), &written, 0))) {
    std::vector<uint8_t> object(object_len);
    if (BCRYPT_SUCCESS(BCryptCreateHash(sha, &hash, object.data(), object_len,
                                        nullptr, 0, 0))) {
      std::vector<char> buffer(64 * 1024);
      ok = true;
      while (in) {
        in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const std::streamsize got = in.gcount();
        if (got <= 0) break;
        if (!BCRYPT_SUCCESS(BCryptHashData(
                hash, reinterpret_cast<PUCHAR>(buffer.data()),
                static_cast<ULONG>(got), 0))) {
          ok = false;
          break;
        }
      }
      if (ok) {
        uint8_t digest[32];
        ok = BCRYPT_SUCCESS(
            BCryptFinishHash(hash, digest, sizeof(digest), 0));
        if (ok) {
          static const char* kDigits = "0123456789abcdef";
          out_hex.clear();
          out_hex.reserve(64);
          for (uint8_t b : digest) {
            out_hex += kDigits[b >> 4];
            out_hex += kDigits[b & 0x0F];
          }
        }
      }
    }
  }
  if (hash) BCryptDestroyHash(hash);
  BCryptCloseAlgorithmProvider(sha, 0);
  if (!ok) error = "the file could not be hashed";
  return ok;
}

// Downloads straight into |path|. A partial file is left behind on failure and
// overwritten by the next attempt, which is why the caller hashes it rather
// than trusting that a completed write means a complete download.
bool download_to(const std::string& url, const std::wstring& path,
                 std::string& error) {
  std::string body;
  if (!http_get(url, body, error)) return false;
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) {
    error = "the new build could not be saved next to the current one";
    return false;
  }
  out.write(body.data(), static_cast<std::streamsize>(body.size()));
  out.close();
  if (!out) {
    error = "the new build could not be saved next to the current one";
    return false;
  }
  return true;
}

bool file_exists(const std::wstring& path) {
  const DWORD attrs = GetFileAttributesW(path.c_str());
  return attrs != INVALID_FILE_ATTRIBUTES &&
         !(attrs & FILE_ATTRIBUTE_DIRECTORY);
}

}  // namespace

std::wstring exe_path() {
  std::wstring buffer(32768, L'\0');
  const DWORD len =
      GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
  buffer.resize(len);
  return buffer;
}

std::wstring sibling(const std::wstring& suffix) {
  std::wstring path = exe_path();
  const auto slash = path.find_last_of(L"\\/");
  const auto dot = path.find_last_of(L'.');
  if (dot == std::wstring::npos || (slash != std::wstring::npos && dot < slash))
    return path + suffix;
  return path.substr(0, dot) + suffix;
}

void cleanup_previous() {
  const std::wstring old_path = sibling(L".old");
  if (!file_exists(old_path)) return;
  // Deleting fails while the old image is still mapped, which means some
  // previous instance is still alive; the next start gets another chance.
  DeleteFileW(old_path.c_str());
}

// The image to launch once this process is gone. Captured during the swap,
// before anything is renamed: asking GetModuleFileNameW afterwards would risk
// reporting whatever the file happens to be called at that moment rather than
// the executable that is meant to run.
std::wstring staged_image;

Result stage(const std::string& url, const std::string& expected_sha256) {
  Result result;

  const std::wstring exe = exe_path();
  const std::wstring fresh = sibling(L".new");
  const std::wstring retired = sibling(L".old");

  if (!download_to(url, fresh, result.error)) return result;

  std::string actual;
  if (!sha256_file(fresh, actual, result.error)) return result;
  if (lower(actual) != lower(expected_sha256)) {
    result.error = "the downloaded bytes did not match the manifest";
    DeleteFileW(fresh.c_str());
    return result;
  }

  // A leftover from an earlier swap means its process has not exited yet, or
  // the previous cleanup failed. Either way replacing it now would be guessing.
  if (file_exists(retired)) {
    DeleteFileW(retired.c_str());
    if (file_exists(retired)) {
      result.error = "an earlier update is still finishing";
      DeleteFileW(fresh.c_str());
      return result;
    }
  }

  // The running image may be renamed but not overwritten, so it moves aside
  // first. From here on every failure is rolled back to a runnable program.
  if (!MoveFileExW(exe.c_str(), retired.c_str(), MOVEFILE_REPLACE_EXISTING)) {
    result.error = error_text("the running executable could not be renamed",
                              GetLastError());
    DeleteFileW(fresh.c_str());
    return result;
  }

  if (!MoveFileExW(fresh.c_str(), exe.c_str(), MOVEFILE_REPLACE_EXISTING)) {
    const DWORD code = GetLastError();
    MoveFileExW(retired.c_str(), exe.c_str(), MOVEFILE_REPLACE_EXISTING);
    DeleteFileW(fresh.c_str());
    result.error = error_text("putting the new executable in place failed", code);
    return result;
  }

  staged_image = exe;
  result.ok = true;
  return result;
}

void restart_if_staged() {
  if (staged_image.empty()) return;
  const std::wstring image = staged_image;
  staged_image.clear();

  // Started with the image path as the application name: there is no command
  // line to preserve, because the program only ever looks for a data folder
  // beside its own executable.
  STARTUPINFOW info{};
  info.cb = sizeof(info);
  PROCESS_INFORMATION process{};
  if (!CreateProcessW(image.c_str(), nullptr, nullptr, nullptr, FALSE, 0, nullptr,
                      nullptr, &info, &process)) {
    // The files are already correct, so the user can simply start the program
    // again. Nothing is rolled back at this point.
    core::log::error("update installed but the replacement could not be started: " +
                     std::to_string(GetLastError()));
    return;
  }
  CloseHandle(process.hThread);
  CloseHandle(process.hProcess);
}

}  // namespace update::install
