#!/bin/bash
echo "======================================"
echo "Modular Kernel Build Script"
echo "======================================"
echo ""

# Clean
echo "[1/16] Cleaning..."
rm -f *.o *.bin os.bin disk.img mkfs_host
echo "    Done!"
echo ""

# Compiler flags
# -nostdinc -fno-builtin ensures we don't accidentally use host headers or builtins
CFLAGS="-m32 -Iinclude -ffreestanding -nostdlib -nostdinc -fno-builtin -fno-pie -fno-pic -fno-stack-protector -O2"
USER_CFLAGS="-m32 -Iuserspace/libc/include -ffreestanding -nostdlib -nostdinc -fno-builtin -fno-pie -fno-pic -fno-stack-protector -O2"

# Compile each module
echo "[2/16] Compiling string.c..."
gcc $CFLAGS -c src/string.c -o string.o
if [ $? -ne 0 ]; then echo "Error!"; exit 1; fi

echo "[3/16] Compiling vga.c..."
gcc $CFLAGS -c src/vga.c -o vga.o
if [ $? -ne 0 ]; then echo "Error!"; exit 1; fi

echo "[4/16] Compiling memory.c..."
gcc $CFLAGS -c src/memory.c -o memory.o
if [ $? -ne 0 ]; then echo "Error!"; exit 1; fi

echo "[5/16] Compiling interrupt.c..."
gcc $CFLAGS -c src/interrupt.c -o interrupt.o
if [ $? -ne 0 ]; then echo "Error!"; exit 1; fi

echo "[7/16] Compiling fs.c..."
gcc $CFLAGS -c src/fs.c -o fs.o
if [ $? -ne 0 ]; then echo "Error!"; exit 1; fi

echo "[9/16] Compiling gdt.c..."
gcc $CFLAGS -c src/gdt.c -o gdt.o
if [ $? -ne 0 ]; then echo "Error!"; exit 1; fi

echo "[10/16] Compiling task.c..."
gcc $CFLAGS -c src/task.c -o task.o
if [ $? -ne 0 ]; then echo "Error!"; exit 1; fi

echo "[11/16] Assembling GDT flush, User Entry, Task Switch & Boot Entry..."
nasm -f elf32 src/gdt_flush.asm -o gdt_flush.o
nasm -f elf32 src/user_entry.asm -o user_entry.o
nasm -f elf32 src/task_asm.asm -o task_asm.o
nasm -f elf32 src/boot_entry.asm -o boot_entry.o


echo "[12b/16] Compiling loader.c..."
gcc $CFLAGS -c src/loader.c -o loader.o
if [ $? -ne 0 ]; then echo "Error!"; exit 1; fi

echo "[12/16] Compiling console.c..."
gcc $CFLAGS -c src/console.c -o console.o
if [ $? -ne 0 ]; then echo "Error!"; exit 1; fi

echo "[13/16] Compiling mouse.c..."
gcc $CFLAGS -c src/mouse.c -o mouse.o
if [ $? -ne 0 ]; then echo "Error!"; exit 1; fi

echo "[14/16] Compiling ata.c..."
gcc $CFLAGS -c src/ata.c -o ata.o
if [ $? -ne 0 ]; then echo "Error!"; exit 1; fi

echo "[15/16] Compiling math.c..."
gcc $CFLAGS -c src/math.c -o math.o
if [ $? -ne 0 ]; then echo "Error!"; exit 1; fi

echo "[16/16] Compiling auth.c..."
gcc $CFLAGS -c src/auth.c -o auth.o
if [ $? -ne 0 ]; then echo "Error!"; exit 1; fi

echo "[16/16] Compiling tty.c..."
gcc $CFLAGS -c src/tty.c -o tty.o
if [ $? -ne 0 ]; then echo "Error!"; exit 1; fi

echo "[16b/16] Compiling pipe.c..."
gcc $CFLAGS -c src/pipe.c -o pipe.o
if [ $? -ne 0 ]; then echo "Error!"; exit 1; fi

echo "[17/16] Compiling paging.c..."
gcc $CFLAGS -c src/paging.c -o paging.o
if [ $? -ne 0 ]; then echo "Error!"; exit 1; fi

echo "[18/16] Compiling syscall.c..."
gcc $CFLAGS -c src/syscall.c -o syscall.o
if [ $? -ne 0 ]; then echo "Error!"; exit 1; fi

