#ifndef SYS_WAIT_H
#define SYS_WAIT_H

#include <stdint.h>

// Wait options
#define WNOHANG    1
#define WUNTRACED  2

// Status decoding macros
#define WIFEXITED(s)    (((s) & 0x7f) == 0)
#define WEXITSTATUS(s)  (((s) & 0xff00) >> 8)
#define WIFSIGNALED(s)  (((s) & 0x7f) > 0 && ((s) & 0x7f) < 0x7f)
#define WTERMSIG(s)     ((s) & 0x7f)
#define WIFSTOPPED(s)   (((s) & 0xff) == 0x7f)
#define WSTOPSIG(s)     (((s) & 0xff00) >> 8)

#endif
