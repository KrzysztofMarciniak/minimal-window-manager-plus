#include <X11/XF86keysym.h>
#include <X11/XKBlib.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <netdb.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/statvfs.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#define STATUS_BAR_HEIGHT 20
#define RESIZE_STEP 50
#define MAX_DESKTOPS 9
#define MAX_WINDOWS_PER_DESKTOP 6
#define MOD_KEY Mod4Mask
#define GAP_SIZE 10
#define BORDER_WIDTH 1
#define COLOR_A 0xFFFFFF
#define COLOR_B 0x000000

typedef struct {
  KeySym keysym;
  const char *command;
} AppLauncher;
static const AppLauncher launchers[] = {{XK_Return, "st"},
                                        {XK_p,
                                         "dmenu_run -m '0' -nb '#000000' -nf '#ffffff' -sb "
                                         "'#ffffff' -sf '#000000'"},
                                        {XF86XK_AudioRaiseVolume, AUDIO_SCRIPT " +"},
                                        {XF86XK_AudioLowerVolume, AUDIO_SCRIPT " -"},
                                        {XF86XK_AudioMicMute, AUDIO_SCRIPT " mic"},
                                        {XF86XK_AudioMute, AUDIO_SCRIPT " aud"}};
typedef struct {
  Window windows[MAX_WINDOWS_PER_DESKTOP];
  unsigned char windowCount;
  unsigned char focusedIdx;
  _Bool isMapped[MAX_WINDOWS_PER_DESKTOP];
} Desktop;
static Display *dpy;
static Window root;
static Desktop desktops[MAX_DESKTOPS];
static unsigned char currentDesktop  = 0;
static volatile sig_atomic_t running = 1;
static unsigned short screen_width, screen_height;
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
static inline int detachWindow(Window w, Window *windows, unsigned char *windowCount,
                               unsigned char *focusedIdx, _Bool *isMapped);
