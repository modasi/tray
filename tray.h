#ifndef TRAY_H
#define TRAY_H

struct tray_menu;

struct tray {
  char *icon;
  char *tooltip;
  struct tray_menu *menu;
};

struct tray_menu {
  char *icon;
  char *text; /* label */
  int flags;

  void (*cb)(struct tray_menu *);
  void *context;
};

static void tray_update(struct tray *tray);

#if defined(TRAY_APPINDICATOR)

#include <gtk/gtk.h>
#include <libappindicator/app-indicator.h>

#define TRAY_APPINDICATOR_ID "tray-id"

static AppIndicator *indicator = NULL;
static int loop_result = 0;

static void _tray_menu_cb(GtkMenuItem *item, gpointer data) {
  struct tray_menu *m = (struct tray_menu *)data;
  m->cb(m);
}

static int tray_init(struct tray *tray) {
  if (gtk_init_check(0, NULL) == FALSE) {
    return -1;
  }
  indicator = app_indicator_new(TRAY_APPINDICATOR_ID, tray->icon,
                                APP_INDICATOR_CATEGORY_APPLICATION_STATUS);
  app_indicator_set_status(indicator, APP_INDICATOR_STATUS_ACTIVE);
  tray_update(tray);
  return 0;
}

static int tray_loop(int blocking) {
  gtk_main_iteration_do(blocking);
  return loop_result;
}

static void tray_update(struct tray *tray) {
  struct tray_menu *m;

  app_indicator_set_icon(indicator, tray->icon);
  GtkMenuShell *gtk_menu = (GtkMenuShell *)gtk_menu_new();
  for (struct tray_menu *m = tray->menu; m != NULL && m->text != NULL; m++) {
    GtkWidget *item = gtk_menu_item_new_with_label(m->text);
    gtk_widget_show(item);
    gtk_menu_shell_append(GTK_MENU_SHELL(gtk_menu), item);
    if (m->cb != NULL) {
      g_signal_connect(item, "activate", G_CALLBACK(_tray_menu_cb), m);
    }
  }
  app_indicator_set_menu(indicator, GTK_MENU(gtk_menu));
}

static void tray_exit() { loop_result = -1; }

#elif defined(TRAY_APPKIT)

#import <Cocoa/Cocoa.h>

static NSAutoreleasePool *pool;
static NSStatusBar *statusBar;
static id statusItem;
static id statusBarButton;

@interface Tray : NSObject <NSApplicationDelegate>
- (void)menuCallback:(id)sender;
@end
@implementation Tray
- (void)menuCallback:(id)sender {
  struct tray_menu *m =
      (struct tray_menu *)[[sender representedObject] pointerValue];
  m->cb(m);
}
@end

static int tray_init(struct tray *tray) {
  pool = [NSAutoreleasePool new];
  [NSApplication sharedApplication];

  Tray *trayDelegate = [Tray new];
  [NSApp setDelegate:trayDelegate];

  statusBar = [NSStatusBar systemStatusBar];
  statusItem = [statusBar statusItemWithLength:NSVariableStatusItemLength];
  [statusItem retain];
  [statusItem setHighlightMode:YES];
  statusBarButton = [statusItem button];

  tray_update(tray);
  [NSApp activateIgnoringOtherApps:YES];
  return -1;
}

static int tray_loop(int blocking) {
  NSEvent *event;
  NSDate *until = (blocking ? [NSDate distantFuture] : [NSDate distantPast]);
  event = [NSApp nextEventMatchingMask:NSAnyEventMask
                             untilDate:until
                                inMode:NSDefaultRunLoopMode
                               dequeue:YES];
  if (event) {
    [NSApp sendEvent:event];
  }
  return 0;
}

static void tray_update(struct tray *tray) {
  [statusBarButton setImage:[NSImage imageNamed:@"icon.png"]];

  NSMenu *menu = [NSMenu new];
  [menu autorelease];
  [menu setAutoenablesItems:NO];
  for (struct tray_menu *m = tray->menu; m != NULL && m->text != NULL; m++) {
    NSMenuItem *menuItem = [NSMenuItem alloc];
    [menuItem autorelease];
    [menuItem initWithTitle:[NSString stringWithUTF8String:m->text]
                     action:@selector(menuCallback:)
              keyEquivalent:@""];
    [menuItem setEnabled:YES];
    [menuItem setRepresentedObject:[NSValue valueWithPointer:m]];

    [menu addItem:menuItem];

    //[menu addItem:[NSMenuItem separatorItem]];
  }

  [statusItem setMenu:menu];
}

static void tray_exit() { [NSApp terminate:NSApp]; }

#elif defined(TRAY_WINAPI)
#include <shellapi.h>
#include <windows.h>

