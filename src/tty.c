// src/tty.c - Proper Unix TTY Device Driver with N_TTY Line Discipline
//
// Replaces the old per-task tty_t model.
// There are TTY_COUNT (4) virtual TTY devices, each with:
//   - A scrollback VGA framebuffer
//   - A termios line discipline (cooked / raw mode)
//   - A pipe for delivering input to readers
//   - A session / foreground process group for job control

#include "../include/tty.h"
#include "../include/string.h"
#include "../include/interrupt.h"
#include "../include/memory.h"
#include "../include/pipe.h"
#include "../include/vga.h"
#include "../include/serial.h"

// ─── Global Device Table ─────────────────────────────────────────────────
tty_device_t  tty_devices[TTY_COUNT];
tty_device_t* active_tty = 0;

// ─── ANSI colour mappings ─────────────────────────────────────────────────
static const uint8_t ansi_to_vga[]       = { 0, 4, 2, 6, 1, 5, 3, 7 };   // 30-37
static const uint8_t ansi_to_vga_bright[]= { 8,12,10,14, 9,13,11,15 };    // 90-97

// ─── Default termios ─────────────────────────────────────────────────────
static void tty_default_termios(termios_t* t) {
    t->c_iflag = ICRNL;
    t->c_oflag = ONLCR;
    t->c_cflag = 0;
    t->c_lflag = ICANON | ECHO | ECHOE | ISIG;
    t->c_cc[VINTR]  = 3;   // Ctrl+C
    t->c_cc[VQUIT]  = 28;  // Ctrl+backslash
    t->c_cc[VERASE] = 127; // DEL / Backspace
    t->c_cc[VKILL]  = 21;  // Ctrl+U
    t->c_cc[VEOF]   = 4;   // Ctrl+D
    t->c_cc[VSUSP]  = 26;  // Ctrl+Z
    t->c_cc[VMIN]   = 1;
    t->c_cc[VTIME]  = 0;
}

// ─── Initialise one TTY device ───────────────────────────────────────────
static void tty_device_init(tty_device_t* tty, int index) {
    memset(tty, 0, sizeof(tty_device_t));

    tty->index = index;
    tty->in_use = 0;
    tty->is_serial = (index >= 4); // Index >=4 is /dev/ttyS0 to /dev/ttyS3

    // Fill buffer with spaces using default colour
    uint16_t blank = ' ' | (COLOR_WHITE_ON_BLACK << 8);
    for (int i = 0; i < TTY_BUF_SIZE; i++) tty->buffer[i] = blank;

    tty->cursor_x     = 0;
    tty->cursor_y     = 0;
    tty->scroll_offset= 0;
    tty->content_end_y= 0;
    tty->curr_color   = COLOR_WHITE_ON_BLACK;
    tty->ansi_state   = 0;
    tty->ld_len       = 0;

    tty_default_termios(&tty->termios);
    tty->winsize.ws_row = TTY_ROWS;
    tty->winsize.ws_col = TTY_COLS;

    tty->foreground_pgid = 0;
    tty->session_id      = 0;

    tty->input_pipe = pipe_create();
}

// ─── Initialise all TTY devices ──────────────────────────────────────────
void tty_init_all(void) {
    for (int i = 0; i < TTY_COUNT; i++) {
        tty_device_init(&tty_devices[i], i);
    }
    active_tty = &tty_devices[0];
}

// ─── Get device by index ─────────────────────────────────────────────────
tty_device_t* tty_get(int n) {
    if (n < 0 || n >= TTY_COUNT) return 0;
    return &tty_devices[n];
}

// ─── VGA flush: copy buffer→hardware ─────────────────────────────────────
void tty_flush_vga(tty_device_t* tty) {
    if (!tty || tty->is_serial) return;
    uint16_t* vga = (uint16_t*)0xB8000;
    int start = tty->scroll_offset * TTY_COLS;
    for (int y = 0; y < TTY_ROWS; y++) {
        int src = start + y * TTY_COLS;
        if (src < TTY_BUF_SIZE) {
            for (int x = 0; x < TTY_COLS; x++)
                vga[y * TTY_COLS + x] = tty->buffer[src + x];
        } else {
            uint16_t blank = ' ' | (COLOR_WHITE_ON_BLACK << 8);
            for (int x = 0; x < TTY_COLS; x++)
                vga[y * TTY_COLS + x] = blank;
        }
    }
    // Update hardware cursor
    if (tty->cursor_y >= tty->scroll_offset &&
        tty->cursor_y <  tty->scroll_offset + TTY_ROWS) {
        uint16_t pos = (tty->cursor_y - tty->scroll_offset) * TTY_COLS + tty->cursor_x;
        outb(0x3D4, 0x0F); outb(0x3D5, (uint8_t)(pos & 0xFF));
        outb(0x3D4, 0x0E); outb(0x3D5, (uint8_t)(pos >> 8));
    }
}

