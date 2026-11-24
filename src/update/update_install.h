#pragma once
// Putting a verified download in place of the running executable.
//
// Windows will not let a file that is mapped as an image be overwritten, but it
// will let it be renamed. So the swap is: write the new file beside the old
// one, rename the running executable out of the way, rename the new file into
// its place. Every step that can fail has a way back.
//
// Staging and starting are separate on purpose. Starting the replacement while
// this process is still shutting down would leave two instances running the
// rule engine and fighting over the settings file, so the new image is started
// only once the shutdown sequence is done.

#include <string>

namespace update::install {

// Where this process's own image lives, and the sibling names used during a
// swap. Exposed for the tests, which need to predict them.
std::wstring exe_path();
std::wstring sibling(const std::wstring& suffix);

// Removes <exe>.old left behind by the previous swap. The old image is no
// longer mapped once its process has exited, so this normally succeeds on the
// first attempt after a restart; if it does not, it is retried at the next
// start rather than treated as an error.
void cleanup_previous();

struct Result {
  bool ok = false;
  std::string error;
};

// Downloads |url|, checks the result against |expected_sha256| and swaps it in
// for this executable. Does not start anything: see restart_if_staged().
Result stage(const std::string& url, const std::string& expected_sha256);

// Starts the replacement image, if stage() put one in place. Does nothing on
// every other call path, so it is safe to invoke unconditionally at the end of
// the shutdown sequence.
void restart_if_staged();

}  // namespace update::install
