#include "platform/tray.h"

#include <shellapi.h>

namespace platform {
namespace {

NOTIFYICONDATAW g_nid{};
bool g_added = false;

}  // namespace

bool tray_add(HWND hwnd) {
  if (g_added) return true;

  ZeroMemory(&g_nid, sizeof(g_nid));
  g_nid.cbSize = sizeof(g_nid);
  g_nid.hWnd = hwnd;
  g_nid.uID = 1;
  g_nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
  g_nid.uCallbackMessage = kTrayCallbackMsg;
  g_nid.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
  lstrcpynW(g_nid.szTip, L"PoTaskojo", ARRAYSIZE(g_nid.szTip));

  g_added = Shell_NotifyIconW(NIM_ADD, &g_nid) != FALSE;
  if (g_added) {
    g_nid.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &g_nid);
  }
  return g_added;
}

void tray_remove() {
  if (!g_added) return;
  Shell_NotifyIconW(NIM_DELETE, &g_nid);
  g_added = false;
}

void tray_set_tooltip(const wchar_t* text) {
  if (!g_added || !text) return;
  lstrcpynW(g_nid.szTip, text, ARRAYSIZE(g_nid.szTip));
  g_nid.uFlags = NIF_TIP;
  Shell_NotifyIconW(NIM_MODIFY, &g_nid);
}

UINT tray_show_menu(HWND hwnd) {
  POINT pt;
  GetCursorPos(&pt);

  HMENU menu = CreatePopupMenu();
  if (!menu) return TrayNone;
  AppendMenuW(menu, MF_STRING, TrayShow, L"Show PoTaskojo");
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(menu, MF_STRING, TrayExit, L"Exit");

  // Required so the menu closes when clicking elsewhere.
  SetForegroundWindow(hwnd);
  const UINT cmd = static_cast<UINT>(
      TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY, pt.x, pt.y, 0, hwnd,
                     nullptr));
  DestroyMenu(menu);
  PostMessageW(hwnd, WM_NULL, 0, 0);  // work around tray menu quirk
  return cmd;
}

}  // namespace platform