// ─── Switch active VGA TTY ───────────────────────────────────────────────
void tty_switch(int n) {
    if (n < 0 || n >= TTY_COUNT) return;
    active_tty = &tty_devices[n];
    tty_flush_vga(active_tty);
}

// ─── Scroll when content exceeds buffer ──────────────────────────────────
static void tty_content_scroll(tty_device_t* tty) {
    if (tty->cursor_y >= TTY_BUF_LINES) {
        // Shift everything up one line
        memcpy(tty->buffer,
               tty->buffer + TTY_COLS,
               (TTY_BUF_LINES - 1) * TTY_COLS * sizeof(uint16_t));
        uint16_t blank = ' ' | (COLOR_WHITE_ON_BLACK << 8);
        for (int i = 0; i < TTY_COLS; i++)
            tty->buffer[(TTY_BUF_LINES - 1) * TTY_COLS + i] = blank;
        tty->cursor_y = TTY_BUF_LINES - 1;
        tty->content_end_y = TTY_BUF_LINES - 1;
    }
}

// ─── ANSI handler ─────────────────────────────────────────────────────────
static void tty_ansi_sgr(tty_device_t* tty) {
    for (int i = 0; i < tty->ansi_num_params; i++) {
        int p = tty->ansi_params[i];
        if (p == 0) {
            tty->curr_color = COLOR_WHITE_ON_BLACK;
        } else if (p >= 30 && p <= 37) {
            tty->curr_color = (tty->curr_color & 0xF0) | ansi_to_vga[p - 30];
        } else if (p >= 40 && p <= 47) {
            tty->curr_color = (tty->curr_color & 0x0F) | (ansi_to_vga[p - 40] << 4);
        } else if (p >= 90 && p <= 97) {
            tty->curr_color = (tty->curr_color & 0xF0) | ansi_to_vga_bright[p - 90];
        } else if (p >= 100 && p <= 107) {
            tty->curr_color = (tty->curr_color & 0x0F) | (ansi_to_vga_bright[p - 100] << 4);
        }
    }
}

