#include "../include/pipe.h"
#include "../include/memory.h"
#include "../include/string.h"
#include "../include/task.h"

pipe_t* pipe_create() {
    pipe_t* pipe = (pipe_t*)kmalloc(sizeof(pipe_t));
    if (!pipe) return 0;
    
    memset(pipe, 0, sizeof(pipe_t));
    pipe->readers = 1;
    pipe->writers = 1;
    return pipe;
}

void pipe_destroy(pipe_t* pipe) {
    if (pipe) kfree(pipe);
}

int pipe_read(pipe_t* pipe, uint8_t* buf, uint32_t count) {
    if (!pipe) return -1;
    
    // If pipe is empty and no writers, return EOF (0)
    // In a real kernel we'd sleep here if empty but with writers
    // For now, let's do a non-blocking-ish or simple poll
    
    uint32_t read_bytes = 0;
    while (read_bytes < count) {
        if (pipe->size == 0) {
            if (pipe->writers == 0) break; // No more data possible
            
            // In a better implementation, we'd sleep here.
            // For now, return what we have if any, or 0.
            if (read_bytes > 0) break;
            
            // Yield if empty?
            // CRITICAL: We MUST re-enable interrupts and halt here,
            // because Punix currently disables interrupts during syscalls.
            // Without this, keyboard IRQs never fire while we wait!
            __asm__ volatile("sti; hlt; cli");
            schedule();
            if (pipe->size == 0 && pipe->writers == 0) break;
            continue; 
        }
        
        buf[read_bytes] = pipe->buffer[pipe->tail];
        pipe->tail = (pipe->tail + 1) % PIPE_BUF_SIZE;
        pipe->size--;
        read_bytes++;
    }
    
    return read_bytes;
}

int pipe_write(pipe_t* pipe, const uint8_t* buf, uint32_t count) {
    if (!pipe) return -1;
    if (pipe->readers == 0) {
        // console_print("[PIPE] Broken pipe: No readers\n");
        return -1; // Broken pipe
    }
    
    uint32_t written_bytes = 0;
    while (written_bytes < count) {
        if (pipe->size == PIPE_BUF_SIZE) {
            // Pipe full. In a real kernel, we'd sleep.
            // For now, yield and try again.
            // CRITICAL: Handle interrupt disablement during syscalls.
            __asm__ volatile("sti; hlt; cli");
            schedule();
            if (pipe->readers == 0) return -1;
            continue;
        }
        
        pipe->buffer[pipe->head] = buf[written_bytes];
        pipe->head = (pipe->head + 1) % PIPE_BUF_SIZE;
        pipe->size++;
        written_bytes++;
    }
    
    return written_bytes;
}
