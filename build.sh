#!/bin/bash
set -e

echo "Building inside WSL..."

# Newlib Headers Path
NEWLIB_INC="src/libc/newlib/newlib/libc/include"
NETSURF_INC="-I apps/netsurf -I apps/netsurf/libparserutils -I apps/netsurf/libwapcaplet -I apps/netsurf/hubbub -I apps/netsurf/libcss -I apps/netsurf/libdom -I apps/netsurf/libnsfb -I apps/netsurf/libcurl -I apps/netsurf/image"
APP_INC="-I $NEWLIB_INC -I src/include $NETSURF_INC -D_LIBC_SKIP_STANDARD_FUNCS"
CORE_INC="-I src/include $NETSURF_INC -D_LIBC_SKIP_STANDARD_FUNCS"

# Clean previous build
find src -name "*.o" -type f -delete
find apps -name "*.o" -type f -delete
find src/libc/posix -name "*.o" -delete || true
rm -f os.img

# 0. Compile Newlib Stubs first (needed by apps)
echo "Compiling Newlib Stubs..."
g++ -m32 -ffreestanding -fno-rtti -fno-exceptions $APP_INC -c src/libc/new_stubs.cpp -o src/libc/new_stubs.o

# 0b. Compile Pure Newlib POSIX implementations from source
echo "Compiling Pure Newlib POSIX logic..."
find src/libc/posix -name "*.c" ! -name "printf.c" ! -name "vfprintf.c" ! -name "puts.c" ! -name "fputs.c" ! -name "nano-vfprintf.c" | while read -r file; do
    echo "  Compiling Newlib Source: $file..."
    outfile="${file%.c}.o"
    gcc -m32 -ffreestanding -O2 -fno-stack-protector -fno-builtin -I src/libc/newlib/newlib/libc/include -I src/libc/newlib/newlib/libc/stdio -I src/libc/newlib/newlib/libc/stdlib -I src/libc/newlib/newlib/libc/string -I src/include -DINTERNAL_NEWLIB -D_REENT_ONLY -D_COMPILING_NEWLIB -D_SSP_NOT_USED -D_FORTIFY_SOURCE=0 -D"__inhibit_loop_to_libcall=" -DPREFER_SIZE_OVER_SPEED -DDEFINE_MALLOC -DDEFINE_FREE -DDEFINE_REALLOC -DDEFINE_CALLOC -DDEFINE_MALLOC_USABLE_SIZE -c "$file" -o "$outfile"
done
LIBC_POSIX_OBJS=$(find src/libc/posix -name "*.o")

# 1. Compile C Sources (Excluding libc which we build as stubs)
echo "Compiling Core Sources..."
find src -name "*.cpp" -not -path "*litehtml*" -not -path "*sb16_new*" -not -path "*dillo*" -not -path "*v8*" -not -path "*src/libc*" | while read -r file; do
    echo "  Compiling $file..."
    outfile="${file%.cpp}.o"
    g++ -ffreestanding -m32 -fno-pie -fno-pic -fno-stack-protector -Os -fno-rtti -fno-exceptions -std=c++20 -g $CORE_INC -Wno-address-of-packed-member -c "$file" -o "$outfile"
done

find src -name "*.c" -not -path "*litehtml*" -not -path "*sb16_new*" -not -path "*dillo*" -not -path "*v8*" -not -path "*src/libc*" | while read -r file; do
    echo "  Compiling $file..."
    outfile="${file%.c}.o"
    gcc -ffreestanding -m32 -fno-pie -fno-pic -fno-stack-protector -Os -g $CORE_INC -Wno-address-of-packed-member -c "$file" -o "$outfile"
done

# 2. Compile Assembly Sources
echo "Compiling Assembly sources (nasm)..."
nasm src/boot/boot.asm -f bin -o src/boot/boot.bin -I src/boot/
nasm src/boot/kernel_entry.asm -f elf32 -o src/boot/kernel_entry.o
nasm src/kernel/interrupt.asm -f elf32 -o src/kernel/interrupt.o
nasm src/kernel/process_asm.asm -f elf32 -o src/kernel/process_asm.o
nasm src/kernel/gdt_asm.asm -f elf32 -o src/kernel/gdt_asm.o
nasm src/kernel/setjmp.asm -f elf32 -o src/kernel/setjmp.o

# 3. Compile App libraries
echo "Compiling App libraries..."
g++ -m32 -ffreestanding -fno-rtti -fno-exceptions -I apps/ -I apps/include $APP_INC -c apps/minimal_os_api.cpp -o apps/minimal_os_api.o
g++ -m32 -ffreestanding -fno-rtti -fno-exceptions -I apps/ -I apps/include $APP_INC -c apps/Contracts.cpp -o apps/Contracts.o

# 3b. Compile NetSurf Libraries
echo "Compiling NetSurf Libraries..."
find apps/netsurf -name "*.cpp" | while read -r file; do
    echo "  Compiling NetSurf: $file..."
    outfile="${file%.cpp}.o"
    g++ -ffreestanding -m32 -fno-pie -fno-pic -fno-stack-protector -Os -fno-rtti -fno-exceptions -std=c++20 -g $APP_INC -c "$file" -o "$outfile"
done
NETSURF_OBJS=$(find apps/netsurf -name "*.o")

# 4. Build Applications
build_app() {
    echo "  Building apps/$1.elf..."
    g++ -m32 $APP_INC -ffreestanding -fno-rtti -fno-exceptions -fno-pie -fno-pic -fno-stack-protector -I apps/ -I apps/include -I edge264_port -D__APP__ -c "apps/$1.cpp" -o "apps/$1.o"
    ld -m elf_i386 -T apps/linker.ld -o "apps/$1.elf" "apps/$1.o" apps/minimal_os_api.o apps/Contracts.o src/libc/new_stubs.o $LIBC_POSIX_OBJS $NETSURF_OBJS
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
build_app "net_test"
build_app "netsurf_demo"

# 5. Link Kernel
echo "Linking Kernel with NetSurf libraries..."
NETSURF_OBJS=$(find apps/netsurf -name "*.o")
OBJ_FILES="$(find src -name "*.o" ! -name "kernel_entry.o" ! -path "src/libc/*") $NETSURF_OBJS"
ld -m elf_i386 -o src/kernel/kernel.bin -T linker.ld src/boot/kernel_entry.o $OBJ_FILES --oformat binary

# 6. Create OS Image
echo "Creating HDD.img..."
truncate -s 50G HDD.img
dd if=src/boot/boot.bin of=HDD.img conv=notrunc
dd if=src/kernel/kernel.bin of=HDD.img bs=512 seek=1 conv=notrunc
python3 inject_wallpaper.py

echo "Build Successful: HDD.img"
rm -f os.img
ln -sf HDD.img os.img
echo "Linked: os.img -> HDD.img"
