#include "include/libc.h"
#include "include/stdio.h"

int main(int argc, char *argv[]) {
  if (argc < 2) {
    fputs("usage: cat <file>\n", stdout);
    return 1;
  }

  for (int i = 1; i < argc; i++) {
    int fd = open(argv[i], 0); // O_RDONLY
    if (fd < 0) {
      fputs("cat: ", stdout);
      fputs(argv[i], stdout);
      fputs(": No such file\n", stdout);
      continue;
    }

    char buf[1024];
    int n;
    while ((n = read(fd, buf, sizeof(buf))) > 0) {
      write(1, buf, n);
    }
    close(fd);
  }

  return 0;
}
