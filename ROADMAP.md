# Retro-OS Future Roadmap

## 1. User Experience & Applications
- [ ] **Notepad / Text Editor**: A full GUI text editor with file open/save dialogs, scrolling, and basic text manipulation (copy/paste). (Partially started with `textview.cpp`)
- [ ] **Paint Application**: A basic bitmap drawing tool to create and edit images (BMP support).
- [ ] **Image Viewer**: Enhance `textview` or create a new app to display images (BMP, maybe PNG) from the filesystem.
- [ ] **Games**: Port simple games like Minesweeper, Snake, or Tic-Tac-Toe to the GUI.
- [ ] **Settings Panel**: A dedicated app to change system themes, wallpapers, resolution, or mouse sensitivity.

## 2. Core Shell & Utilities
- [ ] **Pipe Support**: Implement `|` in the shell to pipe output between processes (e.g., `ls | grep foo`).
- [ ] **Redirection**: Implement `>` and `<` for file I/O redirection.
- [ ] **Job Control**: Support `Ctrl+C` to kill processes, and backgrounding (`&`).
- [ ] **More Commands**: `cp` (copy), `mv` (move), `rm` (remove), `grep`, `wc`, `diff`.

## 3. System Internals
- [ ] **Dynamic Linking**: Support shared libraries (`.so` or `.dll`) to reduce executable size.
- [ ] **Networking Stack**: Implement a basic TCP/IP stack (ARP, IP, ICMP, UDP, maybe TCP) and a network driver (e.g., for QEMU's e1000 or rtl8139).
- [ ] **Sound Support**: Implement a driver for AC97 or SoundBlaster 16 to play audio.
- [ ] **USB Support**: Basic USB controller (UHCI/EHCI) to support USB keyboards/mice/drives (currently relying on PS/2).

## 4. GUI Enhancements
- [ ] **Window Resizing**: Refine the resizing logic (visual feedback while dragging borders).
- [ ] **Z-Order Management**: Ensure focused windows are always properly drawn on top (already partially done).
- [ ] **Font Rendering**: Support TrueType fonts (using something like `stb_truetype`) for smoother text.
- [ ] **Compositor**: Implement alpha blending and window transparency/blur effects.

## 5. Stability & Testing
- [ ] **Unit Tests**: Create a suite of unit tests for the kernel (VFS, PMM, scheduler).
- [ ] **Stress Testing**: Run the OS for extended periods under load to find memory leaks.
