# Minimal Window Manager (MWM)

MWM (Minimal Window Manager) is a lightweight, minimalistic window manager written in **C**. It was designed with the following guiding principles:

* Less than 500 lines of code.
* No status bar required.
* Only 2 windows per desktop.
* No borders or gaps between windows.
* No floating windows.

## Getting Started
- **Mod + Enter**: Open terminal window.
- **Mod + p**: Launch dmenu.
- **Mod + q**: Close the focused window.
- **Mod + Shift + q**: Exit MWM.
- **Mod + j**: Focus on the left window.
- **Mod + l**: Focus on the right window.
- **Mod + [0-9]**: Switch to desktop [0-9].
- **Mod + Shift + [0-9]**: Move the focused window to desktop [0-9].

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
doas make install
```
4. Add to xinitrc:

```bash
exec dbus-launch --sh-syntax --exit-with-session mwm
```