static char previousStatus[256] = "";
static short resizeDelta        = 0;
int main(void) {
  signal(SIGTERM, sigHandler);
  signal(SIGINT, sigHandler);
  setup();
  run();
  cleanup();
}
static char *getCurrentTime() {
  static char timeStr[9];
  time_t t = time(NULL);
  struct tm tm_info;
  if (localtime_r(&t, &tm_info) == NULL) {
    timeStr[0] = '\0';
    return timeStr;
  }
  strftime(timeStr, sizeof(timeStr), "%H:%M:%S", &tm_info);
  return timeStr;
}
static char *getMemoryUsage() {
  static char memoryStatus[64];
  FILE *memInfo = fopen("/proc/meminfo", "r");
  if (!memInfo) {
    snprintf(memoryStatus, sizeof(memoryStatus), "m: Unknown");
    return memoryStatus;
  }
  unsigned long totalMemoryKB = 0, freeMemoryKB = 0, buffersKB = 0, cachedKB = 0;
  char line[256];
  while (fgets(line, sizeof(line), memInfo)) {
    if (sscanf(line, "MemTotal: %lu kB", &totalMemoryKB) == 1) {
      continue;
    }
    if (sscanf(line, "MemFree: %lu kB", &freeMemoryKB) == 1) {
      continue;
    }
    if (sscanf(line, "Buffers: %lu kB", &buffersKB) == 1) {
      continue;
    }
    if (sscanf(line, "Cached: %lu kB", &cachedKB) == 1) {
      break;
    }
  }
  fclose(memInfo);
  if (totalMemoryKB == 0) {
    snprintf(memoryStatus, sizeof(memoryStatus), "m: Unknown");
  } else {
    unsigned long usedMemoryKB      = totalMemoryKB - (freeMemoryKB + buffersKB + cachedKB);
    unsigned long memoryUtilization = (usedMemoryKB * 100) / totalMemoryKB;
    snprintf(memoryStatus, sizeof(memoryStatus), "m: %lu%%", memoryUtilization);
  }
  return memoryStatus;
}
static char *getCPUUsage() {
  static char cpuStatus[64];
  unsigned long user1, nice1, system1, idle1, iowait1, irq1, softirq1;
  unsigned long user2, nice2, system2, idle2, iowait2, irq2, softirq2;
  unsigned long total1, total2, idleTime1, idleTime2;
  FILE *cpuFile;
  cpuFile = fopen("/proc/stat", "r");
  if (!cpuFile) {
    snprintf(cpuStatus, sizeof(cpuStatus), "CPU: Unknown");
    return cpuStatus;
  }
  if (fscanf(cpuFile, "cpu %lu %lu %lu %lu %lu %lu %lu", &user1, &nice1, &system1, &idle1, &iowait1,
             &irq1, &softirq1) != 7) {
    fclose(cpuFile);
    snprintf(cpuStatus, sizeof(cpuStatus), "CPU: Error reading stats");
    return cpuStatus;
  }
  fclose(cpuFile);
  struct timespec ts = {0, 100000000};
  nanosleep(&ts, NULL);
  cpuFile = fopen("/proc/stat", "r");
  if (!cpuFile) {
    snprintf(cpuStatus, sizeof(cpuStatus), "cpu: Unknown");
    return cpuStatus;
  }
  if (fscanf(cpuFile, "cpu %lu %lu %lu %lu %lu %lu %lu", &user2, &nice2, &system2, &idle2, &iowait2,
             &irq2, &softirq2) != 7) {
    fclose(cpuFile);
    snprintf(cpuStatus, sizeof(cpuStatus), "cpu: Error reading stats");
    return cpuStatus;
  }
  fclose(cpuFile);
  total1                  = user1 + nice1 + system1 + idle1 + iowait1 + irq1 + softirq1;
  total2                  = user2 + nice2 + system2 + idle2 + iowait2 + irq2 + softirq2;
  idleTime1               = idle1 + iowait1;
  idleTime2               = idle2 + iowait2;
  unsigned long totalDiff = total2 - total1;
  unsigned long idleDiff  = idleTime2 - idleTime1;
  if (totalDiff == 0) {
    snprintf(cpuStatus, sizeof(cpuStatus), "cpu: Unknown");
    return cpuStatus;
  }
  unsigned long cpuUsage = (totalDiff - idleDiff) * 100 / totalDiff;
  snprintf(cpuStatus, sizeof(cpuStatus), "cpu: %lu%%", cpuUsage);
  return cpuStatus;
}