echo "[19/16] Compiling kernel.c..."
gcc $CFLAGS -c kernel.c -o kernel.o
if [ $? -ne 0 ]; then echo "Error!"; exit 1; fi

echo "[20/16] Compiling kernel_shell.c..."
gcc $CFLAGS -c src/kernel_shell.c -o kernel_shell.o
if [ $? -ne 0 ]; then echo "Error!"; exit 1; fi

echo "[20b/16] Compiling serial.c..."
gcc $CFLAGS -c src/serial.c -o serial.o
if [ $? -ne 0 ]; then echo "Error!"; exit 1; fi

echo "[21/16] Linking kernel..."
ld -m elf_i386 -Ttext 0x10000 --oformat binary \
   boot_entry.o kernel.o string.o vga.o memory.o paging.o interrupt.o fs.o console.o mouse.o ata.o math.o tty.o auth.o syscall.o gdt.o gdt_flush.o task.o task_asm.o user_entry.o loader.o kernel_shell.o pipe.o serial.o\
   -o kernel.bin -nostdlib -e _start
if [ $? -ne 0 ]; then
    echo "Error: Linking failed!"
    exit 1
fi

# Calculate actual kernel size in sectors (round up)
KERNEL_SIZE=$(stat -f%z kernel.bin 2>/dev/null || stat -c%s kernel.bin)
KERNEL_SECTORS=$(( ($KERNEL_SIZE + 511) / 512 ))

echo ""
echo "Kernel size: $KERNEL_SIZE bytes ($KERNEL_SECTORS sectors)"
echo ""

# Check if kernel is too large
if [ $KERNEL_SECTORS -gt 250 ]; then
    echo "WARNING: Kernel is very large ($KERNEL_SECTORS sectors)"
    echo "Consider increasing the filesystem start sector in fs.h"
fi

echo "[21/16] Assembling bootloader..."
nasm -f bin boot.asm -o boot.bin
if [ $? -ne 0 ]; then echo "Error!"; exit 1; fi

# Compile hello_user.c -> hello2.bin
gcc $USER_CFLAGS -c userspace/hello_user.c -o hello_user.o
if [ $? -ne 0 ]; then echo "Error compiling hello_user.c"; exit 1; fi
ld -m elf_i386 -T userspace/user.ld -o hello2.bin hello_user.o
if [ $? -ne 0 ]; then echo "Error linking hello2.bin"; exit 1; fi

echo "[21b/16] Compiling userspace libc..."
nasm -f elf32 userspace/libc/src/crt0.asm -o crt0.o
nasm -f elf32 userspace/libc/src/syscalls.asm -o syscalls_user.o
gcc $USER_CFLAGS -c userspace/libc/src/string.c -o string_user.o
gcc $USER_CFLAGS -c userspace/libc/src/stdio.c -o stdio_user.o
gcc $USER_CFLAGS -c userspace/libc/src/stdlib.c -o stdlib_user.o
gcc $USER_CFLAGS -c userspace/libc/src/unistd.c -o unistd_user.o

LIBC_OBJS="crt0.o syscalls_user.o string_user.o stdio_user.o stdlib_user.o unistd_user.o"

echo "[21c/16] Compiling shell.prog and text.prog..."
# Compile objects for userspace
gcc $USER_CFLAGS -c src/shell.c -o shell_user.o
if [ $? -ne 0 ]; then echo "Error compiling shell_user.o"; exit 1; fi
gcc $USER_CFLAGS -c src/text.c -o text_user.o
if [ $? -ne 0 ]; then echo "Error compiling text_user.o"; exit 1; fi
gcc $USER_CFLAGS -c src/text_main.c -o text_main_user.o
if [ $? -ne 0 ]; then echo "Error compiling text_main_user.o"; exit 1; fi
gcc $USER_CFLAGS -c userspace/clock.c -o clock_user.o
if [ $? -ne 0 ]; then echo "Error compiling clock_user.o"; exit 1; fi
# New utilities
for util in ls mkdir rmdir cat ps kill mem pbash clear pwd echo chmod sudo snake loadbar init zombie_test getty login; do
    gcc $USER_CFLAGS -c userspace/$util.c -o ${util}_user.o
    if [ $? -ne 0 ]; then echo "Error compiling ${util}_user.o"; exit 1; fi
