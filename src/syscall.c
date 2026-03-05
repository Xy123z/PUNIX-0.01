// src/syscall.c - System Call Handler (Unix-conforming)
//
// Removed: SYS_MALLOC, SYS_FREE, SYS_PRINT, SYS_PUTCHAR,
//          SYS_PRINT_COLORED, SYS_CLEAR_SCREEN, SYS_DRAW_*
// Added:   SYS_TCGETATTR, SYS_TCSETATTR, SYS_IOCTL, SYS_SIGACTION,
//          SYS_GETPGRP, SYS_SETPGID, SYS_GETSID, SYS_SETSID
// FD_TYPE_TTY routes through tty_dev_read / tty_dev_write (tty_device_t*).

#include "../include/syscall.h"
#include "../include/types.h"
#include "../include/interrupt.h"
#include "../include/console.h"
#include "../include/fs.h"
#include "../include/string.h"
#include "../include/memory.h"
#include "../include/ata.h"
#include "../include/paging.h"
#include "../include/auth.h"
#include "../include/task.h"
#include "../include/loader.h"
#include "../include/pipe.h"
#include "../include/tty.h"

uint32_t kernel_esp_saved;

// ─── FD helpers ───────────────────────────────────────────────────────────
static int allocate_fd(uint32_t node_id, uint8_t flags) {
    if (!current_task) return -1;
    for (int i = 0; i < MAX_FDS; i++) {
        if (!current_task->fd_table[i].in_use) {
            current_task->fd_table[i].type    = FD_TYPE_FILE;
            current_task->fd_table[i].ptr     = 0;
            current_task->fd_table[i].node_id = node_id;
            current_task->fd_table[i].offset  = 0;
            current_task->fd_table[i].flags   = flags;
            current_task->fd_table[i].in_use  = 1;
            return i;
        }
    }
    return -1;
}

void syscall_free_fd(task_t* task, int fd) {
    if (!task || fd < 0 || fd >= MAX_FDS) return;
    if (!task->fd_table[fd].in_use) return;
    if (task->fd_table[fd].type == FD_TYPE_PIPE) {
        pipe_t* p = (pipe_t*)task->fd_table[fd].ptr;
        if ((task->fd_table[fd].flags & 3) == O_RDONLY) p->readers--;
        else if ((task->fd_table[fd].flags & 3) == O_WRONLY) p->writers--;
        if (p->readers == 0 && p->writers == 0) pipe_destroy(p);
    }
    // TTY FDs: no cleanup needed (TTY device is persistent)
    task->fd_table[fd].in_use = 0;
}

void syscall_close_all(task_t* task) {
    if (!task) return;
    for (int i = 0; i < MAX_FDS; i++) syscall_free_fd(task, i);
}

void syscall_set_cwd(uint32_t id) {
    if (current_task) current_task->cwd_id = id;
}

static int check_permission(fs_node_t* node, uint32_t mask) {
    if (!current_task || !node) return 0;
    return fs_check_permission(node, current_task->uid, current_task->gid, mask);
}

// ─── sys_exit helper ──────────────────────────────────────────────────────
__attribute__((noreturn))
static void sys_exit_impl(int status) {
    task_exit(status);
    while(1) __asm__ volatile("hlt");
}

// ─── syscall_init ─────────────────────────────────────────────────────────
void syscall_init(void) {
    task_init();
    if (current_task) current_task->cwd_id = fs_root_id;
}

