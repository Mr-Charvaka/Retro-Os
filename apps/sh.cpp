#include "include/libc.h"
#include "include/stdio.h"

// Shell specific helpers
void shell_print(const char *s) { fputs(s, stdout); }

void print_prompt() {
  char cwd[256];
  getcwd(cwd, 256);
  shell_print("retro@os:");
  shell_print(cwd);
  shell_print("$ ");
}

int main() {
  char cmd[256];
  shell_print("\n\x1b[1;36mRetro-OS Shell v1.1.0\x1b[0m\n");
  shell_print("Type 'help' for commands.\n\n");

  while (1) {
    print_prompt();

    if (fgets(cmd, 256, stdin) == NULL) {
      shell_print("\n");
      break;
    }

    // Remove trailing newline
    int len = strlen(cmd);
    if (len > 0 && cmd[len - 1] == '\n') {
      cmd[len - 1] = 0;
      len--;
    }

    if (len == 0)
      continue;

    // Built-in commands
    if (strcmp(cmd, "exit") == 0) {
      break;
    } else if (strcmp(cmd, "help") == 0) {
      shell_print("Built-in commands: cd, help, exit, clear, pwd\n");
      shell_print("External: ls, cat, hello, df, stat, rm, mkdir\n");
      continue;
    } else if (strcmp(cmd, "clear") == 0) {
      shell_print("\x1b[2J\x1b[H");
      continue;
    } else if (strcmp(cmd, "pwd") == 0) {
      char cwd[256];
      getcwd(cwd, 256);
      shell_print(cwd);
      shell_print("\n");
      continue;
    } else if (strncmp(cmd, "cd ", 3) == 0) {
      if (chdir(cmd + 3) < 0) {
        shell_print("sh: cd: no such directory\n");
      }
      continue;
    }

    // External commands
    int pid = fork();
    if (pid == 0) {
      // Child process
      char path[256];
      char *argv[2];

      // Try absolute/relative path
      if (cmd[0] == '/' || cmd[0] == '.') {
        strcpy(path, cmd);
      } else {
        // Try /apps/ first
        strcpy(path, "/apps/");
        strcat(path, cmd);
        if (strstr(path, ".elf") == 0)
          strcat(path, ".elf");
      }

      argv[0] = path;
      argv[1] = NULL;
      execve(path, argv, NULL);

      // Try /bin/
      if (cmd[0] != '/' && cmd[0] != '.') {
        strcpy(path, "/bin/");
        strcat(path, cmd);
        if (strstr(path, ".elf") == 0)
          strcat(path, ".elf");
        execve(path, argv, NULL);
      }

      // Try root /
      if (cmd[0] != '/' && cmd[0] != '.') {
        strcpy(path, "/");
        strcat(path, cmd);
        if (strstr(path, ".elf") == 0)
          strcat(path, ".elf");
        execve(path, argv, NULL);
      }

      shell_print("sh: command not found: ");
      shell_print(cmd);
      shell_print("\n");
      exit(1);
    } else if (pid > 0) {
      // Parent process
      int status;
      wait(&status);
    } else {
      shell_print("sh: fork failed\n");
    }
  }

  return 0;
}
