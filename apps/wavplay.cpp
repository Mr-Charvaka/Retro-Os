#include "include/libc.h"
#include "include/syscall.h"

#define BUF_SIZE 32768
#define IBLOCK_SIZE 44

extern "C" int main(int argc, char **argv) {
  if (argc < 2) {
    syscall_print("Usage: wavplay <filename.wav>\n");
    return 1;
  }

  int fd = open(argv[1], O_RDONLY);
  if (fd < 0) {
    syscall_print("Could not open file\n");
    return 1;
  }

  uint8_t header[IBLOCK_SIZE];
  read(fd, header, IBLOCK_SIZE);

  // SYS_AUDIO_INIT (162)
  int audio_buffer_user;
  asm volatile("int $0x80" : "=a"(audio_buffer_user) : "a"(162), "b"(header));

  if (audio_buffer_user == -1) {
    syscall_print("Audio init failed. Valid 16-bit WAV?\n");
    close(fd);
    return 1;
  }

  uint8_t *buf0 = (uint8_t *)(uintptr_t)audio_buffer_user;
  uint8_t *buf1 = (uint8_t *)(uintptr_t)audio_buffer_user + BUF_SIZE;

  syscall_print("Playing audio...\n");

  // Initial load
  read(fd, buf0, BUF_SIZE);
  read(fd, buf1, BUF_SIZE);

  int prev_status = 1;
  while (1) {
    int status;
    // SYS_AUDIO_CSTATUS (163)
    asm volatile("int $0x80" : "=a"(status) : "a"(163));

    if (status != prev_status) {
      // Fill the buffer that just finished playing
      uint8_t *target = (status == 0) ? buf0 : buf1;
      int n = read(fd, target, BUF_SIZE);
      if (n <= 0)
        break;
      prev_status = status;
    }
    // Small sleep to avoid eating CPU
    for (volatile int i = 0; i < 10000; i++)
      ;
  }

  // SYS_AUDIO_SHUTDOWN (164)
  asm volatile("int $0x80" : : "a"(164));

  close(fd);
  syscall_print("Done.\n");
  return 0;
}