// ─── putchar: write one character into TTY buffer ─────────────────────────
void tty_putchar(tty_device_t* tty, char c, uint8_t color) {
    if (!tty) return;

    if (tty->is_serial) {
        serial_putchar_port(tty->index - 4,c);
        return;
    }

    // Use TTY current colour if caller passes 0
    if (color == 0) color = tty->curr_color;

    // ── ANSI state machine ──────────────────────────────────────────────
    if (tty->ansi_state == 1) {
        if (c == '[') {
            tty->ansi_state = 2;
            tty->ansi_num_params = 0;
            for (int i = 0; i < 8; i++) tty->ansi_params[i] = 0;
        } else {
            tty->ansi_state = 0;
        }
        return;
    }
    if (tty->ansi_state == 2) {
        if (c >= '0' && c <= '9') {
            tty->ansi_params[tty->ansi_num_params] =
                tty->ansi_params[tty->ansi_num_params] * 10 + (c - '0');
            return;
        } else if (c == ';') {
            if (tty->ansi_num_params < 7) tty->ansi_num_params++;
            tty->ansi_params[tty->ansi_num_params] = 0;
            return;
        } else {
            tty->ansi_num_params++;
            tty->ansi_state = 0;

            // ── Dispatch ANSI command ──────────────────────────────────
            switch (c) {
                case 'm': tty_ansi_sgr(tty); return;
                case 'H': case 'f': {
                    // ESC[row;colH - move cursor (1-indexed, default 1,1)
                    int row = (tty->ansi_num_params >= 1 && tty->ansi_params[0] > 0)
                              ? tty->ansi_params[0] - 1 : 0;
                    int col = (tty->ansi_num_params >= 2 && tty->ansi_params[1] > 0)
                              ? tty->ansi_params[1] - 1 : 0;
                    tty->cursor_y = tty->scroll_offset + row;
                    tty->cursor_x = col;
                    return;
                }
                case 'A': { // ESC[nA - cursor up
                    int n = tty->ansi_params[0] > 0 ? tty->ansi_params[0] : 1;
                    tty->cursor_y -= n;
                    if (tty->cursor_y < 0) tty->cursor_y = 0;
                    return;
                }
                case 'B': { // ESC[nB - cursor down
                    int n = tty->ansi_params[0] > 0 ? tty->ansi_params[0] : 1;
                    tty->cursor_y += n;
                    return;
                }
                case 'C': { // ESC[nC - cursor right
                    int n = tty->ansi_params[0] > 0 ? tty->ansi_params[0] : 1;
                    tty->cursor_x += n;
                    if (tty->cursor_x >= TTY_COLS) tty->cursor_x = TTY_COLS - 1;
                    return;
                }
                case 'D': { // ESC[nD - cursor left
                    int n = tty->ansi_params[0] > 0 ? tty->ansi_params[0] : 1;
                    tty->cursor_x -= n;
                    if (tty->cursor_x < 0) tty->cursor_x = 0;
                    return;
                }
                case 'J': { // ESC[2J - clear screen
                    if (tty->ansi_params[0] == 2) {
                        uint16_t blank = ' ' | (COLOR_WHITE_ON_BLACK << 8);
                        for (int i = 0; i < TTY_BUF_SIZE; i++) tty->buffer[i] = blank;
                        tty->cursor_x = 0;
                        tty->cursor_y = 0;
                        tty->scroll_offset = 0;
                        tty->content_end_y = 0;
                    }
                    return;
                }
                case 'K': { // ESC[K - erase to end of line
                    int idx = tty->cursor_y * TTY_COLS + tty->cursor_x;
                    uint16_t blank = ' ' | (tty->curr_color << 8);
                    int end = tty->cursor_y * TTY_COLS + TTY_COLS;
                    for (int i = idx; i < end && i < TTY_BUF_SIZE; i++)
                        tty->buffer[i] = blank;
                    return;
                }
                case 's': { // ESC[s - save cursor
                    // Minimal: not stored (could add saved_x/y to struct later)
                    return;
                }
                case 'u': { // ESC[u - restore cursor (minimal)
                    return;
                }
                default:
                    return;
            }
        }
    }
    if (c == 27) { // ESC
        tty->ansi_state = 1;
        return;
    }

    // ── Normal character processing ─────────────────────────────────────
    switch (c) {
        case '\n':
            tty->cursor_x = 0;
            tty->cursor_y++;
            break;
        case '\r':
            tty->cursor_x = 0;
            break;
        case '\b':
            if (tty->cursor_x > 0) {
                tty->cursor_x--;
            } else if (tty->cursor_y > 0) {
                tty->cursor_y--;
                tty->cursor_x = TTY_COLS - 1;
            }
            tty->buffer[tty->cursor_y * TTY_COLS + tty->cursor_x] =
                ' ' | (color << 8);
            break;
        case '\t':
            tty->cursor_x = (tty->cursor_x + 8) & ~7;
            break;
        default:
            if (c < 32 && c != 27) {
                // Echo control character as ^X
                tty_putchar(tty, '^', color);
                tty_putchar(tty, c + '@', color);
                return;
            }
            if (tty->cursor_y * TTY_COLS + tty->cursor_x < TTY_BUF_SIZE) {
                tty->buffer[tty->cursor_y * TTY_COLS + tty->cursor_x] =
                    (uint8_t)c | (color << 8);
            }
            tty->cursor_x++;
            break;
    }

    // Line wrap
    if (tty->cursor_x >= TTY_COLS) { tty->cursor_x = 0; tty->cursor_y++; }
    // Buffer overflow scroll
    if (tty->cursor_y >= TTY_BUF_LINES) tty_content_scroll(tty);
    // Track content extent
    if (tty->cursor_y > tty->content_end_y) tty->content_end_y = tty->cursor_y;
    // Auto-scroll view to follow cursor
    int required = tty->cursor_y - (TTY_ROWS - 1);
    if (required < 0) required = 0;
    tty->scroll_offset = required;
}

// ─── tty_dev_write: SYS_WRITE path ───────────────────────────────────────
int tty_dev_write(tty_device_t* tty, const char* buf, int count) {
    if (!tty || !buf) return -1;
    for (int i = 0; i < count; i++) {
        char c = buf[i];
        // ONLCR: map NL → CR+NL
        if ((tty->termios.c_oflag & ONLCR) && c == '\n')
            tty_putchar(tty, '\r', 0);
        tty_putchar(tty, c, 0);
    }
    // Only flush to hardware if this is the active TTY
    if (tty == active_tty)
        tty_flush_vga(tty);
    return count;
}

// ─── tty_dev_read: SYS_READ path (blocks until data available) ───────────
int tty_dev_read(tty_device_t* tty, char* buf, int count) {
    if (!tty || !buf || !tty->input_pipe) return -1;
    // Blocking: keep halting until pipe has data
    int got = 0;
    while (got < count) {
        int r = pipe_read(tty->input_pipe, (uint8_t*)(buf + got), count - got);
        if (r > 0) {
            got += r;
            // In canonical mode, stop at newline
            if (tty->termios.c_lflag & ICANON) break;
            if (got >= count) break;
        } else {
            // No data yet — yield CPU until next interrupt
            __asm__ volatile("sti; hlt; cli");
        }
    }
    return got;
}