// ─── Main handler ─────────────────────────────────────────────────────────
uint32_t syscall_handler(registers_t* regs) {
    uint32_t num = regs->eax;
    uint32_t ebx = regs->ebx;
    uint32_t ecx = regs->ecx;
    uint32_t edx = regs->edx;
    uint32_t esi = regs->esi;
    uint32_t ret = 0;

    switch (num) {

    // ── 0: read ──────────────────────────────────────────────────────
    case SYS_READ: {
        int fd = (int)ebx;
        char* buf = (char*)ecx;
        uint32_t cnt = edx;
        if (fd < 0 || fd >= MAX_FDS || !current_task->fd_table[fd].in_use) { ret=(uint32_t)-1; break; }
        file_descriptor_t* fde = &current_task->fd_table[fd];
        if (fde->type == FD_TYPE_TTY) {
            ret = (uint32_t)tty_dev_read((tty_device_t*)fde->ptr, buf, (int)cnt);
        } else if (fde->type == FD_TYPE_PIPE) {
            ret = (uint32_t)pipe_read((pipe_t*)fde->ptr, (uint8_t*)buf, cnt);
        } else {
            fs_node_t* node = fs_get_node(fde->node_id);
            if (!node) { ret=(uint32_t)-1; break; }
            ret = fs_read(node, fde->offset, cnt, (uint8_t*)buf);
            fde->offset += ret;
        }
        break;
    }

    // ── 1: write ─────────────────────────────────────────────────────
    case SYS_WRITE: {
        int fd = (int)ebx;
        const char* buf = (const char*)ecx;
        uint32_t cnt = edx;
        if (fd < 0 || fd >= MAX_FDS || !current_task->fd_table[fd].in_use) { ret=(uint32_t)-1; break; }
        file_descriptor_t* fde = &current_task->fd_table[fd];
        if (fde->type == FD_TYPE_TTY) {
            ret = (uint32_t)tty_dev_write((tty_device_t*)fde->ptr, buf, (int)cnt);
        } else if (fde->type == FD_TYPE_PIPE) {
            ret = (uint32_t)pipe_write((pipe_t*)fde->ptr, (uint8_t*)buf, cnt);
        } else {
            fs_node_t* node = fs_get_node(fde->node_id);
            if (!node) { ret=(uint32_t)-1; break; }
            ret = fs_write(node, fde->offset, cnt, (uint8_t*)buf);
            fde->offset += ret;
        }
        break;
    }

    // ── 2: open ──────────────────────────────────────────────────────
    case SYS_OPEN: {
        char* path  = (char*)ebx;
        uint8_t flags = (uint8_t)ecx;
        fs_node_t* node = fs_find_node(path, current_task->cwd_id);

        if (!node && (flags & O_CREAT)) {
            // Create the file
            char parent_path[MAX_PATH], name[64];
            char* sl = strrchr(path, '/');
            if (sl) {
                int len = sl - path;
                if (len == 0) { strcpy(parent_path, "/"); }
                else { strncpy(parent_path, path, len); parent_path[len] = '\0'; }
                strcpy(name, sl + 1);
            } else { strcpy(parent_path, "."); strcpy(name, path); }
            fs_node_t* parent = fs_find_node(parent_path, current_task->cwd_id);
            if (parent && parent->type == FS_TYPE_DIRECTORY) {
                fs_create_node(parent->id, name, FS_TYPE_FILE, current_task->uid, current_task->gid);
                node = fs_find_node(path, current_task->cwd_id);
            }
        }
        if (!node) { ret=(uint32_t)-1; break; }

        // Character device open (e.g. /dev/tty0)
        if (node->type == FS_TYPE_CHARDEV) {
            // major 4 = TTY; minor = tty index
            int minor = (int)(node->indirect_block & 0xFF); // store minor in indirect_block
            tty_device_t* tty = tty_get(minor);
            if (!tty) { ret=(uint32_t)-1; break; }
            int fd = -1;
            for (int i = 0; i < MAX_FDS; i++) {
                if (!current_task->fd_table[i].in_use) { fd = i; break; }
            }
            if (fd < 0) { ret=(uint32_t)-1; break; }
            current_task->fd_table[fd].in_use  = 1;
            current_task->fd_table[fd].type    = FD_TYPE_TTY;
            current_task->fd_table[fd].ptr     = tty;
            current_task->fd_table[fd].flags   = flags;
            current_task->fd_table[fd].node_id = node->id;
            ret = (uint32_t)fd;
            break;
        }

        uint32_t mask = (flags == O_RDONLY) ? 4 : (flags == O_WRONLY) ? 2 : 6;
        if (!check_permission(node, mask)) { ret=(uint32_t)-1; break; }
        if (flags & O_TRUNC) { node->size = 0; fs_update_node(node); }
        ret = (uint32_t)allocate_fd(node->id, flags);
        break;
    }

    // ── 3: close ─────────────────────────────────────────────────────
    case SYS_CLOSE:
        syscall_free_fd(current_task, (int)ebx);
        ret = 0;
        break;

    // ── 4: getdents ──────────────────────────────────────────────────
    case SYS_GETDENTS: {
        // Takes a path (non-standard but kept pending opendir refactor)
        char* path = (char*)ebx;
        struct dirent* dirents = (struct dirent*)ecx;
        int max = (int)edx;
        fs_node_t* dir = fs_find_node(path, current_task->cwd_id);
        if (!dir || dir->type != FS_TYPE_DIRECTORY) { ret=(uint32_t)-1; break; }
        int cnt = 0;
        fs_dirent_t entries[SECTOR_SIZE / sizeof(fs_dirent_t)];
        for (int i = 0; i < 12 && cnt < max; i++) {
            if (!dir->blocks[i]) continue;
            ata_read_sectors(dir->blocks[i], 1, entries);
            for (int j = 0; j < (int)(SECTOR_SIZE / sizeof(fs_dirent_t)) && cnt < max; j++) {
                if (!entries[j].inode_id) continue;
                dirents[cnt].d_ino = entries[j].inode_id;
                inode_t* child = fs_get_node(entries[j].inode_id);
                dirents[cnt].d_type = child ? child->type : 0;
                strcpy(dirents[cnt].d_name, entries[j].name);
                cnt++;
            }
        }
        ret = cnt;
        break;
    }

    // ── 5: chdir ─────────────────────────────────────────────────────
    case SYS_CHDIR: {
        fs_node_t* t = fs_find_node((char*)ebx, current_task->cwd_id);
        if (t && t->type == FS_TYPE_DIRECTORY && check_permission(t, 1)) {
            current_task->cwd_id = t->id; ret = 0;
        } else ret = (uint32_t)-1;
        break;
    }

    // ── 6: getcwd ────────────────────────────────────────────────────
    case SYS_GETCWD:
        if (!ebx) { ret=(uint32_t)-1; break; }
        fs_get_full_path(current_task->cwd_id, (char*)ebx);
        ret = 0; break;

    // ── 7: mkdir ─────────────────────────────────────────────────────
    case SYS_MKDIR: {
        char* path = (char*)ebx;
        char pp[MAX_PATH], name[64];
        char* sl = strrchr(path, '/');
        if (sl) {
            int l = sl - path;
            if (l == 0) strcpy(pp, "/"); else { strncpy(pp, path, l); pp[l]='\0'; }
            strcpy(name, sl+1);
        } else { strcpy(pp, "."); strcpy(name, path); }
        fs_node_t* par = fs_find_node(pp, current_task->cwd_id);
        if (par && par->type == FS_TYPE_DIRECTORY && check_permission(par, 2))
            ret = fs_create_node(par->id, name, FS_TYPE_DIRECTORY, current_task->uid, current_task->gid) ? 0 : (uint32_t)-1;
        else ret = (uint32_t)-1;
        break;
    }

    // ── 8: rmdir ─────────────────────────────────────────────────────
    case SYS_RMDIR: {
        fs_node_t* t = fs_find_node((char*)ebx, current_task->cwd_id);
        if (t && t->type == FS_TYPE_DIRECTORY) {
            fs_node_t* par = fs_get_node(t->parent_id);
            if (par && !check_permission(par, 2)) { ret=(uint32_t)-1; break; }
            ret = fs_delete_node(t->id) ? 0 : (uint32_t)-1;
        } else ret = (uint32_t)-1;
        break;
    }

    // ── 9: unlink ────────────────────────────────────────────────────
    case SYS_UNLINK: {
        fs_node_t* t = fs_find_node((char*)ebx, current_task->cwd_id);
        if (t && t->type == FS_TYPE_FILE) {
            fs_node_t* par = fs_get_node(t->parent_id);
            if (par && !check_permission(par, 2)) { ret=(uint32_t)-1; break; }
            ret = fs_delete_node(t->id) ? 0 : (uint32_t)-1;
        } else ret = (uint32_t)-1;
        break;
    }

    // ── 10: stat ─────────────────────────────────────────────────────
    case SYS_STAT: {
        fs_node_t* node = fs_find_node((char*)ebx, current_task->cwd_id);
        if (!node) { ret=(uint32_t)-1; break; }
        struct_stat_t* s = (struct_stat_t*)ecx;
        s->st_ino = node->id; s->st_mode = node->mode;
        s->st_uid = node->uid; s->st_gid = node->gid;
        s->st_size = node->size; s->st_atime = node->atime;
        s->st_mtime = node->mtime; s->st_ctime = node->ctime;
        s->st_type = node->type;
        ret = 0; break;
    }

    // ── 11: exit ─────────────────────────────────────────────────────
    case SYS_EXIT:
        sys_exit_impl((int)ebx);

    // ── 12: getpid ───────────────────────────────────────────────────
    case SYS_GETPID:
        ret = current_task->id; break;

    // ── 16: create_file ──────────────────────────────────────────────
    case SYS_CREATE_FILE: {
        char* path = (char*)ebx;
        char pp[128], name[64];
        char* sl = strrchr(path, '/');
        if (sl) {
            int l = sl - path;
            if (l == 0) strcpy(pp, "/"); else { strncpy(pp, path, l); pp[l]='\0'; }
            strcpy(name, sl+1);
        } else { strcpy(pp, "."); strcpy(name, path); }
        fs_node_t* par = fs_find_node(pp, current_task->cwd_id);
        if (par && par->type == FS_TYPE_DIRECTORY && check_permission(par, 2))
            ret = fs_create_node(par->id, name, FS_TYPE_FILE, current_task->uid, current_task->gid) ? 0 : (uint32_t)-1;
        else ret = (uint32_t)-1;
        break;
    }

    // ── 20: clear_screen ─────────────────────────────────────────────
    case 20: { // SYS_CLEAR_SCREEN
        if (active_tty) {
            extern int tty_dev_write(tty_device_t*, const char*, int);
            tty_dev_write(active_tty, "\033[2J", 4);
            ret = 0;
        } else ret = (uint32_t)-1;
        break;
    }

    // ── 21-23: disk/cache/sync ───────────────────────────────────────
    case SYS_GET_DISK_STATS:
        fs_get_disk_stats((uint32_t*)ebx, (uint32_t*)ecx, (uint32_t*)edx); ret=0; break;
    case SYS_GET_CACHE_STATS:
        fs_get_cache_stats((uint32_t*)ebx, (uint32_t*)ecx, (uint32_t*)edx); ret=0; break;
    case SYS_SYNC:
        fs_sync(); ret=0; break;

    // ── 24-25: chuser / chpass ───────────────────────────────────────
    case SYS_CHUSER:
        if (current_task->uid != 0) { ret=(uint32_t)-1; break; }
        auth_set_username((const char*)ebx); ret=0; break;
    case SYS_CHPASS:
        if (current_task->uid != 0) { ret=(uint32_t)-1; break; }
        auth_set_password((const char*)ebx); ret=0; break;

    // ── 26-27: getuid / setuid ───────────────────────────────────────
    case SYS_GETUID: ret = current_task->uid; break;
    case SYS_SETUID:
        if (current_task->uid != 0) { ret=(uint32_t)-1; break; }
        current_task->uid = ebx; ret=0; break;

    // ── 28: authenticate ─────────────────────────────────────────────
    case SYS_AUTHENTICATE: {
        extern char ROOT_PASSWORD[];
        // Debug: print what we are comparing
        console_print("AUTH: kernel checking input '");
        console_print((const char*)ebx);
        console_print("' against '");
        console_print(ROOT_PASSWORD);
        console_print("'\n");

        ret = (strcmp((const char*)ebx, ROOT_PASSWORD) == 0) ? 0 : (uint32_t)-1;
        if (ret == 0) current_task->uid = 0;
        break;
    }

    // ── 29-30: shutdown / restart ────────────────────────────────────
    case SYS_SHUTDOWN:
        __asm__ volatile("outw %0,%1"::"a"((uint16_t)0x2000),"Nd"((uint16_t)0x604));
        __asm__ volatile("outw %0,%1"::"a"((uint16_t)0x3400),"Nd"((uint16_t)0x4004));
        while(1) __asm__ volatile("cli;hlt");
        break;
    case SYS_RESTART: {
        uint8_t g = 0x02;
        while (g & 0x02) __asm__ volatile("inb $0x64,%0":"=a"(g));
        __asm__ volatile("outb %0,%1"::"a"((uint8_t)0xFE),"Nd"((uint16_t)0x64));
        while(1) __asm__ volatile("cli;hlt");
        break;
    }

    // ── 31: get_mem_stats ────────────────────────────────────────────
    case SYS_GET_MEM_STATS:
        pmm_get_stats((uint32_t*)ebx, (uint32_t*)ecx, (uint32_t*)edx); ret=0; break;

    // ── 32: exec ─────────────────────────────────────────────────────
    case SYS_EXEC: {
        char* path = (char*)ebx;
        char** argv = (char**)ecx;
        fs_node_t* node = fs_find_node(path, current_task->cwd_id);
        if (!node || !check_permission(node, 1)) { ret=(uint32_t)-1; break; }
        int argc = 0;
        if (argv) while (argv[argc]) argc++;
        task_t* t = load_user_program(current_task, path, argc, argv);
        if (t) { task_run(t); }
        ret = (uint32_t)-1;
        break;
    }

    // ── 33: fork ─────────────────────────────────────────────────────
    case SYS_FORK: {
        task_t* child = task_fork(regs);
        if (!child) { ret=(uint32_t)-1; break; }
        child->state = TASK_READY;
        ret = child->id;
        break;
    }

    // ── 34-38: procs / kill / sleep / ticks / kbhit ──────────────────
    case SYS_GET_PROCS:
        ret = (uint32_t)task_get_procs((proc_info_t*)ebx, (int)ecx); break;
    case SYS_KILL: {
        int pid_sig = (int)ebx;
        int pid = pid_sig >> 8;
        int sig = pid_sig & 0xFF;
        if (sig == 0) sig = SIGTERM; // Default
        
        if (pid > 0) {
            ret = (uint32_t)task_kill_with_sig(pid, sig);
        } else if (pid < 0) {
            task_send_signal_pgrp(-pid, sig);
            ret = 0;
        } else ret = (uint32_t)-1;
        break;
    }
    case SYS_SLEEP:
        task_sleep(ebx); ret=0; break;
    case SYS_GET_TICKS: {
        extern uint32_t timer_ticks;
        ret = timer_ticks; break;
    }
    case SYS_KBHIT:
        ret = (uint32_t)keyboard_has_data(); break;

    // ── 39: wait ─────────────────────────────────────────────────────
    case SYS_WAIT:
        ret = (uint32_t)task_wait(ebx, (int*)ecx); break;

    // ── 40: get_username ─────────────────────────────────────────────
    case SYS_GET_USERNAME: {
        extern char USERNAME[];
        strncpy((char*)ebx, USERNAME, (size_t)ecx - 1);
        ((char*)ebx)[ecx-1] = '\0'; ret=0; break;
    }

    // ── 41: chmod ────────────────────────────────────────────────────
    case SYS_CHMOD: {
        fs_node_t* node = fs_find_node((char*)ebx, current_task->cwd_id);
        if (!node) { ret=(uint32_t)-1; break; }
        if (current_task->uid != 0 && current_task->uid != node->uid) { ret=(uint32_t)-1; break; }
        node->mode = ecx; fs_update_node(node); ret=0; break;
    }

    // ── 42: dup2 ─────────────────────────────────────────────────────
    case SYS_DUP2: {
        int old=(int)ebx, nw=(int)ecx;
        if (old<0||old>=MAX_FDS||nw<0||nw>=MAX_FDS) { ret=(uint32_t)-1; break; }
        if (!current_task->fd_table[old].in_use) { ret=(uint32_t)-1; break; }
        if (old==nw) { ret=(uint32_t)nw; break; }
        if (current_task->fd_table[nw].in_use) syscall_free_fd(current_task, nw);
        current_task->fd_table[nw] = current_task->fd_table[old];
        if (current_task->fd_table[nw].type == FD_TYPE_PIPE) {
            pipe_t* p = (pipe_t*)current_task->fd_table[nw].ptr;
            if ((current_task->fd_table[nw].flags & 3) == O_RDONLY) p->readers++;
            else if ((current_task->fd_table[nw].flags & 3) == O_WRONLY) p->writers++;
        }
        ret = (uint32_t)nw; break;
    }

    // ── 43: pipe ─────────────────────────────────────────────────────
    case SYS_PIPE: {
        int* pipefd = (int*)ebx;
        pipe_t* p = pipe_create();
        if (!p) { ret=(uint32_t)-1; break; }
        int pr=-1, pw=-1;
        for (int i=0; i<MAX_FDS; i++) {
            if (!current_task->fd_table[i].in_use) {
                if (pr==-1) pr=i; else if (pw==-1) { pw=i; break; }
            }
        }
        if (pr==-1||pw==-1) { pipe_destroy(p); ret=(uint32_t)-1; break; }
        current_task->fd_table[pr] = (file_descriptor_t){FD_TYPE_PIPE, 0, p, 0, O_RDONLY, 1};
        current_task->fd_table[pw] = (file_descriptor_t){FD_TYPE_PIPE, 0, p, 0, O_WRONLY, 1};
        p->readers = p->writers = 1;
        pipefd[0]=pr; pipefd[1]=pw; ret=0; break;
    }

    // ── 44-45: getgid / setgid ───────────────────────────────────────
    case SYS_GETGID: ret = current_task->gid; break;
    case SYS_SETGID:
        if (current_task->uid != 0) { ret=(uint32_t)-1; break; }
        current_task->gid = ebx; ret=0; break;

    // ── 49: tcgetattr ────────────────────────────────────────────────
    case SYS_TCGETATTR: {
        int fd = (int)ebx;
        if (fd<0||fd>=MAX_FDS||!current_task->fd_table[fd].in_use) { ret=(uint32_t)-1; break; }
        if (current_task->fd_table[fd].type != FD_TYPE_TTY) { ret=(uint32_t)-1; break; }
        tty_device_t* tty = (tty_device_t*)current_task->fd_table[fd].ptr;
        ret = (uint32_t)tty_ioctl(tty, TCGETS, (void*)ecx);
        break;
    }

    // ── 50: tcsetattr ────────────────────────────────────────────────
    case SYS_TCSETATTR: {
        int fd = (int)ebx;
        if (fd<0||fd>=MAX_FDS||!current_task->fd_table[fd].in_use) { ret=(uint32_t)-1; break; }
        if (current_task->fd_table[fd].type != FD_TYPE_TTY) { ret=(uint32_t)-1; break; }
        tty_device_t* tty = (tty_device_t*)current_task->fd_table[fd].ptr;
        // ecx = optional_actions (TCSANOW/TCSADRAIN/TCSAFLUSH — we treat all as immediate)
        ret = (uint32_t)tty_ioctl(tty, TCSETS, (void*)edx);
        break;
    }

    // ── 51: ioctl ────────────────────────────────────────────────────
    case SYS_IOCTL: {
        int fd = (int)ebx;
        uint32_t request = ecx;
        void* argp = (void*)edx;
        if (fd<0||fd>=MAX_FDS||!current_task->fd_table[fd].in_use) { ret=(uint32_t)-1; break; }
        if (current_task->fd_table[fd].type != FD_TYPE_TTY) { ret=(uint32_t)-1; break; }
        tty_device_t* tty = (tty_device_t*)current_task->fd_table[fd].ptr;
        ret = (uint32_t)tty_ioctl(tty, request, argp);
        break;
    }

    // ── 52: sigaction ────────────────────────────────────────────────
    case SYS_SIGACTION: {
        int sig = (int)ebx;
        void* handler = (void*)ecx;
        if (sig <= 0 || sig >= NSIG || sig == SIGKILL || sig == SIGSTOP) { ret=(uint32_t)-1; break; }
        current_task->signal_handlers[sig] = handler;
        ret = 0; break;
    }

    // ── 54: getpgrp ──────────────────────────────────────────────────
    case SYS_GETPGRP: ret = current_task->pgid; break;

    // ── 55: setpgid ──────────────────────────────────────────────────
    case SYS_SETPGID:
        ret = (uint32_t)task_setpgid(ebx, ecx); break;

    // ── 56: getsid ───────────────────────────────────────────────────
    case SYS_GETSID: ret = current_task->sid; break;

    // ── 57: setsid ───────────────────────────────────────────────────
    case SYS_SETSID:
        ret = (uint32_t)task_setsid(); break;

    default:
        ret = (uint32_t)-1;
        break;
    }

    // Deliver any pending signals before returning to user space
    task_deliver_signals();
    return ret;
}

// ─── Assembly interrupt wrapper ───────────────────────────────────────────
__asm__(
    ".global syscall_interrupt_wrapper\n"
    "syscall_interrupt_wrapper:\n"
    "   cli\n"
    "   pusha\n"
    "   push %ds\n" "   push %es\n" "   push %fs\n" "   push %gs\n"
    "   mov $0x10, %ax\n"
    "   mov %ax, %ds\n" "   mov %ax, %es\n"
    "   mov %ax, %fs\n" "   mov %ax, %gs\n"
    "   push %esp\n"
    "   call syscall_handler\n"
    "   add $4, %esp\n"
    // Store return value in EAX slot of saved registers_t
    "   mov %eax, 44(%esp)\n"
    "   pop %gs\n" "   pop %fs\n" "   pop %es\n" "   pop %ds\n"
    "   popa\n"
    "   iret\n"
);
