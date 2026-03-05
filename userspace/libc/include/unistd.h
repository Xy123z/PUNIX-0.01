#ifndef UNISTD_H
#define UNISTD_H

#include <stddef.h>
#include <stdint.h>

// ─── Open flags ──────────────────────────────────────────────────────────
#define O_RDONLY 0x00
#define O_WRONLY 0x01
#define O_RDWR   0x02
#define O_CREAT  0x04
#define O_TRUNC  0x08
#define O_APPEND 0x10

// File I/O
int      read(int fd, void* buf, size_t count);
int      write(int fd, const void* buf, size_t count);
int      open(const char* path, int flags, ...);
int      close(int fd);
int      unlink(const char* path);
int      dup2(int oldfd, int newfd);
int      pipe(int pipefd[2]);
int      stat(const char* path, void* buf);
int      chmod(const char* path, uint32_t mode);
struct termios;
int      tcgetattr(int fd, struct termios* termios_p);
int      tcsetattr(int fd, int optional_actions, const struct termios* termios_p);
int      ioctl(int fd, uint32_t request, void* argp);
int      sigaction(int sig, void* handler);

// Directory/File system
int      chdir(const char* path);
char*    getcwd(char* buf, size_t size);
int      mkdir(const char* path);
int      rmdir(const char* path);

// Process management
int      fork(void);
int      exec(const char* path, char** argv);
void     exit(int status);
int      wait(int pid, int* status);
int      kill(int pid);
unsigned int sleep(unsigned int seconds);
uint32_t getpid(void);
uint32_t getuid(void);
int      setuid(uint32_t uid);
uint32_t getgid(void);
int      setgid(uint32_t gid);
int      getpgrp(void);
int      setpgid(uint32_t pid, uint32_t pgid);
int      getsid(uint32_t pid);
int      setsid(void);

// Misc
void     sync(void);
uint32_t get_ticks(void);
int      authenticate(const char* password);

#endif