done

# Link shell and text as independent programs
ld -m elf_i386 -T userspace/user.ld -o shell.prog $LIBC_OBJS shell_user.o
if [ $? -ne 0 ]; then echo "Error linking shell.prog"; exit 1; fi
ld -m elf_i386 -T userspace/user.ld -o text.prog $LIBC_OBJS text_main_user.o text_user.o
if [ $? -ne 0 ]; then echo "Error linking text.prog"; exit 1; fi
ld -m elf_i386 -T userspace/user.ld -o clock.prog $LIBC_OBJS clock_user.o
if [ $? -ne 0 ]; then echo "Error linking clock.prog"; exit 1; fi

for util in ls mkdir rmdir cat ps kill mem pbash clear pwd echo chmod sudo snake loadbar init zombie_test getty login; do
    ld -m elf_i386 -T userspace/user.ld -o $util.prog $LIBC_OBJS ${util}_user.o
    if [ $? -ne 0 ]; then echo "Error linking $util.prog"; exit 1; fi
done
ld -m elf_i386 -T userspace/user.ld -o pbash.prog $LIBC_OBJS pbash_user.o
if [ $? -ne 0 ]; then echo "Error linking pbash.prog"; exit 1; fi

# Compile and link test_vga
gcc $USER_CFLAGS -c userspace/test_vga.c -o test_vga.o
if [ $? -ne 0 ]; then echo "Error compiling test_vga.c"; exit 1; fi
ld -m elf_i386 -T userspace/user.ld -o test_vga.prog $LIBC_OBJS test_vga.o
if [ $? -ne 0 ]; then echo "Error linking test_vga.prog"; exit 1; fi

echo "[22/16] Creating OS image..."

# Create a 50MB disk image
dd if=/dev/zero of=disk.img bs=1M count=50 status=none
if [ $? -ne 0 ]; then echo "Error creating disk image!"; exit 1; fi

# Write the bootloader to LBA Sector 0 (CHS Sector 1)
dd if=boot.bin of=disk.img seek=0 count=1 bs=512 conv=notrunc status=none
if [ $? -ne 0 ]; then echo "Error writing bootloader!"; exit 1; fi

# Write the kernel starting at LBA Sector 1 (CHS Sector 2)
dd if=kernel.bin of=disk.img seek=1 bs=512 conv=notrunc status=none
if [ $? -ne 0 ]; then echo "Error writing kernel!"; exit 1; fi

echo ""
echo "======================================"
echo "Kernel Build Complete!"
echo "Kernel: $KERNEL_SIZE bytes ($KERNEL_SECTORS sectors)"
echo "Bootloader: 512 bytes (1 sector)"
echo "Filesystem starts at sector: $((KERNEL_SECTORS + 1))"
echo "======================================"
echo ""

# --- NEW: Host-side Filesystem Creation ---
echo "[23/16] Building host-side filesystem creator..."
gcc -o mkfs_host mkfs_host.c
if [ $? -ne 0 ]; then
    echo "Warning: Failed to build mkfs_host"
    echo "Filesystem will be created at boot time instead"
else
    echo "[24/16] Creating filesystem on disk image..."
    echo ""
    echo "Available files for copying:"
    ls -lh boot.bin kernel.bin 2>/dev/null || echo "  Warning: Some files may be missing"
    echo ""

    ./mkfs_host disk.img
    if [ $? -ne 0 ]; then
        echo "Warning: Host filesystem creation failed"
        echo "Filesystem will be created at boot time instead"
    else
        echo ""
        echo "======================================"
        echo "Filesystem created successfully!"
        echo "Verifying /boot contents..."
        echo "======================================"
    fi
fi
echo ""

# Launch QEMU
#echo "Skipping QEMU launch for headless build."
#qemu-system-i386 -accel kvm -accel tcg,thread=single -drive file=disk.img,format=raw,index=0,media=disk -boot c -display none -serial stdio
#qemu-system-i386 \
 # -accel kvm \
 # -drive file=disk.img,format=raw,media=disk \
 # -boot c \
 # -display gtk \
 # -serial mon:stdio
qemu-system-i386 \
  -accel kvm \
  -drive file=disk.img,format=raw,media=disk \
  -boot c \
  -display gtk \
   -serial mon:stdio \
  -serial pty \
  -serial pty \
  -serial pty \

