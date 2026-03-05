// include/tty.h - Proper Unix TTY Subsystem
// Replaces the old per-task tty_t with a shared TTY device model.

#ifndef TTY_H
#define TTY_H

#include "types.h"
#include "pipe.h"
#include "vga.h"

// ─── VGA / Buffer Constants ────────────────────────────────────────────────
#define TTY_COLS        80
#define TTY_ROWS        25
#define TTY_BUF_LINES   200          // Scrollback buffer depth
#define TTY_BUF_SIZE    (TTY_COLS * TTY_BUF_LINES)
#define TTY_COUNT       5            // /dev/tty0 .. /dev/tty3, plus /dev/ttyS0

// ─── ANSI Colour Definitions (VGA attribute bytes) ────────────────────────
#ifndef COLOR_BLACK
#define COLOR_BLACK         0x00
#define COLOR_BLUE          0x01
#define COLOR_GREEN         0x02
#define COLOR_CYAN          0x03
#define COLOR_RED           0x04
#define COLOR_MAGENTA       0x05
#define COLOR_BROWN         0x06
#define COLOR_LIGHT_GREY    0x07
#define COLOR_DARK_GREY     0x08
#define COLOR_LIGHT_BLUE    0x09
#define COLOR_LIGHT_GREEN   0x0A
#define COLOR_LIGHT_CYAN    0x0B
#define COLOR_LIGHT_RED     0x0C
#define COLOR_LIGHT_MAGENTA 0x0D
#define COLOR_YELLOW        0x0E
#define COLOR_WHITE         0x0F
#endif

#ifndef MAKE_COLOR
#define MAKE_COLOR(fg, bg)  (((bg) << 4) | (fg))
#endif

#ifndef COLOR_WHITE_ON_BLACK
#define COLOR_WHITE_ON_BLACK  MAKE_COLOR(COLOR_WHITE, COLOR_BLACK)
#define COLOR_GREEN_ON_BLACK  MAKE_COLOR(COLOR_GREEN, COLOR_BLACK)
#define COLOR_YELLOW_ON_BLACK MAKE_COLOR(COLOR_YELLOW, COLOR_BLACK)
#endif

// ─── termios ─────────────────────────────────────────────────────────────
// c_cc special characters
#define VINTR    0   // Ctrl+C  -> SIGINT
#define VQUIT    1   // Ctrl+\  -> SIGQUIT
#define VERASE   2   // Backspace
#define VKILL    3   // Ctrl+U  -> kill line
#define VEOF     4   // Ctrl+D  -> EOF
#define VSUSP    5   // Ctrl+Z  -> SIGTSTP
#define VMIN     6   // Raw mode: min chars before read returns
#define VTIME    7   // Raw mode: read timeout (0 = no timeout)
#define NCCS     8

// c_lflag bits
#define ICANON   0x0001   // Canonical (line-buffered) mode
#define ECHO     0x0002   // Echo input characters
#define ECHOE    0x0004   // Echo ERASE as BS-SPACE-BS
#define ISIG     0x0008   // Generate signals on INTR/QUIT/SUSP

// c_iflag bits
#define ICRNL    0x0001   // Map CR to NL on input

// c_oflag bits
#define ONLCR    0x0001   // Map NL to CR+NL on output

typedef struct {
    uint32_t c_iflag;    // Input modes
    uint32_t c_oflag;    // Output modes
    uint32_t c_cflag;    // Control modes
    uint32_t c_lflag;    // Local modes
    uint8_t  c_cc[NCCS]; // Special characters
} termios_t;

// ─── winsize ─────────────────────────────────────────────────────────────
typedef struct {
    uint16_t ws_row;    // Rows (characters)
    uint16_t ws_col;    // Columns (characters)
    uint16_t ws_xpixel; // Width in pixels (unused, 0)
    uint16_t ws_ypixel; // Height in pixels (unused, 0)
} winsize_t;

// ─── ioctl request numbers ───────────────────────────────────────────────
#define TCGETS       0x5401   // Get termios
#define TCSETS       0x5402   // Set termios immediately
#define TCSETSW      0x5403   // Set termios after drain
#define TIOCGWINSZ   0x5413   // Get window size
#define TIOCSWINSZ   0x5414   // Set window size
#define TIOCSCTTY    0x540E   // Set controlling TTY
#define TIOCGPGRP    0x540F   // Get foreground process group
#define TIOCSPGRP    0x5410   // Set foreground process group

// ─── Line Discipline Input Buffer ────────────────────────────────────────
#define TTY_LD_BUF_SIZE 256

// ─── TTY Device Structure ─────────────────────────────────────────────────
typedef struct tty_device {
    // -- VGA Framebuffer --
    uint16_t buffer[TTY_BUF_SIZE];  // Full scrollback buffer
    int cursor_x;
    int cursor_y;
    int scroll_offset;
    int content_end_y;

    // -- ANSI parser state --
    int ansi_state;
    int ansi_params[8];
    int ansi_num_params;
    uint8_t curr_color;

    // -- Line discipline --
    termios_t termios;
    winsize_t winsize;
    char ld_buf[TTY_LD_BUF_SIZE];  // Cooked-mode line buffer
    int  ld_len;                    // Current length of line in ld_buf

    // -- Input pipe (delivers completed lines or raw chars to readers) --
    pipe_t* input_pipe;

    // -- Session / job control --
    uint32_t session_id;            // Session leader SID
    uint32_t foreground_pgid;       // Foreground process group

    // -- Misc --
    int index;                      // Which ttyN this is (0..3, or 4 for ttyS0)
    int in_use;                     // 1 once a session is attached
    int is_serial;                  // 1 if this is a serial TTY (bypasses VGA)
} tty_device_t;

// ─── Global device table ─────────────────────────────────────────────────
extern tty_device_t tty_devices[TTY_COUNT];
extern tty_device_t* active_tty;   // Which TTY is currently rendered to VGA

// ─── Kernel API ──────────────────────────────────────────────────────────
void tty_init_all(void);
tty_device_t* tty_get(int n);           // Get device by index
void tty_switch(int n);                 // Switch VGA output to ttyN

// Line discipline — called by keyboard IRQ handler
void tty_ld_input(tty_device_t* tty, char c);

// TTY I/O — called by SYS_READ / SYS_WRITE via FD_TYPE_TTY
int  tty_dev_read(tty_device_t* tty, char* buf, int count);
int  tty_dev_write(tty_device_t* tty, const char* buf, int count);

// ioctl dispatch — called by SYS_IOCTL
int  tty_ioctl(tty_device_t* tty, uint32_t request, void* argp);

// VGA helpers
void tty_flush_vga(tty_device_t* tty);  // Copy buffer → VGA hardware
void tty_putchar(tty_device_t* tty, char c, uint8_t color);

// Signal helper — implemented in task.c, declared here for tty.c use
void task_send_signal_pgrp(uint32_t pgid, int sig);

#endif // TTY_H
