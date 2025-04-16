#include <X11/XF86keysym.h>
#include <X11/XKBlib.h>
#include <X11/Xlib.h>
#include <X11/cursorfont.h>
#include <X11/keysym.h>
#include <errno.h>
#include <fcntl.h>
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
#define MAX_WINDOWS_PER_DESKTOP 2
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
static int currentDesktop = 0;
static volatile sig_atomic_t running = 1;
static int screen_width, screen_height;

static const AppLauncher launchers[] = {
    {XK_Return, "st"},
    {XK_p, "dmenu_run -m '0' -fn 'Monospace-8' -nb '#000000' -nf '#ffffff' -sb "
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
static void moveWindowToDesktop(Window win, int desktop);
static void grabKeys(void);
static void sigHandler(int);
static int xerrorstart(Display *, XErrorEvent *);
static int xerror(Display *, XErrorEvent *);
static void killFocusedWindow(void);
static void focusCycleWindow(int);
static void removeWindow(Window w);
static void handleMapNotify(XEvent *e);
inline static void die(const char *msg);

int main(void) {
  signal(SIGTERM, sigHandler);
  signal(SIGINT, sigHandler);
  setup();
  run();
  cleanup();
  return 0;
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
  if (!getenv("DISPLAY"))
    die("DISPLAY not set");
  if (!(dpy = XOpenDisplay(NULL)))
    die("cannot open display");

  root = DefaultRootWindow(dpy);

  memset(desktops, 0, sizeof(desktops));

  XWindowAttributes attr;
  XGetWindowAttributes(dpy, root, &attr);
  screen_width = attr.width;
  screen_height = attr.height;

  XSetErrorHandler(xerrorstart);
  XSelectInput(dpy, root,
               SubstructureRedirectMask | SubstructureNotifyMask |
                   StructureNotifyMask);
  Cursor cursor = XCreateFontCursor(dpy, XC_left_ptr);
  XDefineCursor(dpy, root, cursor);
  XSync(dpy, False);
  XSetErrorHandler(xerror);

  grabKeys();
  XSync(dpy, False);
}

static void grabKeys(void) {
  const unsigned int modifiers[] = {MOD_KEY, MOD_KEY | ShiftMask};
  const int modCount = sizeof(modifiers) / sizeof(modifiers[0]);
  KeyCode keycodes[40];
  int k = 0;

  for (int i = 0; i < MAX_DESKTOPS; i++) {
    keycodes[k++] = XKeysymToKeycode(dpy, XK_1 + i);
  }

  KeySym specialKeys[] = {XK_q, XK_j, XK_l, XK_h};

  for (int i = 0; i < sizeof(specialKeys) / sizeof(specialKeys[0]); i++) {
    keycodes[k++] = XKeysymToKeycode(dpy, specialKeys[i]);
  }

  for (int i = 0; i < k; i++) {
    if (keycodes[i] == 0)
      continue;
    for (int j = 0; j < modCount; j++) {
      XGrabKey(dpy, keycodes[i], modifiers[j], root, True, GrabModeAsync,
               GrabModeAsync);
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

    if (select(xfd + 1, &fds, NULL, NULL, NULL) == -1 && errno != EINTR)
      break;
  }
}

static void handleConfigureNotify(XEvent *e) {
  XConfigureEvent *ev = &e->xconfigure;

  if (ev->window == root) {
    screen_width = ev->width;
    screen_height = ev->height;
    tileWindows();
  }
}

static void killFocusedWindow(void) {
  Desktop *d = &desktops[currentDesktop];
  if (d->windowCount <= 0)
    return;

  Window win = d->windows[d->focusedIdx];
  if (win == None || win == root)
    return;

  Atom *p;
  int n;
  Atom del = XInternAtom(dpy, "WM_DELETE_WINDOW", False);

  if (XGetWMProtocols(dpy, win, &p, &n)) {
    while (n--)
      if (p[n] == del)
        goto send;
    XKillClient(dpy, win);
    XFree(p);
    return;
  }
  XEvent ev;
send:
  memset(&ev, 0, sizeof(ev));
  ev.type = ClientMessage;
  ev.xclient.window = win;
  ev.xclient.message_type = XInternAtom(dpy, "WM_PROTOCOLS", False);
  ev.xclient.format = 32;
  ev.xclient.data.l[0] = del;
  ev.xclient.data.l[1] = CurrentTime;
  XSendEvent(dpy, win, False, NoEventMask, &ev);
  XFree(p);
  XFlush(dpy);
}

static void focusCycleWindow(int direction) {
  Desktop *d = &desktops[currentDesktop];
  if (d->windowCount > 1) {
    d->focusedIdx =
        (d->focusedIdx + direction + d->windowCount) % d->windowCount;
    focusWindow(d->windows[d->focusedIdx]);
  }
}

static void handleKeyPress(XEvent *e) {
  XKeyEvent *ev = &e->xkey;
  KeySym keysym = XkbKeycodeToKeysym(dpy, ev->keycode, 0, 0);
  unsigned int state = ev->state;

  if (keysym == XK_q && state == (MOD_KEY | ShiftMask)) {
    running = 0;
    return;
  }

  if (keysym == XK_h && state == (MOD_KEY | ShiftMask)) {
    resizeDelta -= RESIZE_STEP;
    tileWindows();
    return;
  }

  if (keysym == XK_l && state == (MOD_KEY | ShiftMask)) {
    resizeDelta += RESIZE_STEP;
    tileWindows();
    return;
  }

  if (keysym == XK_q && state == MOD_KEY) {
    killFocusedWindow();
    return;
  }

  if (keysym == XK_j && state == MOD_KEY) {
    focusCycleWindow(1);
    return;
  }

  if (keysym == XK_l && state == MOD_KEY) {
    focusCycleWindow(-1);
    return;
  }

  if (keysym >= XK_1 && keysym <= XK_9) {
    int num = keysym - XK_1;
    if (state == MOD_KEY) {
      switchDesktop(num);
      return;
    } else if (state == (MOD_KEY | ShiftMask)) {
      Window focused;
      int revert_to;
      XGetInputFocus(dpy, &focused, &revert_to);

      if (focused != None && focused != root) {
        moveWindowToDesktop(focused, num);
      }
      return;
    }
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
          if (devnull > 2)
            close(devnull);
        }

        execl("/bin/sh", "sh", "-c", launchers[i].command, NULL);
        _exit(EXIT_FAILURE);
      }
      return;
    }
  }
}

