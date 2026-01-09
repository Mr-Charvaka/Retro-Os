#include "os/ipc.hpp"
#include "os/syscalls.hpp"
#include "os_api.h"
#include "syscall.h"
#include "types.h"

// ------------------- Global IPC State -------------------
static OS::IPCClient *g_ipc = nullptr;
static MouseState g_mouse_state = {0, 0, false, false};

static void ensure_ipc() {
  if (!g_ipc) {
    g_ipc = new OS::IPCClient();
    if (!g_ipc->connect())
      return;
    if (!g_ipc->create_window("App", 800, 600))
      return;
  }
}

// ------------------- Graphics -------------------

extern "C" void gfx_text(int x, int y, const char *text, uint32_t color) {
  ensure_ipc();
  if (!g_ipc)
    return;
  // Ensure alpha is 0xFF if not specified
  if ((color >> 24) == 0)
    color |= 0xFF000000;
  int i = 0;
  while (text[i]) {
    // 5x8 block for characters
    g_ipc->fill_rect(x + i * 8, y, 4, 8, color);
    i++;
  }
}

// ------------------- Input -------------------

static KeyState g_key_state = {0, false};

static void poll_events() {
  ensure_ipc();
  if (g_ipc) {
    g_ipc->flush(); // Submit previous frame
    OS::gfx_msg_t msg;
    while (g_ipc->recv_msg(&msg)) {
      if (msg.type == OS::MSG_GFX_MOUSE_EVENT) {
        g_mouse_state.x = msg.data.mouse.x;
        g_mouse_state.y = msg.data.mouse.y;
        g_mouse_state.left = (msg.data.mouse.buttons & 1) != 0;
        g_mouse_state.right = (msg.data.mouse.buttons & 2) != 0;
      } else if (msg.type == OS::MSG_GFX_KEY_EVENT) {
        g_key_state.key = msg.data.key.key;
        g_key_state.pressed = true;
      }
    }
  }
}

extern "C" MouseState mouse_poll() {
  poll_events();
  return g_mouse_state;
}

extern "C" KeyState key_poll() {
  g_key_state.pressed = false;
  poll_events();
  return g_key_state;
}

extern "C" void gfx_clear(uint32_t color) {
  ensure_ipc();
  if (g_ipc) {
    if ((color >> 24) == 0)
      color |= 0xFF000000;
    g_ipc->fill_rect(0, 0, g_ipc->get_width(), g_ipc->get_height(), color);
  }
}

extern "C" void gfx_rect(int x, int y, int w, int h, uint32_t color) {
  ensure_ipc();
  if (g_ipc) {
    if ((color >> 24) == 0)
      color |= 0xFF000000;
    g_ipc->fill_rect(x, y, w, h, color);
  }
}

// ------------------- Filesystem -------------------

std::vector<FileEntry> os_fs_readdir(const std::string &path) {
  std::vector<FileEntry> entries;
  int fd = OS::Syscall::open(path.c_str(), 0);
  if (fd < 0)
    return entries;

  struct dirent de;
  int i = 0;
  while (OS::Syscall::readdir(fd, i++, &de) == 0) {
    if (de.d_name[0] == '.' &&
        (de.d_name[1] == 0 || (de.d_name[1] == '.' && de.d_name[2] == 0)))
      continue;
    FileEntry e;
    e.name = de.d_name;
    e.is_dir = (de.d_type == 0x02); // VFS_DIRECTORY
    entries.push_back(e);
    if (i > 100)
      break;
  }
  OS::Syscall::close(fd);
  return entries;
}

bool os_fs_mkdir(const std::string &path) {
  return OS::Syscall::mkdir(path.c_str(), 0) == 0;
}

bool os_fs_remove(const std::string &path) {
  // Try unlink (file)
  if (OS::Syscall::unlink(path.c_str()) == 0)
    return true;
  // syscall_rmdir is 55, not in syscalls.hpp yet. Use raw call.
  int res;
  asm volatile("int $0x80" : "=a"(res) : "a"(55), "b"(path.c_str()));
  return res == 0;
}

bool os_fs_exists(const std::string &path) {
  // access(path, F_OK) - 0 is F_OK
  int res;
  asm volatile("int $0x80" : "=a"(res) : "a"(41), "b"(path.c_str()), "c"(0));
  return res == 0;
}

// ------------------- Process -------------------

// Handled by Contracts.cpp
