#ifndef TERMIOS_H
#define TERMIOS_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Terminal Control Characters
// ============================================================================
#define NCCS 32

// Control character indices
#define VINTR 0     // Interrupt character (SIGINT)
#define VQUIT 1     // Quit character (SIGQUIT)
#define VERASE 2    // Erase character
#define VKILL 3     // Kill line character
#define VEOF 4      // End-of-file character
#define VTIME 5     // Timeout for noncanonical read
#define VMIN 6      // Minimum characters for noncanonical read
#define VSWTC 7     // Switch character
#define VSTART 8    // Start output character
#define VSTOP 9     // Stop output character
#define VSUSP 10    // Suspend character (SIGTSTP)
#define VEOL 11     // End-of-line character
#define VREPRINT 12 // Reprint character
#define VDISCARD 13 // Discard character
#define VWERASE 14  // Word erase character
#define VLNEXT 15   // Literal next character
#define VEOL2 16    // Second end-of-line character

// ============================================================================
// Input Mode Flags (c_iflag)
// ============================================================================
#define IGNBRK 0x00001  // Ignore break condition
#define BRKINT 0x00002  // Signal interrupt on break
#define IGNPAR 0x00004  // Ignore parity errors
#define PARMRK 0x00008  // Mark parity errors
#define INPCK 0x00010   // Enable input parity check
#define ISTRIP 0x00020  // Strip off eighth bit
#define INLCR 0x00040   // Map NL to CR on input
#define IGNCR 0x00080   // Ignore CR
#define ICRNL 0x00100   // Map CR to NL on input
#define IUCLC 0x00200   // Map uppercase to lowercase
#define IXON 0x00400    // Enable start/stop output control
#define IXANY 0x00800   // Any character restarts output
#define IXOFF 0x01000   // Enable start/stop input control
#define IMAXBEL 0x02000 // Ring bell when queue full
#define IUTF8 0x04000   // Input is UTF-8

// ============================================================================
// Output Mode Flags (c_oflag)
// ============================================================================
#define OPOST 0x0001  // Enable output processing
#define OLCUC 0x0002  // Map lowercase to uppercase on output
#define ONLCR 0x0004  // Map NL to CR-NL on output
#define OCRNL 0x0008  // Map CR to NL on output
#define ONOCR 0x0010  // No CR at column 0
#define ONLRET 0x0020 // NL performs CR function
#define OFILL 0x0040  // Use fill characters for delay
#define OFDEL 0x0080  // Fill is DEL
#define NLDLY 0x0100  // NL delay mask
#define CRDLY 0x0600  // CR delay mask
#define TABDLY 0x1800 // TAB delay mask
#define BSDLY 0x2000  // BS delay mask
#define VTDLY 0x4000  // VT delay mask
#define FFDLY 0x8000  // FF delay mask

// ============================================================================
// Control Mode Flags (c_cflag)
// ============================================================================
#define CSIZE 0x0030  // Character size mask
#define CS5 0x0000    // 5 bits
#define CS6 0x0010    // 6 bits
#define CS7 0x0020    // 7 bits
#define CS8 0x0030    // 8 bits
#define CSTOPB 0x0040 // 2 stop bits
#define CREAD 0x0080  // Enable receiver
#define PARENB 0x0100 // Enable parity
#define PARODD 0x0200 // Odd parity
#define HUPCL 0x0400  // Hang up on last close
#define CLOCAL 0x0800 // Ignore modem control lines

// ============================================================================
// Local Mode Flags (c_lflag)
// ============================================================================
#define ISIG 0x0001    // Enable signals
#define ICANON 0x0002  // Canonical mode
#define XCASE 0x0004   // Canonical upper/lower presentation
#define ECHO 0x0008    // Enable echo
#define ECHOE 0x0010   // Echo erase as backspace-space-backspace
#define ECHOK 0x0020   // Echo NL after kill
#define ECHONL 0x0040  // Echo NL even if ECHO is off
#define NOFLSH 0x0080  // Don't flush after interrupt
#define TOSTOP 0x0100  // Stop background jobs that write to terminal
#define ECHOCTL 0x0200 // Echo control characters as ^X
#define ECHOPRT 0x0400 // Echo erased characters
#define ECHOKE 0x0800  // Erase characters echoed with
#define FLUSHO 0x1000  // Output being flushed
#define PENDIN 0x2000  // Retype pending input
#define IEXTEN 0x4000  // Enable extended input processing

// ============================================================================
// Baud Rate Constants
// ============================================================================
typedef uint32_t speed_t;
typedef uint32_t tcflag_t;
typedef uint8_t cc_t;

#define B0 0
#define B50 1
#define B75 2
#define B110 3
#define B134 4
#define B150 5
#define B200 6
#define B300 7
#define B600 8
#define B1200 9
#define B1800 10
#define B2400 11
#define B4800 12
#define B9600 13
#define B19200 14
#define B38400 15
#define B57600 16
#define B115200 17
#define B230400 18
#define B460800 19

// ============================================================================
// Optional Actions for tcsetattr
// ============================================================================
#define TCSANOW 0   // Make change immediately
#define TCSADRAIN 1 // Drain output before making change
#define TCSAFLUSH 2 // Drain output and discard input

// ============================================================================
// Queue Selectors for tcflush
// ============================================================================
#define TCIFLUSH 0  // Flush input queue
#define TCOFLUSH 1  // Flush output queue
#define TCIOFLUSH 2 // Flush both queues

// ============================================================================
// Actions for tcflow
// ============================================================================
#define TCOOFF 0 // Suspend output
#define TCOON 1  // Resume output
#define TCIOFF 2 // Transmit STOP character
#define TCION 3  // Transmit START character

// ============================================================================
// termios Structure
// ============================================================================
struct termios {
  tcflag_t c_iflag; // Input mode flags
  tcflag_t c_oflag; // Output mode flags
  tcflag_t c_cflag; // Control mode flags
  tcflag_t c_lflag; // Local mode flags
  cc_t c_line;      // Line discipline
  cc_t c_cc[NCCS];  // Control characters
  speed_t c_ispeed; // Input baud rate
  speed_t c_ospeed; // Output baud rate
};

// ============================================================================
// Terminal Control Functions
// ============================================================================
int tcgetattr(int fd, struct termios *termios_p);
int tcsetattr(int fd, int optional_actions, const struct termios *termios_p);
int tcsendbreak(int fd, int duration);
int tcdrain(int fd);
int tcflush(int fd, int queue_selector);
int tcflow(int fd, int action);

// ============================================================================
// Terminal Baud Rate Functions
// ============================================================================
speed_t cfgetispeed(const struct termios *termios_p);
speed_t cfgetospeed(const struct termios *termios_p);
int cfsetispeed(struct termios *termios_p, speed_t speed);
int cfsetospeed(struct termios *termios_p, speed_t speed);
int cfsetspeed(struct termios *termios_p, speed_t speed);

// ============================================================================
// Convenience Functions
// ============================================================================
void cfmakeraw(struct termios *termios_p);
int isatty(int fd);
char *ttyname(int fd);
int ttyname_r(int fd, char *buf, size_t buflen);

#ifdef __cplusplus
}
#endif

#endif // TERMIOS_H