// ─── ioctl dispatch ───────────────────────────────────────────────────────
int tty_ioctl(tty_device_t* tty, uint32_t request, void* argp) {
    if (!tty) return -1;
    switch (request) {
        case TCGETS:
            if (!argp) return -1;
            memcpy(argp, &tty->termios, sizeof(termios_t));
            return 0;
        case TCSETS:
        case TCSETSW:
            if (!argp) return -1;
            memcpy(&tty->termios, argp, sizeof(termios_t));
            return 0;
        case TIOCGWINSZ:
            if (!argp) return -1;
            memcpy(argp, &tty->winsize, sizeof(winsize_t));
            return 0;
        case TIOCSWINSZ:
            if (!argp) return -1;
            memcpy(&tty->winsize, argp, sizeof(winsize_t));
            // TODO: send SIGWINCH to foreground_pgid once signals are delivered
            return 0;
        case TIOCSCTTY: {
            // Attach this TTY as the controlling terminal of the current session
            // (argp is ignored per POSIX when the process has no ctty yet)
            tty->in_use = 1;
            return 0;
        }
        case TIOCGPGRP:
            if (!argp) return -1;
            *(uint32_t*)argp = tty->foreground_pgid;
            return 0;
        case TIOCSPGRP:
            if (!argp) return -1;
            tty->foreground_pgid = *(uint32_t*)argp;
            return 0;
        default:
            return -1; // ENOTTY
    }
}

// ─── Line Discipline: called from keyboard_handler() ─────────────────────
void tty_ld_input(tty_device_t* tty, char c) {
    if (!tty) return;
    termios_t* t = &tty->termios;

    // Map CR → NL if ICRNL
    if ((t->c_iflag & ICRNL) && c == '\r') c = '\n';

    // ── Signal generation (ISIG) ──────────────────────────────────────
    if (t->c_lflag & ISIG) {
        if ((uint8_t)c == t->c_cc[VINTR]) {  // Ctrl+C → SIGINT
            if (tty->foreground_pgid)
                task_send_signal_pgrp(tty->foreground_pgid, 2); // SIGINT=2
            return;
        }
        if ((uint8_t)c == t->c_cc[VQUIT]) {  // Ctrl+\ → SIGQUIT
            if (tty->foreground_pgid)
                task_send_signal_pgrp(tty->foreground_pgid, 3); // SIGQUIT=3
            return;
        }
        if ((uint8_t)c == t->c_cc[VSUSP]) {  // Ctrl+Z → SIGTSTP
            if (tty->foreground_pgid)
                task_send_signal_pgrp(tty->foreground_pgid, 20); // SIGTSTP=20
            return;
        }
    }

    // ── Raw mode: deliver immediately ────────────────────────────────
    if (!(t->c_lflag & ICANON)) {
        if (tty->input_pipe)
            pipe_write(tty->input_pipe, (const uint8_t*)&c, 1);
        if (t->c_lflag & ECHO)
            tty_dev_write(tty, &c, 1);
        return;
    }

    // ── Canonical mode ───────────────────────────────────────────────
    if ((uint8_t)c == t->c_cc[VEOF]) {
        // Ctrl+D: flush line buffer (including empty = EOF signal)
        if (tty->ld_len > 0 && tty->input_pipe)
            pipe_write(tty->input_pipe, (const uint8_t*)tty->ld_buf, tty->ld_len);
        else if (tty->input_pipe) {
            // Empty read signals EOF — write a 0-byte "record"
            // (readers will get 0 bytes back from pipe_read, indicating EOF)
        }
        tty->ld_len = 0;
        return;
    }

    // Ctrl+U: kill line
    if ((uint8_t)c == t->c_cc[VKILL]) {
        if (t->c_lflag & ECHO) {
            // Erase all echoed characters backwards
            for (int i = 0; i < tty->ld_len; i++)
                tty_dev_write(tty, "\b \b", 3);
        }
        tty->ld_len = 0;
        return;
    }

    // Backspace / ERASE
    if ((uint8_t)c == t->c_cc[VERASE] || c == '\b') {
        if (tty->ld_len > 0) {
            tty->ld_len--;
            if (t->c_lflag & (ECHO | ECHOE))
                tty_dev_write(tty, "\b \b", 3);
        }
        return;
    }

    // Normal character — buffer it
    if (tty->ld_len < TTY_LD_BUF_SIZE - 1) {
        tty->ld_buf[tty->ld_len++] = c;
        if (t->c_lflag & ECHO)
            tty_dev_write(tty, &c, 1);
    }

    // Newline: deliver line to input pipe
    if (c == '\n') {
        if (tty->input_pipe)
            pipe_write(tty->input_pipe, (const uint8_t*)tty->ld_buf, tty->ld_len);
        tty->ld_len = 0;
    }
}
