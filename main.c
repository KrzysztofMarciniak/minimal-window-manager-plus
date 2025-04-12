#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/XKBlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdbool.h>
#include <sys/wait.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/select.h>

#define MAX_DESKTOPS 9
#define MAX_WINDOWS_PER_DESKTOP 2
#define MOD_KEY Mod4Mask

typedef struct {
    Window windows[MAX_WINDOWS_PER_DESKTOP];
    unsigned char windowCount;
    unsigned char focusedIdx;
} Desktop;

typedef struct {
    KeySym keysym;
    unsigned int modifiers;
    const char *command;
} AppLauncher;

static Display *dpy;
static Window root;
static Desktop desktops[MAX_DESKTOPS];
static int currentDesktop = 0;
static volatile sig_atomic_t running = 1;
static int screen_width, screen_height;

static const AppLauncher launchers[] = {
    { XK_Return, 0, "st" },
    { XK_p, 0, "dmenu_run" }
};

static void setup(void);
static void run(void);
static void cleanup(void);
static void handleKeyPress(XEvent *e);
static void handleMapRequest(XEvent *e);
static void handleUnmapNotify(XEvent *e);
static void handleDestroyNotify(XEvent *e);
static void handleConfigureNotify(XEvent *e);
static void focusWindow(Window w);
static void tileWindows(void);
static void switchDesktop(int desktop);
static void moveWindowToDesktop(Window win, int desktop);
static int findWindowInCurrentDesktop(Window w);
static void grabKeys(void);
static void sigHandler(int);
static int xerrorstart(Display*, XErrorEvent*);
static int xerror(Display*, XErrorEvent*);
static void killFocusedWindow(void);
static void focusNextWindow(void);
static void focusPrevWindow(void);

int main(void) {
    signal(SIGTERM, sigHandler);
    signal(SIGINT, sigHandler);
    setup();
    run();
    cleanup();
    return 0;
}

static void sigHandler(int sig) {
    (void)sig;
    running = 0;
}
static int xerrorstart(Display *dpy, XErrorEvent *ee) {
    (void)dpy;
    (void)ee;
    die("another window manager is already running");
}

static int xerror(Display *dpy, XErrorEvent *ee) {
    (void)dpy;
    (void)ee;
    return 0;
}

static void die(const char *msg) {
    write(STDERR_FILENO, "mwm: ", 5);
    write(STDERR_FILENO, msg, strlen(msg));
    write(STDERR_FILENO, "\n", 1);
    exit(EXIT_FAILURE);
}

static void setup(void) {
    if (!getenv("DISPLAY")) die("DISPLAY not set");
    if (!(dpy = XOpenDisplay(NULL))) die("cannot open display");

    root = DefaultRootWindow(dpy);
    
    memset(desktops, 0, sizeof(desktops));
    
    XWindowAttributes attr;
    XGetWindowAttributes(dpy, root, &attr);
    screen_width = attr.width;
    screen_height = attr.height;
    
    XSetErrorHandler(xerrorstart);
    XSelectInput(dpy, root, SubstructureRedirectMask | SubstructureNotifyMask | StructureNotifyMask);
    XSync(dpy, False);
    XSetErrorHandler(xerror);
    
    grabKeys();
    XSync(dpy, False);
}

