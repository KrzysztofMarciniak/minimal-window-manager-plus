#include <X11/XF86keysym.h>
#include <X11/XKBlib.h>
#include <X11/Xlib.h>
#include <X11/cursorfont.h>
#include <X11/keysym.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <unistd.h>

static int resizeDelta = 0;
#define RESIZE_STEP 50
#define MAX_DESKTOPS 9
#define MAX_WINDOWS_PER_DESKTOP 6
#define MOD_KEY Mod4Mask

typedef struct {
  Window windows[MAX_WINDOWS_PER_DESKTOP];
  unsigned char windowCount;
  unsigned char focusedIdx;
  bool isMapped[MAX_WINDOWS_PER_DESKTOP];
} Desktop;

typedef struct {
  KeySym keysym;
  const char *command;
} AppLauncher;

static Display *dpy;
static Window root;
static Desktop desktops[MAX_DESKTOPS];
static int currentDesktop            = 0;
static volatile sig_atomic_t running = 1;
static int screen_width, screen_height;

static const AppLauncher launchers[] = {{XK_Return, "st"},
                                        {XK_p,
                                         "dmenu_run -m '0' -nb '#000000' -nf '#ffffff' -sb "
                                         "'#ffffff' -sf '#000000'"},
                                         {XF86XK_AudioRaiseVolume, AUDIO_SCRIPT " +"},
                                         {XF86XK_AudioLowerVolume, AUDIO_SCRIPT " -"},
                                         {XF86XK_AudioMicMute, AUDIO_SCRIPT " mic"},
                                         {XF86XK_AudioMute, AUDIO_SCRIPT " aud"}};

static void setup(void);
static void run(void);
static void cleanup(void);
static void handleKeyPress(XEvent *e);
static void handleMapRequest(XEvent *e);
static void handleUnmapNotify(XEvent *e);
static void handleDestroyNotify(XEvent *e);
static void handleConfigureNotify(XEvent *e);
inline static void focusWindow(Window w);
static void tileWindows(void);
static void switchDesktop(int desktop);
static void moveWindowToDesktop(Window win, unsigned char desktop);
static void grabKeys(void);
static void sigHandler(int);
static int xerrorstart(Display *, XErrorEvent *);
static int xerror(Display *, XErrorEvent *);
static void killFocusedWindow(void);
static void focusCycleWindow(int);
static void handleMapNotify(XEvent *e);
inline static void die(const char *msg);
static bool handleWindowRemoval(Window window);
static inline int detachWindow(Window w, Window *windows, unsigned char *windowCount,
                               unsigned char *focusedIdx);

int main(void) {
  signal(SIGTERM, sigHandler);
  signal(SIGINT, sigHandler);
  setup();
  run();
  cleanup();
}

inline static void die(const char *msg) {
  fprintf(stderr, "mwm: %s\n", msg);
  exit(EXIT_FAILURE);
}

static void sigHandler(int sig) {
  (void)sig;
  running = 0;
}

static int xerrorstart(Display *dpy __attribute__((unused)),
                       XErrorEvent *ee __attribute__((unused))) {
  die("another window manager is already running");
  return -1;
}

static int xerror(Display *dpy, XErrorEvent *ee) {
  (void)dpy;
  (void)ee;
  return 0;
}

static void setup(void) {
  if (!getenv("DISPLAY")) die("DISPLAY not set");
  if (!(dpy = XOpenDisplay(NULL))) die("cannot open display");

  root = DefaultRootWindow(dpy);

  memset(desktops, 0, sizeof(desktops));

  XWindowAttributes attr;
  XGetWindowAttributes(dpy, root, &attr);
  screen_width  = attr.width;
  screen_height = attr.height;

  XSetErrorHandler(xerrorstart);
  XSelectInput(dpy, root, SubstructureRedirectMask | SubstructureNotifyMask | StructureNotifyMask);
  Cursor cursor = XCreateFontCursor(dpy, XC_left_ptr);
  XDefineCursor(dpy, root, cursor);
  XSync(dpy, False);
  XSetErrorHandler(xerror);

  grabKeys();
  XSync(dpy, False);
}

