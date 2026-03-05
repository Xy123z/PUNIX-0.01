#include "../include/unistd.h"
#include "../include/punix_def.h"

// External assembly wrappers from syscalls.asm
extern int sys_read(int fd, void* buf, size_t count);
extern int sys_write(int fd, const void* buf, size_t count);
extern int sys_open(const char* path, int flags);
extern int sys_close(int fd);
extern int sys_getdents(const char* path, void* buf, int count);
extern int sys_chdir(const char* path);
extern int sys_getcwd(char* buf, size_t size);
extern int sys_mkdir(const char* path);
extern int sys_rmdir(const char* path);
extern int sys_unlink(const char* path);
extern int sys_exit(int status);
extern int sys_fork(void);
extern int sys_exec(const char* path, char** argv);
extern int sys_wait(int pid, int* status);
extern int sys_kill(int pid);
extern void sys_sleep(uint32_t ticks);
extern uint32_t sys_getpid(void);
extern uint32_t sys_getuid(void);
extern int sys_setuid(uint32_t uid);
extern uint32_t sys_getgid(void);
extern int sys_setgid(uint32_t gid);
extern int sys_tcgetattr(int fd, void* termios_p);
extern int sys_tcsetattr(int fd, int optional_actions, const void* termios_p);
extern int sys_ioctl(int fd, uint32_t request, void* argp);
extern int sys_sigaction(int sig, void* handler);
extern int sys_getpgrp(void);
extern int sys_setpgid(uint32_t pid, uint32_t pgid);
extern int sys_getsid(uint32_t pid);
extern int sys_setsid(void);
extern int sys_authenticate(const char* password);
extern void sys_sync(void);
extern uint32_t sys_get_ticks(void);
extern int sys_dup2(int oldfd, int newfd);
extern int sys_pipe(int pipefd[2]);
extern int sys_stat(const char* path, void* buf);
extern int sys_chmod(const char* path, uint32_t mode);

int read(int fd, void* buf, size_t count) {
    return sys_read(fd, buf, count);
}

int write(int fd, const void* buf, size_t count) {
    return sys_write(fd, buf, count);
}

int open(const char* path, int flags, ...) {
    return sys_open(path, flags);
}

int close(int fd) {
    return sys_close(fd);
}

int unlink(const char* path) {
    return sys_unlink(path);
}

int dup2(int oldfd, int newfd) {
    return sys_dup2(oldfd, newfd);
}

int pipe(int pipefd[2]) {
    return sys_pipe(pipefd);
}

int stat(const char* path, void* buf) {
    return sys_stat(path, buf);
}

int chmod(const char* path, uint32_t mode) {
    return sys_chmod(path, mode);
}

int chdir(const char* path) {
    return sys_chdir(path);
}

char* getcwd(char* buf, size_t size) {
    if (sys_getcwd(buf, size) == 0) return buf;
    return NULL;
}

int mkdir(const char* path) {
    return sys_mkdir(path);
}

int rmdir(const char* path) {
    return sys_rmdir(path);
}

int fork(void) {
    return sys_fork();
}

int exec(const char* path, char** argv) {
    return sys_exec(path, argv);
}

int wait(int pid, int* status) {
    return sys_wait(pid, status);
}

int kill(int pid) {
    return sys_kill(pid);
}

unsigned int sleep(unsigned int seconds) {
    // Assuming 100 ticks per second for now, or just pass as is if ticks
    sys_sleep(seconds * 100); 
    return 0;
}

uint32_t getpid(void) {
    return sys_getpid();
}

uint32_t getuid(void) {
    return sys_getuid();
}

int setuid(uint32_t uid) {
    return sys_setuid(uid);
}

uint32_t getgid(void) {
    return sys_getgid();
}

int setgid(uint32_t gid) {
    return sys_setgid(gid);
}

void sync(void) {
    sys_sync();
}

struct termios;
int tcgetattr(int fd, struct termios* termios_p) {
    return sys_tcgetattr(fd, (void*)termios_p);
}

int tcsetattr(int fd, int optional_actions, const struct termios* termios_p) {
    return sys_tcsetattr(fd, optional_actions, (void*)termios_p);
}

int ioctl(int fd, uint32_t request, void* argp) {
    return sys_ioctl(fd, request, argp);
}

int sigaction(int sig, void* handler) {
    return sys_sigaction(sig, handler);
}

int getpgrp(void) {
    return sys_getpgrp();
}

int setpgid(uint32_t pid, uint32_t pgid) {
    return sys_setpgid(pid, pgid);
}

int getsid(uint32_t pid) {
    return sys_getsid(pid);
}

int setsid(void) {
    return sys_setsid();
}

int authenticate(const char* password) {
    return sys_authenticate(password);
}

uint32_t get_ticks(void) {
    return sys_get_ticks();
}
