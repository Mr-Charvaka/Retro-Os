// ============================================================================
// termios.cpp - Terminal I/O Implementation
// Entirely hand-crafted for Retro-OS kernel
// ============================================================================

#include "../include/termios.h"
#include "../drivers/serial.h"
#include "../include/errno.h"
#include "../include/string.h"
#include "process.h"

extern "C" {

// ============================================================================
// Default Terminal Settings
// ============================================================================
static struct termios default_termios;

static void init_default_termios() {
  static bool initialized = false;
  if (initialized)
    return;
  initialized = true;

  default_termios.c_iflag = ICRNL | IXON;  // Map CR to NL, enable XON/XOFF
  default_termios.c_oflag = OPOST | ONLCR; // Enable output processing, NL->CRNL
  default_termios.c_cflag =
      CS8 | CREAD | CLOCAL; // 8 bits, enable receiver, ignore modem
  default_termios.c_lflag =
      ISIG | ICANON | ECHO | ECHOE | ECHOK | IEXTEN; // Canonical mode with echo
  default_termios.c_line = 0;

  // Initialize c_cc array
  for (int i = 0; i < NCCS; i++)
    default_termios.c_cc[i] = 0;
  default_termios.c_cc[VINTR] = 3;  // ^C
  default_termios.c_cc[VQUIT] = 28; // ^\ 
  default_termios.c_cc[VERASE] = 127;  // DEL
  default_termios.c_cc[VKILL] = 21; // ^U
  default_termios.c_cc[VEOF] = 4;   // ^D
  default_termios.c_cc[VTIME] = 0;
  default_termios.c_cc[VMIN] = 1;
  default_termios.c_cc[VSWTC] = 0;
  default_termios.c_cc[VSTART] = 17; // ^Q
  default_termios.c_cc[VSTOP] = 19;  // ^S
  default_termios.c_cc[VSUSP] = 26;  // ^Z
  default_termios.c_cc[VEOL] = 0;
  default_termios.c_cc[VREPRINT] = 18; // ^R
  default_termios.c_cc[VDISCARD] = 15; // ^O
  default_termios.c_cc[VWERASE] = 23;  // ^W
  default_termios.c_cc[VLNEXT] = 22;   // ^V
  default_termios.c_cc[VEOL2] = 0;

  default_termios.c_ispeed = B9600;
  default_termios.c_ospeed = B9600;
}

// ============================================================================
// Per-TTY Terminal Settings (simplified - just one for now)
// ============================================================================
#define MAX_TTYS 8
static struct termios tty_settings[MAX_TTYS];
static int tty_initialized[MAX_TTYS] = {0};

static int get_tty_index(int fd) {
  // Map fd to tty index (simplified)
  if (fd >= 0 && fd <= 2)
    return 0; // stdin/stdout/stderr share settings
  return -1;
}

static void init_tty(int idx) {
  if (idx < 0 || idx >= MAX_TTYS)
    return;
  if (!tty_initialized[idx]) {
    tty_settings[idx] = default_termios;
    tty_initialized[idx] = 1;
  }
}

// ============================================================================
// tcgetattr - Get terminal attributes
// ============================================================================
int tcgetattr(int fd, struct termios *termios_p) {
  if (!termios_p)
    return -EFAULT;

  int idx = get_tty_index(fd);
  if (idx < 0)
    return -ENOTTY;

  init_tty(idx);
  *termios_p = tty_settings[idx];
  return 0;
}

// ============================================================================
// tcsetattr - Set terminal attributes
// ============================================================================
int tcsetattr(int fd, int optional_actions, const struct termios *termios_p) {
  if (!termios_p)
    return -EFAULT;

  int idx = get_tty_index(fd);
  if (idx < 0)
    return -ENOTTY;

  init_tty(idx);

  // Optional actions (TCSANOW, TCSADRAIN, TCSAFLUSH)
  // For now, we apply immediately
  (void)optional_actions;

  tty_settings[idx] = *termios_p;
  return 0;
}

// ============================================================================
// tcsendbreak - Send break
// ============================================================================
int tcsendbreak(int fd, int duration) {
  (void)duration;

  int idx = get_tty_index(fd);
  if (idx < 0)
    return -ENOTTY;

  // Break not actually implemented
  return 0;
}

// ============================================================================
// tcdrain - Wait for output to complete
// ============================================================================
int tcdrain(int fd) {
  int idx = get_tty_index(fd);
  if (idx < 0)
    return -ENOTTY;

  // For now, output is immediate
  return 0;
}

// ============================================================================
// tcflush - Discard queued data
// ============================================================================
int tcflush(int fd, int queue_selector) {
  int idx = get_tty_index(fd);
  if (idx < 0)
    return -ENOTTY;

  switch (queue_selector) {
  case TCIFLUSH:
  case TCOFLUSH:
  case TCIOFLUSH:
    // Flush not fully implemented
    return 0;
  default:
    return -EINVAL;
  }
}

// ============================================================================
// tcflow - Flow control
// ============================================================================
int tcflow(int fd, int action) {
  int idx = get_tty_index(fd);
  if (idx < 0)
    return -ENOTTY;

  switch (action) {
  case TCOOFF:
  case TCOON:
  case TCIOFF:
  case TCION:
    // Flow control not fully implemented
    return 0;
  default:
    return -EINVAL;
  }
}

// ============================================================================
// Baud Rate Functions
// ============================================================================
speed_t cfgetispeed(const struct termios *termios_p) {
  if (!termios_p)
    return 0;
  return termios_p->c_ispeed;
}

speed_t cfgetospeed(const struct termios *termios_p) {
  if (!termios_p)
    return 0;
  return termios_p->c_ospeed;
}

int cfsetispeed(struct termios *termios_p, speed_t speed) {
  if (!termios_p)
    return -1;
  termios_p->c_ispeed = speed;
  return 0;
}

int cfsetospeed(struct termios *termios_p, speed_t speed) {
  if (!termios_p)
    return -1;
  termios_p->c_ospeed = speed;
  return 0;
}

int cfsetspeed(struct termios *termios_p, speed_t speed) {
  if (!termios_p)
    return -1;
  termios_p->c_ispeed = speed;
  termios_p->c_ospeed = speed;
  return 0;
}

// ============================================================================
// cfmakeraw - Set raw mode
// ============================================================================
void cfmakeraw(struct termios *termios_p) {
  if (!termios_p)
    return;

  termios_p->c_iflag &=
      ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON);
  termios_p->c_oflag &= ~OPOST;
  termios_p->c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
  termios_p->c_cflag &= ~(CSIZE | PARENB);
  termios_p->c_cflag |= CS8;
  termios_p->c_cc[VMIN] = 1;
  termios_p->c_cc[VTIME] = 0;
}

// ============================================================================
// isatty - Test if fd is a terminal
// ============================================================================
int isatty(int fd) {
  int idx = get_tty_index(fd);
  return idx >= 0;
}

// ============================================================================
// ttyname - Get terminal name
// ============================================================================
static char ttyname_buf[16] = "/dev/tty";

char *ttyname(int fd) {
  if (get_tty_index(fd) < 0)
    return 0;
  return ttyname_buf;
}

int ttyname_r(int fd, char *buf, size_t buflen) {
  if (get_tty_index(fd) < 0)
    return -ENOTTY;

  if (!buf || buflen < 9)
    return -EINVAL;

  strcpy(buf, "/dev/tty");
  return 0;
}

} // extern "C"
