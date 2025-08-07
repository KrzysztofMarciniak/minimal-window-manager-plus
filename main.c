#include <X11/XF86keysym.h>
#include <X11/XKBlib.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <unistd.h>

#define RESIZE_STEP 30
#define MAX_DESKTOPS 9            // 256 limit
#define MAX_WINDOWS_PER_DESKTOP 12// 256 limit
#define CURRENT_DESKTOP desktops[currentDesktop]
#define DESKTOPS desktops
#define MOD_KEY Mod4Mask
#ifndef AUDIO_SCRIPT
#define AUDIO_SCRIPT ""
#endif

#define GAP_SIZE 10
#define BORDER_WIDTH 1
#define COLOR_A 0xFFFFFF
#define COLOR_B 0x000000
#define STATUS_BAR_LENGTH 256// modify this if you want longer status bar.
#define STATUS_BAR_HEIGHT 20
#ifndef STATUS_BAR_SCRIPT
#define STATUS_BAR_SCRIPT ""
#endif

static char currentStatus[STATUS_BAR_LENGTH]  = "";
static char previousStatus[STATUS_BAR_LENGTH] = "";
static _Bool statusBarVisible;
static Window barWindow;
static int statusFd    = -1;
static pid_t statusPid = 0;

typedef struct {
        KeySym keysym;
        const char *command;
} AppLauncher;
static AppLauncher launchers[6] = {{XK_Return, "st"},
                                   {XK_p,
                                    "dmenu_run -m '0' -nb '#000000' -nf '#ffffff' -sb "
                                    "'#ffffff' -sf '#000000'"},
                                   {XF86XK_AudioRaiseVolume, AUDIO_SCRIPT " +"},
                                   {XF86XK_AudioLowerVolume, AUDIO_SCRIPT " -"},
                                   {XF86XK_AudioMicMute, AUDIO_SCRIPT " mic"},
                                   {XF86XK_AudioMute, AUDIO_SCRIPT " aud"}};
typedef struct {
        Window windows[MAX_WINDOWS_PER_DESKTOP];
        _Bool isMapped[MAX_WINDOWS_PER_DESKTOP];
        unsigned char windowCount;
        unsigned char focusedIdx;
} Desktop;
static Display *dpy;
#define ARRAY_LEN(arr) (sizeof(arr) / sizeof((arr)[0]))
static _Bool IsSwitching = False;
static Desktop desktops[MAX_DESKTOPS];
static Window root;
static unsigned char currentDesktop  = 0;
static volatile sig_atomic_t running = 1;
// screen resolution fits within 0–65,535 range, too large for unsigned char
// (0–255) but safe in unsigned short
static unsigned short screen_width, screen_height;

// 'resizeDelta' stores pixel offsets in tileWindows.
// 'char' (-128 to 127) is too small to hold needed values,
// but 'short' (-32,768 to 32,767) provides sufficient range.
static short resizeDelta[MAX_DESKTOPS] = {0};
static _Bool need_flush                = False;

static void setup(void);
static void run(void);
static void cleanup(void);
static void handleKeyPress(XEvent *e);
static void handleMapRequest(Window win);
static void handleDestroyNotify(Window win);
inline static void focusWindow(Window w);
static void tileWindows(void);
static void switchDesktop(unsigned char desktop);
static void moveWindowToDesktop(Window win, unsigned char desktop);
static void grabKeys(void);
static void sigHandler(Bool);// Bool = int, provided by x11
// Must return int because XSetErrorHandler requires this signature
static int xerror(Display *, XErrorEvent *);
static void killFocusedWindow(void);
inline static void focusCycleWindow(_Bool);// 1 bit
static void removeWindowFromDesktop(Window win, Desktop *d);
inline static void die(void);
static void handleUnmapNotify(Window win);

unsigned short myStrlen(const char *s);
inline static unsigned short myStrcmp(const char *a, const char *b);
inline static char *myStrncpy(char *dest, const char *src, unsigned short n);
static void toggleStatusBar(void);

