#ifndef UNISTD_H
#define UNISTD_H

#include <stddef.h>
#include <stdint.h>

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

// Misc
void     sync(void);
uint32_t get_ticks(void);

#endif
