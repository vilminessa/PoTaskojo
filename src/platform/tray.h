#pragma once
// System tray icon: show/hide, context menu, close-to-tray behaviour.

#include <windows.h>

namespace platform {

// Message posted to the main window for tray events.
constexpr UINT kTrayCallbackMsg = WM_APP + 1;

// Adds the tray icon. hwnd receives kTrayCallbackMsg.
bool tray_add(HWND hwnd);
void tray_remove();
void tray_set_tooltip(const wchar_t* text);

// Shows the standard tray context menu at the cursor.
// Returns the chosen command id (see TrayCommand) or 0.
enum TrayCommand : UINT {
  TrayNone = 0,
  TrayShow = 1,
  TrayExit = 2,
};
UINT tray_show_menu(HWND hwnd);

// Puts a balloon over the icon. Windows replaces a balloon that is still
// showing, so the caller decides how often it is worth interrupting for -
// this only does the plumbing.
void tray_balloon(const wchar_t* title, const wchar_t* body, bool warning);

}  // namespace platform