unsigned short myStrlen(const char *s) {
        short len = 0;
        while (s[len]) len++;
        return len;
}
inline static unsigned short myStrcmp(const char *a, const char *b) {
        for (; *a && *a == *b; ++a, ++b);
        return (unsigned short)((unsigned char)*a - (unsigned char)*b);
}
inline static char *myStrncpy(char *dest, const char *src, unsigned short n) {
        unsigned short i = 0;
        for (; i < n && src[i]; ++i) dest[i] = src[i];
        for (; i < n; ++i) dest[i] = '\0';
        return dest;
}
static void runStatusBar(void) {
        if (statusPid != 0) return;
        int pipefd[2];
        if (pipe(pipefd) < 0) die();
        pid_t pid = fork();
        if (pid < 0) die();
        if (pid == 0) {
                setsid();
                dup2(pipefd[1], STDOUT_FILENO);
                close(pipefd[0]);
                close(pipefd[1]);
                execl("/bin/sh", "sh", "-c", STATUS_BAR_SCRIPT, NULL);
                _exit(1);
        }
        close(pipefd[1]);
        statusFd  = pipefd[0];
        statusPid = pid;
}
static void killStatusBar(void) {
        if (statusFd != -1) {
                close(statusFd);
                statusFd = -1;
        }
        if (statusPid != 0) {
                kill(statusPid, SIGTERM);
                statusPid = 0;
        }
}
static void toggleStatusBar(void) {
        statusBarVisible = !statusBarVisible;
        if (statusBarVisible) {
                runStatusBar();
                XMapWindow(dpy, barWindow);
                XRaiseWindow(dpy, barWindow);
        } else {
                XUnmapWindow(dpy, barWindow);
                killStatusBar();
        }
        tileWindows();
}
static void drawStatusBar() {
        if (!statusBarVisible) return;
        short len = myStrlen(currentStatus);
        while (len > 0 && (currentStatus[len - 1] == '\n' || currentStatus[len - 1] == '\r')) {
                currentStatus[--len] = '\0';
        }
        if (len == 0) return;
        if (myStrcmp(currentStatus, previousStatus) != 0) {
                GC gc = DefaultGC(dpy, 0);
                XSetForeground(dpy, gc, COLOR_B);
                XFillRectangle(dpy, barWindow, gc, 0, 0, screen_width, STATUS_BAR_HEIGHT);
                XSetForeground(dpy, gc, COLOR_A);
                XDrawString(dpy, barWindow, gc, 10, STATUS_BAR_HEIGHT - 5, currentStatus, len);
                myStrncpy(previousStatus, currentStatus, sizeof(previousStatus) - 1);
                previousStatus[sizeof(previousStatus) - 1] = '\0';
                XFlush(dpy);
        }
}
int main(void) {
        signal(SIGCHLD, SIG_IGN);
        signal(SIGTERM, sigHandler);
        signal(SIGINT, sigHandler);
        setup();
        run();
        cleanup();
}
inline static void die(void) {
        __attribute__((unused)) char _ = write(2, "mwm:error\n", 10);
        _exit(1);
}
static void sigHandler(Bool sig) {
        (void)sig;
        running = 0;
}
static int xerror(Display *dpy, XErrorEvent *ee) {
        static _Bool startup = True;
        if (startup) {
                startup = False;
                die();
                return -1;
        }
        (void)dpy;
        (void)ee;
        return 0;
}
static void setup(void) {
        if (!getenv("DISPLAY")) die();
        dpy = XOpenDisplay(NULL);
        if (!dpy) die();
        root = DefaultRootWindow(dpy);
        XWindowAttributes attr;
        if (!XGetWindowAttributes(dpy, root, &attr)) die();
        screen_width  = attr.width;
        screen_height = attr.height;
        {
                XSetWindowAttributes wa;
                wa.override_redirect = True;
                wa.background_pixel  = COLOR_B;
                barWindow            = XCreateWindow(
                    dpy, root, 0, screen_height - STATUS_BAR_HEIGHT, screen_width,
                    STATUS_BAR_HEIGHT, 0, DefaultDepth(dpy, DefaultScreen(dpy)), CopyFromParent,
                    DefaultVisual(dpy, DefaultScreen(dpy)), CWOverrideRedirect | CWBackPixel, &wa);
                XMapWindow(dpy, barWindow);
                XRaiseWindow(dpy, barWindow);
        }
        XSetErrorHandler(xerror);
        XSelectInput(dpy, root,
                     SubstructureRedirectMask | SubstructureNotifyMask | StructureNotifyMask);
        Cursor cursor = XCreateFontCursor(dpy, 68);
        if (cursor == None) die();
        XDefineCursor(dpy, root, cursor);
        toggleStatusBar();
        grabKeys();
        XSync(dpy, False);
}
static void grabKey(KeySym keysym, unsigned int modifiers, Bool with_shift) {
        KeyCode code = XKeysymToKeycode(dpy, keysym);
        if (!code) return;
        XGrabKey(dpy, code, modifiers, root, True, GrabModeAsync, GrabModeAsync);
        if (with_shift) {
                XGrabKey(dpy, code, modifiers | ShiftMask, root, True, GrabModeAsync,
                         GrabModeAsync);
        }
}
static void grabKeys(void) {
        for (unsigned char i = 0; i < MAX_DESKTOPS; ++i) {
                grabKey(XK_1 + i, MOD_KEY, True);
        }
        static const KeySym wmKeys[] = {XK_q, XK_j, XK_k, XK_h, XK_l, XK_b};
        for (unsigned char i = 0; i < ARRAY_LEN(wmKeys); ++i) {
                grabKey(wmKeys[i], MOD_KEY, True);
        }
        for (unsigned char i = 0; i < ARRAY_LEN(launchers); ++i) {
                grabKey(launchers[i].keysym, MOD_KEY, False);
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
                                        handleMapRequest(e.xmaprequest.window);
                                        break;
                                case UnmapNotify:
                                        handleUnmapNotify(e.xunmap.window);
                                        break;
                                case DestroyNotify:
                                        handleDestroyNotify(e.xdestroywindow.window);
                                        break;
                                case ConfigureNotify:
                                        if (e.xconfigure.window != root) break;
                                        screen_width  = e.xconfigure.width;
                                        screen_height = e.xconfigure.height;
                                        tileWindows();
                                        break;
                                default:
                                        break;
                        }
                }
                if (need_flush) {
                        XFlush(dpy);
                        need_flush = False;
                }
                FD_ZERO(&fds);
                FD_SET(xfd, &fds);
                int maxfd = xfd;
                if (statusFd >= 0) {
                        FD_SET(statusFd, &fds);
                        if (statusFd > maxfd) maxfd = statusFd;
                }
                int ret = select(maxfd + 1, &fds, NULL, NULL, NULL);
                if (ret == -1) {
                        if (errno == EINTR)
                                continue;
                        else
                                break;
                }
                if (statusFd >= 0 && FD_ISSET(statusFd, &fds)) {
                        ssize_t n = read(statusFd, currentStatus, sizeof(currentStatus) - 1);
                        if (n > 0) {
                                currentStatus[n] = '\0';
                                drawStatusBar();
                                need_flush = True;
                        } else {
                                killStatusBar();
                                runStatusBar();
                        }
                }
        }
}
static void killFocusedWindow(void) {
        if (CURRENT_DESKTOP.windowCount == 0) return;
        if (CURRENT_DESKTOP.focusedIdx >= CURRENT_DESKTOP.windowCount) return;
        Window win = CURRENT_DESKTOP.windows[CURRENT_DESKTOP.focusedIdx];
        if (win == None || win == root) return;
        Atom wmDelete    = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
        Atom wmProtocols = XInternAtom(dpy, "WM_PROTOCOLS", False);
        Atom *protocols  = NULL;
        int count;
        if (XGetWMProtocols(dpy, win, &protocols, &count)) {
                for (int i = 0; i < count; ++i) {
                        if (protocols[i] == wmDelete) {
                                XEvent ev               = {0};
                                ev.type                 = ClientMessage;
                                ev.xclient.window       = win;
                                ev.xclient.message_type = wmProtocols;
                                ev.xclient.format       = 32;
                                ev.xclient.data.l[0]    = wmDelete;
                                ev.xclient.data.l[1]    = CurrentTime;
                                XSendEvent(dpy, win, False, NoEventMask, &ev);
                                XFree(protocols);
                                need_flush = True;
                                return;
                        }
                }
                XFree(protocols);
        }
        XKillClient(dpy, win);
        need_flush = True;
}
inline static void focusCycleWindow(_Bool forward) {
        if (CURRENT_DESKTOP.windowCount <= 1) return;
        short idx = CURRENT_DESKTOP.focusedIdx + (forward ? 1 : -1);
        idx += CURRENT_DESKTOP.windowCount * (idx < 0);
        idx -= CURRENT_DESKTOP.windowCount * (idx >= CURRENT_DESKTOP.windowCount);
        CURRENT_DESKTOP.focusedIdx = idx;
        focusWindow(CURRENT_DESKTOP.windows[idx]);
}
static void handleKeyPress(XEvent *e) {
        XKeyEvent *ev      = &e->xkey;
        KeySym keysym      = XkbKeycodeToKeysym(dpy, ev->keycode, 0, 0);
        unsigned int state = ev->state;
        if (keysym == XK_q && state == (MOD_KEY | ShiftMask)) {
                running = 0;
                return;
        }
        if (keysym == XK_b && state == MOD_KEY) {
                toggleStatusBar();
                return;
        }
        if ((keysym == XK_h || keysym == XK_l) && state == (MOD_KEY | ShiftMask)) {
                short *rd = &resizeDelta[currentDesktop];
                *rd += (keysym == XK_l) ? RESIZE_STEP : -RESIZE_STEP;
                if (*rd < -(screen_width / 2 - 100)) *rd = -(screen_width / 2 - 100);
                if (*rd > (screen_width / 2 - 100)) *rd = (screen_width / 2 - 100);
                tileWindows();
                return;
        }
        if (keysym == XK_q && state == MOD_KEY) {
                killFocusedWindow();
                return;
        }
        if ((keysym == XK_j || keysym == XK_k) && state == MOD_KEY) {
                focusCycleWindow(keysym == XK_j ? True : False);
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
        for (unsigned int i = 0; i < ARRAY_LEN(launchers); i++) {
                if (keysym == launchers[i].keysym && state == MOD_KEY) {
                        if (fork() == 0) {
                                if (fork() == 0) {
                                        setsid();
                                        for (int fd = 0; fd <= 2; fd++) close(fd);
                                        execl("/bin/sh", "sh", "-c", launchers[i].command, NULL);
                                        _exit(EXIT_FAILURE);
                                }
                                _exit(EXIT_SUCCESS);
                        }
                        return;
                }
        }
}
static void moveWindowToDesktop(Window win, unsigned char desktop) {
        if (desktop >= MAX_DESKTOPS || desktop == currentDesktop) return;
        if (DESKTOPS[desktop].windowCount >= MAX_WINDOWS_PER_DESKTOP) return;
        for (unsigned char i = 0; i < CURRENT_DESKTOP.windowCount; i++) {
                if (CURRENT_DESKTOP.windows[i] == win) {
                        removeWindowFromDesktop(win, &CURRENT_DESKTOP);
                        unsigned char idx               = DESKTOPS[desktop].windowCount;
                        DESKTOPS[desktop].windows[idx]  = win;
                        DESKTOPS[desktop].isMapped[idx] = False;
                        DESKTOPS[desktop].windowCount++;
                        XUnmapWindow(dpy, win);
                        tileWindows();
                        return;
                }
        }
}
inline static void focusWindow(Window w) {
        if (w == None) return;
        XSetInputFocus(dpy, w, RevertToParent, CurrentTime);
        for (unsigned char i = 0; i < CURRENT_DESKTOP.windowCount; i++) {
                Window win = CURRENT_DESKTOP.windows[i];
                if (win == None) continue;
                XSetWindowBorder(dpy, win, (win == w) ? COLOR_A : COLOR_B);
        }
        need_flush = True;
}
static void removeWindowFromDesktop(Window win, Desktop *d) {
        unsigned int write     = 0;
        unsigned char newFocus = d->focusedIdx;
        for (unsigned int read = 0; read < d->windowCount; read++) {
                if (d->windows[read] != win) {
                        d->windows[write]  = d->windows[read];
                        d->isMapped[write] = d->isMapped[read];
                        if (read == d->focusedIdx) newFocus = write;
                        write++;
                }
        }
        d->windowCount = write;
        if (write == 0) {
                d->focusedIdx = 0;
                return;
        }
        if (newFocus >= write) newFocus = write - 1;
        d->focusedIdx = newFocus;
        if (d->isMapped[d->focusedIdx]) focusWindow(d->windows[d->focusedIdx]);
}
static void handleDestroyNotify(Window win) {
        for (unsigned char d_idx = 0; d_idx < MAX_DESKTOPS; d_idx++) {
                Desktop *d = &desktops[d_idx];
                for (unsigned char i = 0; i < d->windowCount; i++) {
                        if (d->windows[i] == win) {
                                removeWindowFromDesktop(win, d);
                                if (d_idx == currentDesktop) {
                                        tileWindows();
                                }
                                return;
                        }
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
        const unsigned char n = CURRENT_DESKTOP.windowCount;
        if (n == 0) return;
        unsigned char statusBarHeight = statusBarVisible ? STATUS_BAR_HEIGHT : 0;
        if (n == 1) {
                XMoveResizeWindow(dpy, CURRENT_DESKTOP.windows[0], GAP_SIZE, GAP_SIZE,
                                  screen_width - 2 * GAP_SIZE,
                                  screen_height - 2 * GAP_SIZE - statusBarHeight);
                XRaiseWindow(dpy, CURRENT_DESKTOP.windows[0]);
                return;
        }
        unsigned short masterWidth = (screen_width + (resizeDelta[currentDesktop] << 1)) >> 1;
        masterWidth                = (masterWidth < 100)                  ? 100
                                     : (masterWidth > screen_width - 100) ? screen_width - 100
                                                                          : masterWidth;
        unsigned short totalHorizontalGaps = 3 * GAP_SIZE;
        unsigned short stackWidth          = screen_width - masterWidth - totalHorizontalGaps;
        unsigned char stackCount           = CURRENT_DESKTOP.windowCount - 1;
        unsigned short totalVerticalGaps   = (stackCount + 1) * GAP_SIZE;
        unsigned short stackHeight =
            (screen_height - totalVerticalGaps - statusBarHeight) / stackCount;
        XMoveResizeWindow(dpy, CURRENT_DESKTOP.windows[0], GAP_SIZE, GAP_SIZE, masterWidth,
                          screen_height - 2 * GAP_SIZE - statusBarHeight);
        for (unsigned char i = 1; i < CURRENT_DESKTOP.windowCount; i++) {
                XMoveResizeWindow(dpy, CURRENT_DESKTOP.windows[i], masterWidth + 2 * GAP_SIZE,
                                  GAP_SIZE + (i - 1) * (stackHeight + GAP_SIZE), stackWidth,
                                  stackHeight);
        }
        XRaiseWindow(dpy, CURRENT_DESKTOP.windows[CURRENT_DESKTOP.focusedIdx]);
}
static void handleMapRequest(Window win) {
        for (unsigned char i = 0; i < CURRENT_DESKTOP.windowCount; i++) {
                if (CURRENT_DESKTOP.windows[i] == win) {
                        CURRENT_DESKTOP.focusedIdx = i;
                        if (!CURRENT_DESKTOP.isMapped[i]) {
                                XMapWindow(dpy, win);
                                CURRENT_DESKTOP.isMapped[i] = True;
                        }
                        focusWindow(win);
                        tileWindows();
                        return;
                }
        }
        if (CURRENT_DESKTOP.windowCount < MAX_WINDOWS_PER_DESKTOP) {
                unsigned char idx             = CURRENT_DESKTOP.windowCount;
                CURRENT_DESKTOP.windows[idx]  = win;
                CURRENT_DESKTOP.isMapped[idx] = True;
                CURRENT_DESKTOP.windowCount++;
                CURRENT_DESKTOP.focusedIdx = idx;
                XMapWindow(dpy, win);
                XSetWindowBorderWidth(dpy, win, BORDER_WIDTH);
                focusWindow(win);
                tileWindows();
        } else {
                XKillClient(dpy, win);
                need_flush = True;
        }
}
static void switchDesktop(unsigned char newDesk) {
        if (newDesk == currentDesktop || newDesk >= MAX_DESKTOPS || IsSwitching) return;
        IsSwitching = 1;
        for (unsigned int i = 0; i < CURRENT_DESKTOP.windowCount; i++) {
                if (CURRENT_DESKTOP.isMapped[i]) {
                        XUnmapWindow(dpy, CURRENT_DESKTOP.windows[i]);
                        CURRENT_DESKTOP.isMapped[i] = False;
                }
        }
        currentDesktop = newDesk;
        for (unsigned int i = 0; i < CURRENT_DESKTOP.windowCount; i++) {
                XMapWindow(dpy, CURRENT_DESKTOP.windows[i]);
                CURRENT_DESKTOP.isMapped[i] = True;
        }
        tileWindows();
        if (CURRENT_DESKTOP.windowCount > 0)
                focusWindow(CURRENT_DESKTOP.windows[CURRENT_DESKTOP.focusedIdx]);
        IsSwitching = 0;
}
static void handleUnmapNotify(Window win) {
        if (IsSwitching) return;
        for (unsigned char i = 0; i < CURRENT_DESKTOP.windowCount; i++) {
                if (CURRENT_DESKTOP.windows[i] == win) {
                        CURRENT_DESKTOP.isMapped[i] = False;
                        removeWindowFromDesktop(win, &CURRENT_DESKTOP);
                        tileWindows();
                        return;
                }
        }
}