static inline int detachWindowFromDesktop(Window w, Desktop *d) {
  for (int i = 0; i < d->windowCount; i++) {
    if (d->windows[i] == w) {
      if (i < d->windowCount - 1)
        memmove(&d->windows[i], &d->windows[i + 1],
                (d->windowCount - i - 1) * sizeof(Window));
      d->windowCount--;
      if (d->focusedIdx >= d->windowCount)
        d->focusedIdx = d->windowCount ? d->windowCount - 1 : 0;
      return 1;
    }
  }
  return 0;
}

static void moveWindowToDesktop(Window win, int desktop) {
  if (desktop < 0 || desktop >= MAX_DESKTOPS || desktop == currentDesktop)
    return;

  Desktop *target = &desktops[desktop];
  if (target->windowCount >= MAX_WINDOWS_PER_DESKTOP)
    return;

  Desktop *current = &desktops[currentDesktop];
  if (!detachWindowFromDesktop(win, current))
    return;

  target->windows[target->windowCount++] = win;

  XUnmapWindow(dpy, win);
  tileWindows();
}

static void handleMapRequest(XEvent *e) {
  XMapRequestEvent *ev = &e->xmaprequest;
  Desktop *d = &desktops[currentDesktop];

  if (d->windowCount < MAX_WINDOWS_PER_DESKTOP) {
    int idx = d->windowCount;
    d->windows[idx] = ev->window;
    d->isMapped[idx] = false;
    d->windowCount++;
    d->focusedIdx = idx;

    XMapWindow(dpy, ev->window);
    focusWindow(ev->window);
  } else {
    XKillClient(dpy, ev->window);
    XFlush(dpy);
  }
}

inline static void focusWindow(Window w) {
  XSetInputFocus(dpy, w, RevertToParent, CurrentTime);
}

static void switchDesktop(int desktop) {
  if (desktop == currentDesktop || desktop < 0 || desktop >= MAX_DESKTOPS)
    return;

  Desktop *current = &desktops[currentDesktop];
  for (int i = 0; i < current->windowCount; i++) {
    XUnmapWindow(dpy, current->windows[i]);
  }

  currentDesktop = desktop;
  Desktop *target = &desktops[currentDesktop];

  for (int i = 0; i < target->windowCount; i++) {
    XMapWindow(dpy, target->windows[i]);
  }

  tileWindows();

  if (target->windowCount > 0) {
    focusWindow(target->windows[target->focusedIdx]);
  }
}

static void removeWindow(Window w) {
  if (detachWindowFromDesktop(w, &desktops[currentDesktop])) {
    tileWindows();
  }
}

static void handleUnmapNotify(XEvent *e) { removeWindow(e->xunmap.window); }

static void handleDestroyNotify(XEvent *e) {
  removeWindow(e->xdestroywindow.window);
}

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
  int tiledCount = 0;

  for (int i = 0; i < d->windowCount; i++) {
    if (d->isMapped[i]) {
      tiledCount++;
    }
  }

  if (tiledCount == 0)
    return;

  if (tiledCount == 1) {
    int width = screen_width;
    int x = (screen_width - width) / 2;
    XMoveResizeWindow(dpy, d->windows[0], x, 0, width, screen_height);
  } else {
    int minSize = 100;
    int half = (screen_width + resizeDelta) / 2;
    if (half < minSize)
      half = minSize;

    for (int i = 0; i < tiledCount; i++) {
      if (i < tiledCount / 2) {
        int width = half;
        if (width < minSize)
          width = minSize;
        XMoveResizeWindow(dpy, d->windows[i], 0, 0, width, screen_height);
      } else {
        int width = screen_width - half;
        if (width < minSize)
          width = minSize;
        XMoveResizeWindow(dpy, d->windows[i], half, 0, width, screen_height);
      }
    }
  }
}

static void handleMapNotify(XEvent *e) {
  XMapEvent *ev = &e->xmap;
  Desktop *d = &desktops[currentDesktop];

  for (int i = 0; i < d->windowCount; i++) {
    if (d->windows[i] == ev->window && !d->isMapped[i]) {
      d->isMapped[i] = true;
      tileWindows();
      break;
    }
  }
}
