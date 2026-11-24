#include "update/update_check.h"

#include <windows.h>
#include <winhttp.h>

#include <vector>

#include "update/update_crypto.h"

#pragma comment(lib, "winhttp.lib")

namespace update {
namespace {

// One place to convert, because every failure path below has to produce text a
// person can read rather than a bare error code.
std::string describe(DWORD code) {
  switch (code) {
    case ERROR_WINHTTP_TIMEOUT:
      return "the request timed out";
    case ERROR_WINHTTP_CANNOT_CONNECT:
      return "could not connect";
    case ERROR_WINHTTP_NAME_NOT_RESOLVED:
      return "the host name could not be resolved";
    case ERROR_WINHTTP_SECURE_FAILURE:
      return "the secure connection failed";
    case ERROR_WINHTTP_AUTO_PROXY_SERVICE_ERROR:
    case ERROR_WINHTTP_BAD_AUTO_PROXY_SCRIPT:
      return "the proxy configuration failed";
    default:
      break;
  }
  return "Windows error " + std::to_string(code);
}

// Splits a URL the way WinHTTP wants it: host and object path separately.
bool split_url(const std::string& url, std::wstring& host_out,
               std::wstring& path_out, bool& secure_out) {
  std::string host;
  std::string path;
  if (url.rfind("https://", 0) == 0) {
    secure_out = true;
    host = url.substr(8);
  } else if (url.rfind("http://", 0) == 0) {
    secure_out = false;
    host = url.substr(7);
  } else {
    return false;
  }
  const auto slash = host.find('/');
  if (slash == std::string::npos || slash == 0) return false;
  path = host.substr(slash);
  host = host.substr(0, slash);
  if (host.empty()) return false;

  host_out.assign(host.begin(), host.end());
  path_out.assign(path.begin(), path.end());
  return true;
}

struct Handle {
  HINTERNET h = nullptr;
  explicit Handle(HINTERNET handle) : h(handle) {}
  Handle(const Handle&) = delete;
  Handle& operator=(const Handle&) = delete;
  ~Handle() {
    if (h) WinHttpCloseHandle(h);
  }
};

}  // namespace

bool http_get(const std::string& url, std::string& body_out,
              std::string& error_out) {
  body_out.clear();
  error_out.clear();

  std::wstring host;
  std::wstring path;
  bool secure = false;
  if (!split_url(url, host, path, secure)) {
    error_out = "the address is not a valid http url";
    return false;
  }

  Handle session(WinHttpOpen(L"PoTaskojo update check",
                             WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                             WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
  if (!session.h) {
    error_out = describe(GetLastError());
    return false;
  }
  // Bounded so a dead network cannot park the checking thread indefinitely.
  WinHttpSetTimeouts(session.h, 5000, 5000, 15000, 15000);

  Handle conn(WinHttpConnect(session.h, host.c_str(),
                             secure ? INTERNET_DEFAULT_HTTPS_PORT
                                    : INTERNET_DEFAULT_HTTP_PORT,
                             0));
  if (!conn.h) {
    error_out = describe(GetLastError());
    return false;
  }

  Handle request(WinHttpOpenRequest(
      conn.h, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER,
      WINHTTP_DEFAULT_ACCEPT_TYPES,
      secure ? WINHTTP_FLAG_SECURE : 0));
  if (!request.h) {
    error_out = describe(GetLastError());
    return false;
  }

  if (!WinHttpSendRequest(request.h, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                          WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
      !WinHttpReceiveResponse(request.h, nullptr)) {
    // The failure here is usually transient (a captive portal, a VPN that just
    // dropped), so it is reported as-is rather than retried silently: a retry
    // loop inside a synchronous call just delays the answer.
    error_out = describe(GetLastError());
    return false;
  }

  DWORD status = 0;
  DWORD status_size = sizeof(status);
  if (!WinHttpQueryHeaders(request.h,
                           WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                           WINHTTP_HEADER_NAME_BY_INDEX, &status, &status_size,
                           WINHTTP_NO_HEADER_INDEX)) {
    error_out = describe(GetLastError());
    return false;
  }
  if (status != 200) {
    error_out = "the server answered HTTP " + std::to_string(status);
    return false;
  }

  for (;;) {
    DWORD available = 0;
    if (!WinHttpQueryDataAvailable(request.h, &available)) {
      error_out = describe(GetLastError());
      return false;
    }
    if (available == 0) break;
    std::vector<char> chunk(available);
    DWORD read = 0;
    if (!WinHttpReadData(request.h, chunk.data(), available, &read)) {
      error_out = describe(GetLastError());
      return false;
    }
    body_out.append(chunk.data(), read);
  }
  return true;
}

CheckResult check_for_updates(const std::string& current_version) {
  CheckResult result;

  // The signature covers these exact bytes, so the manifest is kept as it
  // arrived and verified before anything is read out of it: a manifest that
  // fails to verify must never reach the parser, let alone the dialog.
  std::string manifest_text;
  std::string error;
  if (!http_get(kManifestUrl, manifest_text, error)) {
    result.error = "could not fetch the manifest: " + error;
    return result;
  }

  std::string signature;
  if (!http_get(kSignatureUrl, signature, error)) {
    result.error = "could not fetch the manifest signature: " + error;
    return result;
  }
  // The signature file is hex and nothing else; surrounding whitespace from a
  // text transfer is trimmed rather than treated as corruption.
  const auto first = signature.find_first_not_of(" \t\r\n");
  const auto last = signature.find_last_not_of(" \t\r\n");
  signature = (first == std::string::npos) ? std::string()
                                           : signature.substr(first, last - first + 1);
  if (!crypto::verify(manifest_text, signature)) {
    result.error = "the manifest signature did not check out";
    return result;
  }

  if (!parse_manifest(manifest_text, result.manifest)) {
    result.error = "the manifest could not be read";
    return result;
  }
  result.ok = true;
  result.newer = is_newer(current_version, result.manifest.version);
  return result;
}

}  // namespace update
