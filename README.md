# Minimal Window Manager + (MWM)

MWM+ Expanded version of [MWM](https://github.com/KrzysztofMarciniak/minimal-window-manager) with extra features:

* Status Bar
* Gaps and Borders

# Getting Started

### Media Keys
Below are two examples for handling media keys. You can modify these to suit your needs. To use them out of the box, you will need [herbe](https://github.com/dudik/herbe), or modify (`notifications_daemon="/usr/local/bin/herbe"` - .sh) or (`#define NOTIFICATIONS_DAEMON "/usr/local/bin/herbe"` - .c)
Makefile:
```bash
-DAUDIO_SCRIPT="\"$(shell pwd)/audio.sh\""
```
or (after compiling `gcc audio.sh -o audio`):
```bash
-DAUDIO_SCRIPT="\"$(shell pwd)/audio\""
```

### Keyboard Shortcuts

#### Window Management
- **Mod + Enter**: Launch terminal
- **Mod + q**: Close focused window
- **Mod + Shift + q**: Exit MWM
- **Mod + j**: Focus left window
- **Mod + k**: Focus right window
- **Mod + Shift + l**: Increase window size
- **Mod + Shift + h**: Decrease window size

#### Desktop Navigation
- **Mod + [0-9]**: Switch to desktop [0-9]
- **Mod + Shift + [0-9]**: Move focused window to desktop [0-9]

#### Application Launcher
- **Mod + p**: Launch dmenu (application menu)
## Prerequisites
To build and run MWM, you'll need:
- A C compiler (e.g., `gcc`).
- `make` for building the project.
- A Linux-based environment with X11 support.

Optional, but recommended:
- `st` (simple terminal).
- `dmenu` (for launching applications).

### Building the Project
#### default (`recommended`) (lower in runtime memory but bigger disk size.):
1. Clone the repository:
```bash
git clone https://github.com/KrzysztofMarciniak/minimal-window-manager-plus.git
```
2. Navigate to the project directory: 
```bash
cd minimal-window-manager-plus
```
3. Install:
```bash
sudo make install
```
4. Add to xinitrc:

```bash
exec dbus-launch --sh-syntax --exit-with-session mwmp
```
#### compressed (higher in runtime memory, smaller in disk size.):
0. You have to install [upx](https://github.com/upx/upx).
1. Clone the repository:
```bash
git clone https://github.com/KrzysztofMarciniak/minimal-window-manager-plus.git
```
2. Navigate to the project directory: 
```bash
cd minimal-window-manager-plus
```
3. Install the compressed version:
```bash
sudo make install_compressed
```
4. Add to xinitrc:
```bash
exec dbus-launch --sh-syntax --exit-with-session mwmp.upx
```
