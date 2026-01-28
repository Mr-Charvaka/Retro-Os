#include "include/libc.h"
#include "include/stdio.h"
#include "include/types.h"

int main(int argc, char *argv[]) {
  char path[256];
  if (argc > 1) {
    strcpy(path, argv[1]);
  } else {
    getcwd(path, 256);
  }

  int fd = open(path, 0);
  if (fd < 0) {
    fputs("ls: cannot access '", stdout);
    fputs(path, stdout);
    fputs("': No such directory\n", stdout);
    return 1;
  }

  struct dirent de;
  int i = 0;
  while (syscall_readdir(fd, i++, &de) == 0) {
    if (de.d_name[0] == '.' &&
        (de.d_name[1] == 0 || (de.d_name[1] == '.' && de.d_name[2] == 0)))
      continue;

    // Simple coloring: Directories in Cyan (using ANSI codes supported by our
    // new terminal)
    if (de.d_type == 2) { // 2 = VFS_DIRECTORY
      fputs("\x1b[1;36m", stdout);
      fputs(de.d_name, stdout);
      fputs("\x1b[0m  ", stdout);
    } else {
      fputs(de.d_name, stdout);
      fputs("  ", stdout);
    }
  }
  fputs("\n", stdout);

  close(fd);
  return 0;
}
