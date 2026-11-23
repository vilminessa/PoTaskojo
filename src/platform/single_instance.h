#pragma once
// Single-instance guard using a named mutex. Plain "one owner" semantics;
// keeping the handle past the owner's exit is NOT needed for MVP.

#include <windows.h>

#include <string>

namespace platform {

class SingleInstance {
 public:
  explicit SingleInstance(const wchar_t* name) {
    handle_ = CreateMutexW(nullptr, FALSE, name);
    owns_ = (handle_ != nullptr) && GetLastError() != ERROR_ALREADY_EXISTS;
  }
  ~SingleInstance() {
    if (handle_) CloseHandle(handle_);
  }
  SingleInstance(const SingleInstance&) = delete;
  SingleInstance& operator=(const SingleInstance&) = delete;

  bool owns() const { return owns_; }

 private:
  HANDLE handle_ = nullptr;
  bool owns_ = false;
};

}  // namespace platform
