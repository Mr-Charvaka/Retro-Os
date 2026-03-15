#include <os/syscalls.hpp>
#include <syscall.h> // For O_ flags

extern "C" void _start() {
  OS::Syscall::print("--- Retro-OS Chrome Installer ---\n");

  const char *src_path = "/C/Network/chrome_setup.exe";
  const char *dst_dir1 = "/C/Program Files/Google";
  const char *dst_dir2 = "/C/Program Files/Google/Chrome";
  const char *dst_path = "/C/Program Files/Google/Chrome/chrome.exe";

  OS::Syscall::print("Step 1: Connecting to host via Network Share...\n");

  int src_fd = OS::Syscall::open(src_path, O_RDONLY);
  if (src_fd < 0) {
    OS::Syscall::print("ERROR: chrome_setup.exe NOT found on Network Drive!\n");
    OS::Syscall::print("GUIDE: Put a file named 'chrome_setup.exe' in your "
                       "Windows 'apps' folder.\n");
    OS::Syscall::exit(1);
  }

  OS::Syscall::print("Step 2: Preparing local C: Drive directories...\n");
  OS::Syscall::mkdir(dst_dir1, 0);
  OS::Syscall::mkdir(dst_dir2, 0);

  OS::Syscall::print("Step 3: Transferring binary data [");

  int dst_fd = OS::Syscall::open(dst_path, O_CREAT | O_WRONLY | O_TRUNC);
  if (dst_fd < 0) {
    OS::Syscall::print("] ERROR: Permission denied on C: Drive.\n");
    OS::Syscall::close(src_fd);
    OS::Syscall::exit(1);
  }

  char buffer[8192];
  int bytes;
  int chunk_count = 0;
  while ((bytes = OS::Syscall::read(src_fd, buffer, sizeof(buffer))) > 0) {
    OS::Syscall::write(dst_fd, buffer, bytes);
    chunk_count++;
    if (chunk_count % 128 == 0) { // Approx every 1MB
      OS::Syscall::print("#");
    }
  }

  OS::Syscall::close(src_fd);
  OS::Syscall::close(dst_fd);

  OS::Syscall::print("] 100%\n");
  OS::Syscall::print(
      "SUCCESS: Google Chrome has been installed to C:\\Program Files.\n");
  OS::Syscall::print("You can now launch it from the Desktop!\n");

  OS::Syscall::exit(0);
}
