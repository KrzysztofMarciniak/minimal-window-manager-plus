# Minimal Window Manager (MWM)

MWM (Minimal Window Manager) is a lightweight, minimalistic window manager written in **C**. It was designed with the following guiding principles:
size:
```
   text	   data	    bss	    dec	    hex	filename
  12100	   1044	    568	  13712	   3590	mwm
```
bloaty:
```
    FILE SIZE        VM SIZE    
 --------------  -------------- 
  25.4%  6.38Ki   0.0%       0    [Unmapped]
  23.7%  5.95Ki  41.4%  5.88Ki    .text
   7.4%  1.87Ki   0.0%       0    .symtab
   5.2%  1.31Ki   2.8%     412    [16 Others]
   4.7%  1.19Ki   7.9%  1.12Ki    .dynsym
   4.3%  1.08Ki   0.0%       0    .strtab
   4.1%  1.02Ki   6.8%     984    .rela.plt
   3.1%     801   5.5%     801    [LOAD #2 [R]]
   2.9%     736   4.6%     672    .plt
   2.8%     720   4.5%     656    .plt.sec
   2.8%     711   4.4%     647    .dynstr
   2.5%     640   4.0%     576    .rodata
   2.2%     576   3.5%     512    .dynamic
   0.0%       0   3.9%     568    .bss
   2.1%     536   3.2%     472    .eh_frame
   1.8%     464   2.7%     400    .got
   1.7%     448   2.6%     384    .rela.dyn
   1.3%     340   0.0%       0    .shstrtab
   0.7%     172   0.7%     108    .eh_frame_hdr
   0.6%     160   0.7%      96    .data.rel.ro
   0.6%     160   0.7%      96    .gnu.version
 100.0%  25.1Ki 100.0%  14.2Ki    TOTAL
```
* < 500 lines of code (clang-formatted).
* High Performance.
* No status bar.
* No borders or gaps between windows.
* No floating windows.
* No compiler warnings, clean builds only.
* One file.

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
sudo make install
```
4. Add to xinitrc:

```bash
exec dbus-launch --sh-syntax --exit-with-session mwm
```