static void grabKeys(void) {
  const unsigned int modifiers[] = {MOD_KEY, MOD_KEY | ShiftMask};
  const int modCount             = sizeof(modifiers) / sizeof(modifiers[0]);
  KeyCode keycodes[40];
  int k = 0;

  for (int i = 0; i < MAX_DESKTOPS; i++) {
    keycodes[k++] = XKeysymToKeycode(dpy, XK_1 + i);
  }

  KeySym specialKeys[] = {XK_q, XK_j, XK_l, XK_h};

  for (size_t i = 0; i < sizeof(specialKeys) / sizeof(specialKeys[0]); i++) {
    keycodes[k++] = XKeysymToKeycode(dpy, specialKeys[i]);
  }

  for (int i = 0; i < k; i++) {
    if (keycodes[i] == 0) continue;
    for (int j = 0; j < modCount; j++) {
      XGrabKey(dpy, keycodes[i], modifiers[j], root, True, GrabModeAsync, GrabModeAsync);
    }
  }

  const size_t launcherCount = sizeof(launchers) / sizeof(launchers[0]);
  for (size_t i = 0; i < launcherCount; i++) {
    KeyCode code = XKeysymToKeycode(dpy, launchers[i].keysym);
    if (code != 0) {
      XGrabKey(dpy, code, MOD_KEY, root, True, GrabModeAsync, GrabModeAsync);
    }
  }
}

static void run(void) {
  XEvent e;
  fd_set fds;
  int xfd = ConnectionNumber(dpy);

  while (running) {
    while (XPending(dpy)) {
      XNextEvent(dpy, &e);
      switch (e.type) {
        case KeyPress:
          handleKeyPress(&e);
          break;
        case MapRequest:
          handleMapRequest(&e);
          break;
        case MapNotify:
          handleMapNotify(&e);
          break;
        case UnmapNotify:
          handleUnmapNotify(&e);
          break;
        case DestroyNotify:
          handleDestroyNotify(&e);
          break;
        case ConfigureNotify:
          handleConfigureNotify(&e);
          break;
      }
    }

    FD_ZERO(&fds);
    FD_SET(xfd, &fds);

    if (select(xfd + 1, &fds, NULL, NULL, NULL) == -1 && errno != EINTR) break;
  }
}

static void handleConfigureNotify(XEvent *e) {
  XConfigureEvent *ev = &e->xconfigure;

  if (ev->window == root) {
    screen_width  = ev->width;
    screen_height = ev->height;
    tileWindows();
  }
}

static void killFocusedWindow(void) {
  Desktop *d = &desktops[currentDesktop];
  if (d->windowCount <= 0) return;

  Window win = d->windows[d->focusedIdx];
  if (win == None || win == root) return;

  Atom *p;
  int n;
  Atom del = XInternAtom(dpy, "WM_DELETE_WINDOW", False);

  if (XGetWMProtocols(dpy, win, &p, &n)) {
    while (n--)
      if (p[n] == del) goto send;
    XKillClient(dpy, win);
    XFree(p);
    return;
  }
  XEvent ev;
send:
  memset(&ev, 0, sizeof(ev));
  ev.type                 = ClientMessage;
  ev.xclient.window       = win;
  ev.xclient.message_type = XInternAtom(dpy, "WM_PROTOCOLS", False);
  ev.xclient.format       = 32;
  ev.xclient.data.l[0]    = del;
  ev.xclient.data.l[1]    = CurrentTime;
  XSendEvent(dpy, win, False, NoEventMask, &ev);
  XFree(p);
  XFlush(dpy);
}

static void focusCycleWindow(int direction) {
  Desktop *d = &desktops[currentDesktop];
  if (d->windowCount > 1) {
    d->focusedIdx = (d->focusedIdx + direction + d->windowCount) % d->windowCount;
    focusWindow(d->windows[d->focusedIdx]);
  }
}

