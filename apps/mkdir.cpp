#include "include/libc.h"
#include "include/stdio.h"

int main(int argc, char *argv[]) {
  if (argc < 2) {
    fputs("usage: mkdir <directory>\n", stdout);
    return 1;
  }

  for (int i = 1; i < argc; i++) {
    if (syscall_mkdir(argv[i], 0755) < 0) {
      fputs("mkdir: cannot create directory '", stdout);
      fputs(argv[i], stdout);
      fputs("': Error\n", stdout);
    }
  }

  return 0;
}
