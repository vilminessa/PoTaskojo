#pragma once
// A throwaway child process, for the tests that need a live target to act on.
// Spawns a ping that would run for a minute and is killed either by the test
// or by the destructor.

#include <windows.h>

#include <cstdint>

class Child {
 public:
  Child() = default;
  Child(const Child&) = delete;
  Child& operator=(const Child&) = delete;

  ~Child() {
    if (pi_.hProcess) {
      TerminateProcess(pi_.hProcess, 0);
      WaitForSingleObject(pi_.hProcess, 5000);
      CloseHandle(pi_.hProcess);
    }
    if (pi_.hThread) CloseHandle(pi_.hThread);
  }

  bool start() {
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    wchar_t cmd[] = L"ping.exe -n 60 127.0.0.1";
    return CreateProcessW(nullptr, cmd, nullptr, nullptr, FALSE,
                          CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi_) !=
           FALSE;
  }

  uint32_t pid() const { return pi_.dwProcessId; }
  HANDLE handle() const { return pi_.hProcess; }

 private:
  PROCESS_INFORMATION pi_{};
};