static void handleKeyPress(XEvent *e) {
  XKeyEvent *ev      = &e->xkey;
  KeySym keysym      = XkbKeycodeToKeysym(dpy, ev->keycode, 0, 0);
  unsigned int state = ev->state;

  if (keysym == XK_q && state == (MOD_KEY | ShiftMask)) {
    running = 0;
    return;
  }

  if ((keysym == XK_h || keysym == XK_l) && state == (MOD_KEY | ShiftMask)) {
    resizeDelta += (keysym == XK_l) ? RESIZE_STEP : -RESIZE_STEP;
    tileWindows();
    return;
  }

  if (keysym == XK_q && state == MOD_KEY) {
    killFocusedWindow();
    return;
  }

  if ((keysym == XK_j || keysym == XK_l) && state == MOD_KEY) {
    focusCycleWindow(keysym == XK_j ? 1 : -1);
    return;
  }
  if (keysym >= XK_1 && keysym <= XK_9) {
    unsigned char num = keysym - XK_1;

    Window focused;
    int revert_to;
    XGetInputFocus(dpy, &focused, &revert_to);

    if (state == MOD_KEY) {
      switchDesktop(num);
    } else if (state == (MOD_KEY | ShiftMask) && focused != None && focused != root) {
      moveWindowToDesktop(focused, num);
    }
    return;
  }

  const size_t launcherCount = sizeof(launchers) / sizeof(launchers[0]);
  for (size_t i = 0; i < launcherCount; i++) {
    if (keysym == launchers[i].keysym && state == MOD_KEY) {
      if (fork() == 0) {
        setsid();
        close(0);
        close(1);
        close(2);

        int devnull = open("/dev/null", O_RDWR);
        if (devnull >= 0) {
          dup2(devnull, STDIN_FILENO);
          dup2(devnull, STDOUT_FILENO);
          dup2(devnull, STDERR_FILENO);
          if (devnull > 2) close(devnull);
        }

        execl("/bin/sh", "sh", "-c", launchers[i].command, NULL);
        _exit(EXIT_FAILURE);
      }
      return;
    }
  }
}

static inline int detachWindow(Window w, Window *windows, unsigned char *windowCount,
                               unsigned char *focusedIdx) {
  for (int i = 0; i < *windowCount; i++) {
    if (windows[i] == w) {
      if (i < *windowCount - 1)
        memmove(&windows[i], &windows[i + 1], (*windowCount - i - 1) * sizeof(Window));
      (*windowCount)--;
      if (*focusedIdx >= *windowCount) *focusedIdx = *windowCount ? *windowCount - 1 : 0;
      return 1;
    }
  }
  return 0;
}

static inline int detachWindowFromDesktop(Window w, Desktop *d) {
  return detachWindow(w, d->windows, &d->windowCount, &d->focusedIdx);
}

static void moveWindowToDesktop(Window win, unsigned char desktop) {
  if (desktop >= MAX_DESKTOPS || desktop == currentDesktop) return;

  Desktop *target = &desktops[desktop];
  if (target->windowCount >= MAX_WINDOWS_PER_DESKTOP) return;

  Desktop *current = &desktops[currentDesktop];

  int windowIdx = -1;
  for (int i = 0; i < current->windowCount; i++) {
    if (current->windows[i] == win) {
      windowIdx = i;
      break;
    }
  }

  if (windowIdx == -1) return;

  bool wasMapped = current->isMapped[windowIdx];

  if (!detachWindowFromDesktop(win, current)) return;

  int newIdx               = target->windowCount;
  target->windows[newIdx]  = win;
  target->isMapped[newIdx] = wasMapped;
  target->windowCount++;

  XUnmapWindow(dpy, win);
  tileWindows();
}

inline static void focusWindow(Window w) { XSetInputFocus(dpy, w, RevertToParent, CurrentTime); }
static bool handleWindowRemoval(Window window) {
  for (size_t d = 0; d < MAX_DESKTOPS; d++) {
    if (detachWindow(window, desktops[d].windows, &desktops[d].windowCount,
                     &desktops[d].focusedIdx)) {
      tileWindows();
      return true;
    }
  }
  return false;
}

