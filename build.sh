#!/bin/bash
set -e

echo "Building inside WSL..."

# Newlib Headers Path
NEWLIB_INC="src/libc/newlib/newlib/libc/include"
NETSURF_INC="-I apps/netsurf -I apps/netsurf/libparserutils -I apps/netsurf/libwapcaplet -I apps/netsurf/hubbub -I apps/netsurf/libcss -I apps/netsurf/libdom -I apps/netsurf/libnsfb -I apps/netsurf/libcurl -I apps/netsurf/image"
APP_INC="-I $NEWLIB_INC -I src/include $NETSURF_INC -D_LIBC_SKIP_STANDARD_FUNCS"
CORE_INC="-I src/include -I src/kernel $NETSURF_INC -D_LIBC_SKIP_STANDARD_FUNCS"

# Clean previous build
find src -name "*.o" -type f -delete
find apps -name "*.o" -type f -delete
find src/libc/posix -name "*.o" -delete || true
rm -f os.img

# 0. Compile Newlib Stubs first (needed by apps)
echo "Compiling Newlib Stubs..."
g++ -m32 -ffreestanding -fno-rtti -fno-exceptions -fno-pic -fno-pie $APP_INC -c src/libc/new_stubs.cpp -o src/libc/new_stubs.o

# 0b. Compile Pure Newlib POSIX implementations from source
echo "Compiling Pure Newlib POSIX logic..."
find src/libc/posix -name "*.c" ! -name "printf.c" ! -name "vfprintf.c" ! -name "puts.c" ! -name "fputs.c" ! -name "nano-vfprintf.c" | while read -r file; do
    echo "  Compiling Newlib Source: $file..."
    outfile="${file%.c}.o"
    gcc -m32 -ffreestanding -O2 -fno-stack-protector -fno-builtin -fno-pic -fno-pie -I src/libc/newlib/newlib/libc/include -I src/libc/newlib/newlib/libc/stdio -I src/libc/newlib/newlib/libc/stdlib -I src/libc/newlib/newlib/libc/string -I src/include -DINTERNAL_NEWLIB -D_REENT_ONLY -D_COMPILING_NEWLIB -D_SSP_NOT_USED -D_FORTIFY_SOURCE=0 -D"__inhibit_loop_to_libcall=" -DPREFER_SIZE_OVER_SPEED -DDEFINE_MALLOC -DDEFINE_FREE -DDEFINE_REALLOC -DDEFINE_CALLOC -DDEFINE_MALLOC_USABLE_SIZE -c "$file" -o "$outfile"
done
LIBC_POSIX_OBJS=$(find src/libc/posix -name "*.o")

# 1. Compile C Sources (Excluding libc which we build as stubs)
echo "Compiling Core Sources..."
find src -name "*.cpp" -not -path "*litehtml*" -not -path "*sb16_new*" -not -path "*dillo*" -not -path "*v8*" -not -path "*src/libc*" -not -path "*toaruos*" | while read -r file; do
    echo "  Compiling $file..."
    outfile="${file%.cpp}.o"
    g++ -ffreestanding -m32 -fno-pic -fno-pie -fstack-protector-strong -Os -fno-rtti -fno-exceptions -std=c++20 -g $CORE_INC -Wno-address-of-packed-member -c "$file" -o "$outfile"
done

find src -name "*.c" -not -path "*litehtml*" -not -path "*sb16_new*" -not -path "*dillo*" -not -path "*v8*" -not -path "*src/libc*" -not -path "*toaruos*" | while read -r file; do
    echo "  Compiling $file..."
    outfile="${file%.c}.o"
    gcc -ffreestanding -m32 -fno-pic -fno-pie -fstack-protector-strong -Os -g $CORE_INC -Wno-address-of-packed-member -c "$file" -o "$outfile"
done

