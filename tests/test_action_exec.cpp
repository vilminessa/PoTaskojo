// Access rights cannot be checked without a live process: OpenProcess
// succeeds even when the resulting handle is too weak, and the failure only
// surfaces at TerminateProcess or NtSuspendProcess. The rules engine used to
// ask for too little and its start actions quietly did nothing, so these
// cases spawn a throwaway child and prove the actions take effect.

#include <windows.h>

#include "rules/action_exec.h"
#include "test_process.h"
#include "test_support.h"

TEST_CASE(action_end_terminates_a_live_process) {
  Child child;
  CHECK(child.start());
  if (!child.handle()) return;

  CHECK(rules::end_process(child.pid()));
  CHECK_EQ(WaitForSingleObject(child.handle(), 5000),
           static_cast<DWORD>(WAIT_OBJECT_0));
}

TEST_CASE(action_suspend_and_resume_a_live_process) {
  Child child;
  CHECK(child.start());
  if (!child.handle()) return;

  CHECK(rules::suspend_process(child.pid()));
  CHECK(rules::resume_process(child.pid()));

  // Suspended and resumed, so it must still be running.
  DWORD code = 0;
  CHECK(GetExitCodeProcess(child.handle(), &code));
  CHECK_EQ(code, static_cast<DWORD>(STILL_ACTIVE));
}

TEST_CASE(action_refuses_a_pid_that_cannot_be_opened) {
  CHECK(!rules::end_process(0));
  CHECK(!rules::suspend_process(0));
  CHECK(!rules::resume_process(0));
}