static void unmapWindow(Window window) {
  Desktop *desktop = &desktops[currentDesktop];
  for (int i = 0; i < desktop->windowCount; i++) {
    if (desktop->windows[i] == window) {
      desktop->isMapped[i] = false;
      XUnmapWindow(dpy, window);
      if (i == desktop->focusedIdx && desktop->windowCount > 1) {
        desktop->focusedIdx = (i + 1) % desktop->windowCount;
        focusWindow(desktop->windows[desktop->focusedIdx]);
      }

      tileWindows();
      return;
    }
  }
}

static void handleUnmapNotify(XEvent *e) { unmapWindow(e->xunmap.window); }

static void handleDestroyNotify(XEvent *e) { handleWindowRemoval(e->xdestroywindow.window); }

static void cleanup(void) {
  for (int d = 0; d < MAX_DESKTOPS; d++) {
    for (int i = 0; i < desktops[d].windowCount; i++) {
      XUnmapWindow(dpy, desktops[d].windows[i]);
    }
  }

  XCloseDisplay(dpy);
}

static void tileWindows(void) {
  Desktop *d = &desktops[currentDesktop];
  int count  = d->windowCount;
  if (count == 0) return;
  int masterCount  = count > 1 ? 1 : 0;
  int stackCount   = count - masterCount;
  int masterWidth  = screen_width * 0.6;
  int stackWidth   = screen_width - masterWidth;
  int masterHeight = screen_height / masterCount;
  int stackHeight  = screen_height / stackCount;

  if (count == 1) {
    XMoveResizeWindow(dpy, d->windows[0], 0, 0, screen_width, screen_height);
    XRaiseWindow(dpy, d->windows[0]);
    return;
  }

  if (masterCount > 0) {
    XMoveResizeWindow(dpy, d->windows[0], 0, 0, masterWidth, masterHeight);
  }
  for (int i = 0; i < stackCount; i++) {
    XMoveResizeWindow(dpy, d->windows[i + masterCount], masterWidth, i * stackHeight, stackWidth,
                      stackHeight);
  }
  XRaiseWindow(dpy, d->windows[0]);
}

static void mapWindowToDesktop(Window win) {
  Desktop *d = &desktops[currentDesktop];

  if (d->windowCount < MAX_WINDOWS_PER_DESKTOP) {
    int idx          = d->windowCount;
    d->windows[idx]  = win;
    d->isMapped[idx] = true;
    d->windowCount++;
    d->focusedIdx = idx;

    XMapWindow(dpy, win);
    focusWindow(win);
    tileWindows();
  } else {
    XKillClient(dpy, win);
    XFlush(dpy);
  }
}

static void handleMapRequest(XEvent *e) {
  XMapRequestEvent *ev = &e->xmaprequest;
  mapWindowToDesktop(ev->window);
}

static void handleMapNotify(XEvent *e) {
  XMapEvent *ev = &e->xmap;
  Desktop *d    = &desktops[currentDesktop];

  for (int i = 0; i < d->windowCount; i++) {
    if (d->windows[i] == ev->window && !d->isMapped[i]) {
      d->isMapped[i] = true;
      tileWindows();
      break;
    }
  }
}

static void switchDesktop(int desktop) {
  if (desktop == currentDesktop || desktop < 0 || desktop >= MAX_DESKTOPS) return;

  Desktop *current = &desktops[currentDesktop];
  for (int i = 0; i < current->windowCount; i++) {
    XUnmapWindow(dpy, current->windows[i]);
  }

  currentDesktop  = desktop;
  Desktop *target = &desktops[currentDesktop];

  for (int i = 0; i < target->windowCount; i++) {
    XMapWindow(dpy, target->windows[i]);
  }

  tileWindows();

  if (target->windowCount > 0) {
    focusWindow(target->windows[target->focusedIdx]);
  }
}