# 2. Compile Assembly Sources
echo "Compiling Assembly sources (nasm)..."
nasm src/boot/boot.asm -f bin -o src/boot/boot.bin -I src/boot/
nasm src/kernel/ap_trampoline.asm -f bin -o src/kernel/ap_trampoline.bin
nasm src/kernel/ap_trampoline_data.asm -f elf32 -o src/kernel/ap_trampoline_data.o
nasm src/boot/kernel_entry.asm -f elf32 -o src/boot/kernel_entry.o
nasm src/kernel/interrupt.asm -f elf32 -o src/kernel/interrupt.o
nasm src/kernel/process_asm.asm -f elf32 -o src/kernel/process_asm.o
nasm src/kernel/gdt_asm.asm -f elf32 -o src/kernel/gdt_asm.o
nasm src/kernel/setjmp.asm -f elf32 -o src/kernel/setjmp.o
nasm src/kernel/ring2_gate.asm -f elf32 -o src/kernel/ring2_gate.o
nasm src/kernel/pae_asm.asm -f elf32 -o src/kernel/pae_asm.o

# 3. Compile App libraries
echo "Compiling App libraries..."
g++ -m32 -ffreestanding -fno-rtti -fno-exceptions -fno-pic -fno-pie -I apps/ -I apps/include $APP_INC -c apps/minimal_os_api.cpp -o apps/minimal_os_api.o
g++ -m32 -ffreestanding -fno-rtti -fno-exceptions -fno-pic -fno-pie -I apps/ -I apps/include $APP_INC -c apps/Contracts.cpp -o apps/Contracts.o
g++ -m32 -ffreestanding -O2 -fno-rtti -fno-exceptions -fno-pic -fno-pie -I apps/ -I apps/include $APP_INC -c apps/libc.cpp -o apps/libc.o
gcc -m32 -fno-pic -fno-pie -c apps/crt0.S -o apps/crt0.o

# 3b. Compile NetSurf Libraries
echo "Compiling NetSurf Libraries..."
find apps/netsurf -name "*.cpp" | while read -r file; do
    echo "  Compiling NetSurf: $file..."
    outfile="${file%.cpp}.o"
    g++ -ffreestanding -m32 -fno-pic -fno-pie -fstack-protector-strong -Os -fno-rtti -fno-exceptions -std=c++20 -g $APP_INC -c "$file" -o "$outfile"
done
NETSURF_OBJS=$(find apps/netsurf -name "*.o")

# 4. Build Applications
build_app() {
    echo "  Building apps/$1.elf..."
    g++ -m32 $APP_INC -ffreestanding -fno-rtti -fno-exceptions -fno-pic -fno-pie -fno-stack-protector -fno-unwind-tables -fno-asynchronous-unwind-tables -I apps/ -I apps/include -I edge264_port -D__APP__ -c "apps/$1.cpp" -o "apps/$1.o"
    ld -m elf_i386 -static -nostdlib --no-dynamic-linker -T apps/user.ld -o "apps/$1.elf" apps/crt0.o apps/libc.o "apps/$1.o" apps/minimal_os_api.o apps/Contracts.o src/libc/new_stubs.o $LIBC_POSIX_OBJS $NETSURF_OBJS
}

echo "Compiling applications..."
build_app "hello"
build_app "init"
build_app "df"
build_app "textview"
build_app "file_utils"
build_app "sh"
build_app "terminal"
build_app "ls"
build_app "cat"
build_app "mkdir"
build_app "test"
build_app "ping"
build_app "tcptest"
build_app "wavplay"
build_app "chaos"
build_app "audio_demo"
build_app "libc_demo"
build_app "netsurf_demo"
build_app "r3test"
build_app "brain"

# 5. Link Kernel
echo "Linking Kernel..."
OBJ_FILES=$(find src -name "*.o" ! -name "kernel_entry.o" ! -path "src/libc/*")
ld -m elf_i386 -o src/kernel/kernel.elf -T linker.ld src/boot/kernel_entry.o $OBJ_FILES
objcopy -O binary src/kernel/kernel.elf src/kernel/kernel.bin