static void grabKeys(void) {
    unsigned int modifiers[] = {MOD_KEY, MOD_KEY|ShiftMask};
    KeyCode keycodes[20];
    int k = 0;
    
    for (int i = 0; i < MAX_DESKTOPS; i++)
        keycodes[k++] = XKeysymToKeycode(dpy, XK_1 + i);
    
    keycodes[k++] = XKeysymToKeycode(dpy, XK_q);
    keycodes[k++] = XKeysymToKeycode(dpy, XK_j);
    keycodes[k++] = XKeysymToKeycode(dpy, XK_l);
    
    for (int i = 0; i < k; i++)
        for (unsigned int j = 0; j < sizeof(modifiers)/sizeof(modifiers[0]); j++)
            if (keycodes[i] != 0)
                XGrabKey(dpy, keycodes[i], modifiers[j], root, True, GrabModeAsync, GrabModeAsync);
    
    for (size_t i = 0; i < sizeof(launchers)/sizeof(launchers[0]); i++) {
        KeyCode code = XKeysymToKeycode(dpy, launchers[i].keysym);
        if (code != 0)
            XGrabKey(dpy, code, MOD_KEY | launchers[i].modifiers, root, True, GrabModeAsync, GrabModeAsync);
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
                case KeyPress: handleKeyPress(&e); break;
                case MapRequest: handleMapRequest(&e); break;
                case UnmapNotify: handleUnmapNotify(&e); break;
                case DestroyNotify: handleDestroyNotify(&e); break;
                case ConfigureNotify: handleConfigureNotify(&e); break;
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
    if (d->windowCount <= 0) return;

    Window win = d->windows[d->focusedIdx];
    if (win == None || win == root) return;
    
    Atom *p;
    int n;
    Atom del = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
    
    if (XGetWMProtocols(dpy, win, &p, &n)) {
        while (n--) if (p[n] == del) goto send;
        XKillClient(dpy, win);
        XFree(p);
        return;
    send:
        XEvent ev;
        memset(&ev, 0, sizeof(ev));
        ev.type = ClientMessage;
        ev.xclient.window = win;
        ev.xclient.message_type = XInternAtom(dpy, "WM_PROTOCOLS", False);
        ev.xclient.format = 32;
        ev.xclient.data.l[0] = del;
        ev.xclient.data.l[1] = CurrentTime;
        XSendEvent(dpy, win, False, NoEventMask, &ev);
        XFree(p);
    } else {
        XKillClient(dpy, win);
    }
    XFlush(dpy);
}

static void focusNextWindow(void) {
    Desktop *d = &desktops[currentDesktop];
    if (d->windowCount > 1) {
        d->focusedIdx = (d->focusedIdx + 1) % d->windowCount;
        focusWindow(d->windows[d->focusedIdx]);
    }
}

static void focusPrevWindow(void) {
    Desktop *d = &desktops[currentDesktop];
    if (d->windowCount > 1) {
        d->focusedIdx = (d->focusedIdx + d->windowCount - 1) % d->windowCount;
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
    
    if (keysym == XK_q && state == MOD_KEY) {
        killFocusedWindow();
        return;
    }
    
    if (keysym == XK_j && state == MOD_KEY) {
        focusNextWindow();
        return;
    }
    
    if (keysym == XK_l && state == MOD_KEY) {
        focusPrevWindow();
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
        if (keysym == launchers[i].keysym && 
            state == (MOD_KEY | launchers[i].modifiers)) {
            
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

static void handleMapRequest(XEvent *e) {
    XMapRequestEvent *ev = &e->xmaprequest;
    Desktop *d = &desktops[currentDesktop];
    
    if (d->windowCount < MAX_WINDOWS_PER_DESKTOP) {
        int idx = d->windowCount;
        d->windows[idx] = ev->window;
        d->windowCount++;
        d->focusedIdx = idx;
        
        XMapWindow(dpy, ev->window);
        focusWindow(ev->window);
        tileWindows();
    }
}

static void handleUnmapNotify(XEvent *e) {
    XUnmapEvent *ev = &e->xunmap;
    
    if (ev->send_event) return;
    
    int idx = findWindowInCurrentDesktop(ev->window);
    if (idx != -1) {
        Desktop *d = &desktops[currentDesktop];
        
        memmove(&d->windows[idx], &d->windows[idx+1], 
                (d->windowCount - idx - 1) * sizeof(Window));
        
        d->windowCount--;
        
        if (d->focusedIdx >= d->windowCount)
            d->focusedIdx = d->windowCount > 0 ? d->windowCount - 1 : 0;
            
        tileWindows();
    }
}

static void handleDestroyNotify(XEvent *e) {
    XDestroyWindowEvent *ev = &e->xdestroywindow;
    
    int idx = findWindowInCurrentDesktop(ev->window);
    if (idx != -1) {
        Desktop *d = &desktops[currentDesktop];
        
        memmove(&d->windows[idx], &d->windows[idx+1], 
                (d->windowCount - idx - 1) * sizeof(Window));
        
        d->windowCount--;
        
        if (d->focusedIdx >= d->windowCount)
            d->focusedIdx = d->windowCount > 0 ? d->windowCount - 1 : 0;
            
        tileWindows();
    }
}

static void focusWindow(Window w) {
    XSetInputFocus(dpy, w, RevertToParent, CurrentTime);
}

static void tileWindows(void) {
    Desktop *d = &desktops[currentDesktop];
    
    if (d->windowCount == 1) {
        XMoveResizeWindow(dpy, d->windows[0], 0, 0, screen_width, screen_height);
    } else if (d->windowCount == 2) {
        int half_width = screen_width / 2;
        XMoveResizeWindow(dpy, d->windows[0], 0, 0, half_width, screen_height);
        XMoveResizeWindow(dpy, d->windows[1], half_width, 0, half_width, screen_height);
    }
}

static void switchDesktop(int desktop) {
    if (desktop == currentDesktop || desktop < 0 || desktop >= MAX_DESKTOPS) return;
    
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

static void moveWindowToDesktop(Window win, int desktop) {
    if (desktop < 0 || desktop >= MAX_DESKTOPS || desktop == currentDesktop) return;
    
    Desktop *target = &desktops[desktop];
    if (target->windowCount >= MAX_WINDOWS_PER_DESKTOP) return;
    
    int idx = findWindowInCurrentDesktop(win);
    if (idx == -1) return;
    
    Desktop *current = &desktops[currentDesktop];
    
    target->windows[target->windowCount] = win;
    target->windowCount++;
    
    for (int i = idx; i < current->windowCount - 1; i++) {
        current->windows[i] = current->windows[i + 1];
    }
    current->windowCount--;
    
    if (current->focusedIdx >= current->windowCount)
        current->focusedIdx = current->windowCount > 0 ? current->windowCount - 1 : 0;
    
    XUnmapWindow(dpy, win);
    tileWindows();
}

int findWindowInCurrentDesktop(Window w) 
{
    for (int i = 0; i < desktops[currentDesktop].windowCount; i++) {
        if (desktops[currentDesktop].windows[i] == w) {
            return i;
        }
    }
    return -1;
}

void cleanup() 
{
    for (int d = 0; d < MAX_DESKTOPS; d++) {
        for (int i = 0; i < desktops[d].windowCount; i++) {
            XUnmapWindow(dpy, desktops[d].windows[i]);
        }
    }
    
    XCloseDisplay(dpy);
}
