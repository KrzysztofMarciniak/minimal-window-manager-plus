# Minimal Window Manager (MWM)

MWM (Minimal Window Manager) is a lightweight, minimalistic window manager written in **C**. It was designed with the following guiding principles:

* Less than 500 lines of code (clang-formatted).
* No status bar required.
* Only 2 windows per desktop.
* No borders or gaps between windows.
* No floating windows.
* No compiler warnings.

## Getting Started
### Keyboard Shortcuts

#### Window Management
- **Mod + Enter**: Launch terminal
- **Mod + q**: Close focused window
- **Mod + Shift + q**: Exit MWM
- **Mod + j**: Focus left window
- **Mod + l**: Focus right window
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

1. Clone the repository:
```bash
git clone https://github.com/KrzysztofMarciniak/minimal-window-manager.git
```
2. Navigate to the project directory: 
```bash
cd minimal-window-manager
```
3. Install:
```bash
if command -v doas > /dev/null; then
  doas make install
else
  sudo make install
fi
```
4. Add to xinitrc:

```bash
exec dbus-launch --sh-syntax --exit-with-session mwm
```