# 5b. CHECK KERNEL SIZE — must fit before partition at sector 2048
KERNEL_SIZE=$(stat -c%s src/kernel/kernel.bin 2>/dev/null || stat -f%z src/kernel/kernel.bin)
KERNEL_SECTORS=$(( (KERNEL_SIZE + 511) / 512 ))
MAX_SECTORS=2047
echo "Kernel size: $KERNEL_SIZE bytes ($KERNEL_SECTORS sectors)"

if [ "$KERNEL_SECTORS" -gt "$MAX_SECTORS" ]; then
    echo "ERROR: Kernel too large! $KERNEL_SECTORS sectors exceeds $MAX_SECTORS limit"
    echo "ERROR: Kernel would overwrite FAT32 partition at sector 2048"
    exit 1
fi

# 6. Create OS Image
echo "Creating HDD.img (Port 0 - System)..."

# Check for real model files, download if missing
if [ ! -f "model.bin" ]; then
    echo "Downloading stories15M.bin..."
    curl -L https://huggingface.co/karpathy/tinyllamas/resolve/main/stories15M.bin -o model.bin
fi
if [ ! -f "tokenizer.bin" ]; then
    echo "Downloading tokenizer.bin..."
    curl -L https://github.com/karpathy/llama2.c/raw/master/tokenizer.bin -o tokenizer.bin
fi

# System Disk (64MB)
rm -f HDD.img
truncate -s 64M HDD.img
dd if=src/boot/boot.bin of=HDD.img conv=notrunc
dd if=src/kernel/kernel.bin of=HDD.img bs=512 seek=1 conv=notrunc
echo "Kernel written: $KERNEL_SECTORS sectors"

# Inject System Files into HDD.img
python3 inject_wallpaper.py HDD.img \
    WALL.BMP:assets/wallpaper.bmp \
    INIT.ELF:apps/init.elf \
    HELLO.ELF:apps/hello.elf \
    DF.ELF:apps/df.elf \
    SH.ELF:apps/sh.elf \
    TERMINAL.ELF:apps/terminal.elf \
    LS.ELF:apps/ls.elf \
    CAT.ELF:apps/cat.elf \
    MKDIR.ELF:apps/mkdir.elf \
    TEXTVIEW.ELF:apps/textview.elf \
    UTILS.ELF:apps/file_utils.elf \
    TEST.ELF:apps/test.elf \
    PING.ELF:apps/ping.elf \
    TCPTEST.ELF:apps/tcptest.elf \
    WAVPLAY.ELF:apps/wavplay.elf \
    CHAOS.ELF:apps/chaos.elf \
    AUDIO.ELF:apps/audio_demo.elf \
    BROWSER.ELF:apps/netsurf_demo.elf \
    R3TEST.ELF:apps/r3test.elf \
    BRAIN.ELF:apps/brain.elf

# AI Brain Disk (128MB)
echo "Creating brain_disk.img (Port 1 - AI)..."
rm -f brain_disk.img
truncate -s 128M brain_disk.img
python3 inject_wallpaper.py brain_disk.img \
    MODEL.BIN:model.bin \
    TOKEN.BIN:tokenizer.bin \
    TRUTH.DAT:TRUTH.DAT

echo "Build Successful: HDD.img and brain_disk.img"
cp HDD.img os.img
echo "Created: os.img"

# Verify boot sector integrity
BOOT_SIZE=$(stat -c%s src/boot/boot.bin 2>/dev/null || stat -f%z src/boot/boot.bin)
if [ "$BOOT_SIZE" -ne 512 ]; then
    echo "ERROR: Boot sector is $BOOT_SIZE bytes (must be exactly 512)!"
    exit 1
fi

# Verify boot signature (must be 0xAA55 at the end of sector 0)
# Note: xxd output format for -p is hex, 55aa (little endian order for 0xAA55)
SIGNATURE=$(xxd -s 510 -l 2 -p os.img)
if [ "$SIGNATURE" != "55aa" ]; then
    echo "ERROR: Boot signature missing or incorrect! Got: $SIGNATURE"
    exit 1
fi

echo "Boot sector verified: 512 bytes, signature 0xAA55"
echo "Kernel at LBA 1 (offset 512), $KERNEL_SECTORS sectors"
