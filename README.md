# Minimal Window Manager (MWM)

MWM (Minimal Window Manager) is a lightweight and minimalistic window manager written in **C**. This project is designed to provide a simple and efficient solution for managing windows in a graphical environment, with a focus on performance and simplicity.

## Repository Structure

- **Source Code**: The main functionality of the window manager is implemented in **C**.
- **Build System**: Utilizes a `Makefile` for building and managing the project.

## Getting Started

### Prerequisites

To build and run MWM, you will need:

- A C compiler (e.g., `gcc`).
- `make` (for building the project).
- A Linux-based environment with X11 support.

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