#define WM_TRAY_CALLBACK_MESSAGE (WM_USER + 1)
#define WC_TRAY_CLASS_NAME "TRAY"
#define ID_TRAY_FIRST 1000

static WNDCLASSEX wc;
static NOTIFYICONDATA nid;
static HWND hwnd;
static HMENU hmenu;

static LRESULT CALLBACK _tray_wnd_proc(HWND hwnd, UINT msg, WPARAM wparam,
                                       LPARAM lparam) {
  switch (msg) {
  case WM_CLOSE:
    DestroyWindow(hwnd);
    return 0;
  case WM_DESTROY:
    PostQuitMessage(0);
    return 0;
  case WM_TRAY_CALLBACK_MESSAGE:
    if (lparam == WM_LBUTTONUP || lparam == WM_RBUTTONUP) {
      POINT p;
      GetCursorPos(&p);
      WORD cmd = TrackPopupMenu(hmenu, TPM_LEFTALIGN | TPM_RIGHTBUTTON |
                                           TPM_RETURNCMD | TPM_NONOTIFY,
                                p.x, p.y, 0, hwnd, NULL);
      SendMessage(hwnd, WM_COMMAND, cmd, 0);
      return 0;
    }
    break;
  case WM_COMMAND:
    if (wparam >= ID_TRAY_FIRST) {
      MENUITEMINFO item = {
          .cbSize = sizeof(MENUITEMINFO), .fMask = MIIM_ID | MIIM_DATA,
      };
      if (GetMenuItemInfo(hmenu, wparam, FALSE, &item)) {
        struct tray_menu *menu = (struct tray_menu *)item.dwItemData;
        menu->cb(menu->context);
      }
      return 0;
    }
    break;
  }
  return DefWindowProc(hwnd, msg, wparam, lparam);
}

static int tray_init(struct tray *tray) {
  memset(&wc, 0, sizeof(wc));
  wc.cbSize = sizeof(WNDCLASSEX);
  wc.lpfnWndProc = _tray_wnd_proc;
  wc.hInstance = GetModuleHandle(NULL);
  wc.lpszClassName = WC_TRAY_CLASS_NAME;
  if (!RegisterClassEx(&wc)) {
    return -1;
  }

  hwnd = CreateWindowEx(0, WC_TRAY_CLASS_NAME, NULL, 0, 0, 0, 0, 0, 0, 0, 0, 0);
  if (hwnd == NULL) {
    return -1;
  }
  UpdateWindow(hwnd);

  memset(&nid, 0, sizeof(nid));
  nid.cbSize = sizeof(NOTIFYICONDATA);
  nid.hWnd = hwnd;
  nid.uID = 0;
  nid.uFlags = NIF_ICON | NIF_MESSAGE;
  nid.uCallbackMessage = WM_TRAY_CALLBACK_MESSAGE;
  Shell_NotifyIcon(NIM_ADD, &nid);

  tray_update(tray);
}

static int tray_loop(int blocking) {
  MSG msg;
  if (GetMessage(&msg, NULL, 0, 0)) {
    TranslateMessage(&msg);
    DispatchMessage(&msg);
    return 0;
  }
  return -1;
}

static void tray_update(struct tray *tray) {
  int i = 0;
  hmenu = CreatePopupMenu();
  for (struct tray_menu *m = tray->menu; m != NULL && m->text != NULL; m++) {
    MENUITEMINFO *item = (MENUITEMINFO *)malloc(sizeof(MENUITEMINFO));
    item->cbSize = sizeof(MENUITEMINFO);
    item->fMask = MIIM_ID | MIIM_TYPE | MIIM_STATE | MIIM_DATA;
    item->fType = 0;
    item->fState = 0;
    item->wID = i + ID_TRAY_FIRST;
    item->dwTypeData = m->text;
    item->dwItemData = (ULONG_PTR)m;

    InsertMenuItem(hmenu, i, TRUE, item);
    i++;
  }
  SendMessage(hwnd, WM_INITMENUPOPUP, (WPARAM)hmenu, 0);
  ExtractIconEx(tray->icon, 0, NULL, &(nid.hIcon), 1);
  Shell_NotifyIcon(NIM_MODIFY, &nid);
}

static void tray_exit() {
  Shell_NotifyIcon(NIM_DELETE, &nid);
  if (nid.hIcon != 0) {
    DestroyIcon(nid.hIcon);
  }
  if (hmenu != 0) {
    DestroyMenu(hmenu);
  }
  PostQuitMessage(0);
  UnregisterClass(WC_TRAY_CLASS_NAME, GetModuleHandle(NULL));
}
#else
#endif

#endif /* TRAY_H */