static char *getNetworkInfo() {
  static char networkStatus[128];
  struct ifaddrs *ifaddr, *ifa;
  int family, n;
  char host[NI_MAXHOST];
  if (getifaddrs(&ifaddr) == -1) {
    snprintf(networkStatus, sizeof(networkStatus), "n: Unknown");
    return networkStatus;
  }
  for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
    if (ifa->ifa_addr == NULL) continue;
    family = ifa->ifa_addr->sa_family;
    if (family == AF_INET) {
      n = getnameinfo(ifa->ifa_addr, sizeof(struct sockaddr_in), host, NI_MAXHOST, NULL, 0,
                      NI_NUMERICHOST);
      if (n == 0 && strcmp(ifa->ifa_name, "lo") != 0) {
        snprintf(networkStatus, sizeof(networkStatus), "n: %s (%s)", ifa->ifa_name, host);
        break;
      }
    }
  }
  freeifaddrs(ifaddr);
  return networkStatus;
}
static char *getBatteryStatus() {
  static char batteryStatus[64];
  FILE *capacityFile = fopen("/sys/class/power_supply/BAT0/capacity", "r");
  FILE *statusFile   = fopen("/sys/class/power_supply/BAT0/status", "r");
  if (!capacityFile || !statusFile) {
    snprintf(batteryStatus, sizeof(batteryStatus), "b: Unknown");
    if (capacityFile) fclose(capacityFile);
    if (statusFile) fclose(statusFile);
    return batteryStatus;
  }
  int capacity;
  char status[16];
  if (fscanf(capacityFile, "%d", &capacity) != 1) {
    capacity = -1;
  }
  if (fscanf(statusFile, "%15s", status) != 1) {
    status[0] = '\0';
  }
  fclose(capacityFile);
  fclose(statusFile);
  snprintf(batteryStatus, sizeof(batteryStatus), "b: %d%%%s", capacity,
           strcmp(status, "Charging") == 0      ? " (char)"
           : strcmp(status, "Discharging") == 0 ? " (dis)"
                                                : "");
  return batteryStatus;
}
static void drawStatusBar() {
  char status[512];
  snprintf(status, sizeof(status), "%s | %s | %s | %s | %s", getCurrentTime(), getBatteryStatus(),
           getMemoryUsage(), getCPUUsage(), getNetworkInfo());
  if (strcmp(status, previousStatus) != 0) {
    unsigned long backgroundColor = COLOR_B;
    unsigned long textColor       = COLOR_A;
    XSetForeground(dpy, DefaultGC(dpy, 0), backgroundColor);
    XFillRectangle(dpy, root, DefaultGC(dpy, 0), 0, screen_height - STATUS_BAR_HEIGHT, screen_width,
                   STATUS_BAR_HEIGHT);
    XSetForeground(dpy, DefaultGC(dpy, 0), textColor);
    XDrawString(dpy, root, DefaultGC(dpy, 0), 10, screen_height - STATUS_BAR_HEIGHT + 15, status,
                strlen(status));
    strncpy(previousStatus, status, sizeof(previousStatus) - 1);
  }
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
  for (unsigned char i = 0; i < MAX_DESKTOPS; i++) {
    desktops[i].windowCount = 0;
    desktops[i].focusedIdx  = 0;
    for (unsigned char j = 0; j < MAX_WINDOWS_PER_DESKTOP; j++) {
      desktops[i].windows[j]  = None;
      desktops[i].isMapped[j] = 0;
    }
  }
  XWindowAttributes attr;
  XGetWindowAttributes(dpy, root, &attr);
  screen_width  = attr.width;
  screen_height = attr.height;
  XSetErrorHandler(xerrorstart);
  XSelectInput(dpy, root, SubstructureRedirectMask | SubstructureNotifyMask | StructureNotifyMask);
  Cursor cursor = XCreateFontCursor(dpy, 68);
  XDefineCursor(dpy, root, cursor);
  XSync(dpy, False);
  XSetErrorHandler(xerror);
  grabKeys();
  XSync(dpy, False);
}
static void grabKeys(void) {
  const unsigned int modifiers[] = {MOD_KEY, MOD_KEY | ShiftMask};
  const unsigned char modCount   = sizeof(modifiers) / sizeof(modifiers[0]);
  KeyCode keycodes[40];
  int k = 0;
  for (unsigned char i = 0; i < MAX_DESKTOPS; i++) {
    keycodes[k++] = XKeysymToKeycode(dpy, XK_1 + i);
  }
  KeySym specialKeys[] = {XK_q, XK_j, XK_k, XK_h, XK_l};
  for (size_t i = 0; i < sizeof(specialKeys) / sizeof(specialKeys[0]); i++) {
    keycodes[k++] = XKeysymToKeycode(dpy, specialKeys[i]);
  }
  for (int i = 0; i < k; i++) {
    if (keycodes[i] == 0) continue;
    for (unsigned char j = 0; j < modCount; j++) {
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

  struct timeval last_draw, now;
  gettimeofday(&last_draw, NULL);

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
    gettimeofday(&now, NULL);
    long elapsed = (now.tv_sec - last_draw.tv_sec) * 1000000L + (now.tv_usec - last_draw.tv_usec);
    if (elapsed >= 500000L) {
      drawStatusBar();
      last_draw = now;
    }
    struct timeval timeout;
    timeout.tv_sec  = 0;
    timeout.tv_usec = 100000;
    FD_ZERO(&fds);
    FD_SET(xfd, &fds);
    select(xfd + 1, &fds, NULL, NULL, &timeout);
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
  for (long unsigned int i = 0; i < sizeof(ev) / sizeof(long); i++) {
    ((long *)&ev)[i] = 0;
  }
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
  if ((keysym == XK_j || keysym == XK_k) && state == MOD_KEY) {
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
static inline int detachWindowFromDesktop(Window w, Desktop *d) {
  return detachWindow(w, d->windows, &d->windowCount, &d->focusedIdx, NULL);
}
static void moveWindowToDesktop(Window win, unsigned char desktop) {
  if (desktop >= MAX_DESKTOPS || desktop == currentDesktop) return;
  Desktop *target = &desktops[desktop];
  if (target->windowCount >= MAX_WINDOWS_PER_DESKTOP) return;
  Desktop *current = &desktops[currentDesktop];
  short windowIdx  = -1;
  for (unsigned char i = 0; i < current->windowCount; i++) {
    if (current->windows[i] == win) {
      windowIdx = i;
      break;
    }
  }
  if (windowIdx == -1) return;
  _Bool wasMapped = current->isMapped[windowIdx];
  if (!detachWindowFromDesktop(win, current)) return;
  int newIdx               = target->windowCount;
  target->windows[newIdx]  = win;
  target->isMapped[newIdx] = wasMapped;
  target->windowCount++;
  XUnmapWindow(dpy, win);
  tileWindows();
}
inline static void focusWindow(Window w) {
  XSetInputFocus(dpy, w, RevertToParent, CurrentTime);
  for (unsigned char d = 0; d < MAX_DESKTOPS; d++) {
    Desktop *desk = &desktops[d];
    for (unsigned char i = 0; i < desk->windowCount; i++) {
      unsigned long color = (desk->windows[i] == w) ? COLOR_A : COLOR_B;
      XSetWindowBorderWidth(dpy, desk->windows[i], BORDER_WIDTH);
      XSetWindowBorder(dpy, desk->windows[i], color);
    }
  }
}
static void handleUnmapNotify(XEvent *e) {
  Window win = e->xunmap.window;
  Desktop *d = &desktops[currentDesktop];
  for (unsigned char i = 0; i < d->windowCount; i++) {
    if (d->windows[i] == win) {
      d->isMapped[i] = 0;
      XUnmapWindow(dpy, win);
      if (i == d->focusedIdx && d->windowCount > 1) {
        d->focusedIdx = (i + 1) % d->windowCount;
        focusWindow(d->windows[d->focusedIdx]);
      }
      for (unsigned char j = i; j < d->windowCount - 1; j++) {
        d->windows[j]  = d->windows[j + 1];
        d->isMapped[j] = d->isMapped[j + 1];
      }
      d->windowCount--;
      tileWindows();
      return;
    }
  }
}
static void handleDestroyNotify(XEvent *e) {
  Window win = e->xdestroywindow.window;
  Desktop *d = &desktops[currentDesktop];
  for (unsigned char i = 0; i < d->windowCount; i++) {
    if (d->windows[i] == win) {
      d->isMapped[i] = 0;
      d->windowCount--;
      for (unsigned char j = i; j < d->windowCount; j++) {
        d->windows[j]  = d->windows[j + 1];
        d->isMapped[j] = d->isMapped[j + 1];
      }
      tileWindows();
      break;
    }
  }
}
static void cleanup(void) {
  for (unsigned char d = 0; d < MAX_DESKTOPS; d++) {
    for (unsigned char i = 0; i < desktops[d].windowCount; i++) {
      XUnmapWindow(dpy, desktops[d].windows[i]);
    }
  }
  XCloseDisplay(dpy);
}
static void tileWindows(void) {
  Desktop *d          = &desktops[currentDesktop];
  unsigned char count = d->windowCount;
  if (count == 0) return;
  if (count == 1) {
    XMoveResizeWindow(dpy, d->windows[0], 0, 0, screen_width - 2 * BORDER_WIDTH,
                      screen_height - STATUS_BAR_HEIGHT - 2 * BORDER_WIDTH);
    d->isMapped[0] = 1;
    XMapWindow(dpy, d->windows[0]);
    focusWindow(d->windows[0]);
    return;
  }
  int masterCount  = count >= 1 ? 1 : 0;
  int stackCount   = count - masterCount;
  int totalGapV    = stackCount * GAP_SIZE;
  int totalGapH    = 3 * GAP_SIZE;
  int usableHeight = screen_height - STATUS_BAR_HEIGHT - totalGapV;
  int masterWidth  = (screen_width + (resizeDelta << 1)) >> 1;
  if (masterWidth < 100) masterWidth = 100;
  if (masterWidth > screen_width - 100) masterWidth = screen_width - 100;
  int stackWidth = screen_width - masterWidth - totalGapH;
  masterWidth -= 2 * GAP_SIZE;
  int masterHeight = screen_height - 0.5 * STATUS_BAR_HEIGHT - 2 * GAP_SIZE;
  int stackHeight  = stackCount > 0 ? (usableHeight / stackCount) : 0;
  int x, y, w, h;
  for (unsigned char i = 0; i < count; i++) {
    if (!d->isMapped[i]) continue;
    if (i == 0 && masterCount == 1) {
      x = GAP_SIZE;
      y = GAP_SIZE;
      w = masterWidth - 2 * BORDER_WIDTH;
      h = masterHeight - 2 * BORDER_WIDTH;
    } else {
      int stackIdx = i - 1;
      x            = masterWidth + 2 * GAP_SIZE;
      y            = GAP_SIZE + stackIdx * (stackHeight + GAP_SIZE);
      w            = stackWidth - 1.5 * BORDER_WIDTH;
      h            = stackHeight - 2 * BORDER_WIDTH;
    }
    XMoveResizeWindow(dpy, d->windows[i], x, y, w, h);
    d->isMapped[i] = 1;
    XMapWindow(dpy, d->windows[i]);
  }
  focusWindow(d->windows[d->focusedIdx]);
}
static void mapWindowToDesktop(Window win) {
  Desktop *d = &desktops[currentDesktop];
  if (d->windowCount < MAX_WINDOWS_PER_DESKTOP) {
    int idx          = d->windowCount;
    d->windows[idx]  = win;
    d->isMapped[idx] = 1;
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
static inline int detachWindow(Window w, Window *windows, unsigned char *windowCount,
                               unsigned char *focusedIdx, _Bool *isMapped) {
  for (unsigned char i = 0; i < *windowCount; i++) {
    if (windows[i] == w) {
      if (i < *windowCount - 1) {
        for (unsigned char j = i; j < *windowCount - 1; j++) {
          windows[j]  = windows[j + 1];
          isMapped[j] = isMapped[j + 1];
        }
      }
      (*windowCount)--;
      if (*focusedIdx >= *windowCount) *focusedIdx = *windowCount ? *windowCount - 1 : 0;
      return 1;
    }
  }
  return 0;
}
static void handleMapNotify(XEvent *e) {
  XMapEvent *ev = &e->xmap;
  Desktop *d    = &desktops[currentDesktop];
  for (unsigned char i = 0; i < d->windowCount; i++) {
    if (d->windows[i] == ev->window && !d->isMapped[i]) {
      d->isMapped[i] = 1;
      tileWindows();
      break;
    }
  }
}
static void switchDesktop(int desktop) {
  if (desktop == currentDesktop || desktop < 0 || desktop >= MAX_DESKTOPS) return;
  Desktop *current = &desktops[currentDesktop];
  for (unsigned char i = 0; i < current->windowCount; i++) {
    XUnmapWindow(dpy, current->windows[i]);
  }
  currentDesktop  = desktop;
  Desktop *target = &desktops[currentDesktop];
  for (unsigned char i = 0; i < target->windowCount; i++) {
    XMapWindow(dpy, target->windows[i]);
  }
  tileWindows();
  if (target->windowCount > 0) {
    focusWindow(target->windows[target->focusedIdx]);
  }
}
