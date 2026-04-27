#include <Contracts.h>
#include <string>
#include <vector>

// ------------------- Minimal OS API -------------------

// graphics
extern "C" void gfx_clear(uint32_t color);
extern "C" void gfx_rect(int x, int y, int w, int h, uint32_t color);
extern "C" void gfx_text(int x, int y, const char *text, uint32_t color);

// input
struct MouseState {
  int x, y;
  bool left, right;
};
extern "C" MouseState mouse_poll();

struct KeyState {
  char key;
  bool pressed;
};
extern "C" KeyState key_poll();

// filesystem
struct FileEntry {
  std::string name;
  bool is_dir;
};
extern std::vector<FileEntry> os_fs_readdir(const std::string &path);
extern bool os_fs_mkdir(const std::string &path);
extern bool os_fs_remove(const std::string &path);
extern bool os_fs_exists(const std::string &path);

// process
extern "C" PID spawn_process(const char *path);

// Utility
extern "C" void *kmalloc(size_t size);
extern "C" void kfree(void *ptr);
