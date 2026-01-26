#!/bin/bash
set -e

echo "Building inside WSL..."

# Clean previous build
find src -name "*.o" -type f -delete
find apps -name "*.o" -type f -delete
rm -f os.img

# Compile Apps
echo "Compiling apps (C++)..."

# Function to build an app
build_app() {
    local name=$1
    echo "  Building apps/$name.cpp..."
    g++ -m32 -ffreestanding -fno-rtti -fno-exceptions -I apps/ -I apps/include -I src/include -c apps/$name.cpp -o apps/$name.o
    ld -m elf_i386 -T apps/linker.ld -o apps/$name.elf apps/$name.o apps/posix_impl.o apps/minimal_os_api.o apps/Contracts.o
}

# Core Libs for Apps
g++ -m32 -ffreestanding -fno-rtti -fno-exceptions -I apps/ -I apps/include -I src/include -c apps/posix_impl.cpp -o apps/posix_impl.o
g++ -m32 -ffreestanding -fno-rtti -fno-exceptions -I apps/ -I apps/include -I src/include -c apps/minimal_os_api.cpp -o apps/minimal_os_api.o
g++ -m32 -ffreestanding -fno-rtti -fno-exceptions -I apps/ -I apps/include -I src/include -c apps/Contracts.cpp -o apps/Contracts.o

# Build Applications
build_app "hello"
build_app "init"
# build_app "calc"
build_app "df"
build_app "textview"
build_app "file_utils"
build_app "sh"
build_app "terminal"
build_app "ls"
build_app "cat"
build_app "mkdir"
build_app "notepad"
build_app "test"
build_app "ping"
build_app "tcptest"
# ... (Keep existing)
build_app "ping"
build_app "tcptest"

# Browser Build
build_browser() {
    echo "  Building Browser..."
    
    # 1. Compile mbedtls (C)
    echo "    Compiling mbedtls..."
    find src/external/mbedtls/library -name "*.c" | while read -r file; do
        outfile="${file%.c}.o"
        # Using -D_POSIX_C_SOURCE for mbedtls if needed, or MBEDTLS_NO_PLATFORM_ENTROPY etc
        # forcing 32-bit
        gcc -ffreestanding -m32 -fno-pie -fstack-protector-strong -Os -g \
            -I src/include -I src/external/mbedtls/include \
            -c "$file" -o "$outfile"
    done

    # 2. Compile litehtml (C++)
    echo "    Compiling litehtml..."
    find src/external/litehtml/src -maxdepth 1 -name "*.cpp" | while read -r file; do
        outfile="${file%.cpp}.o"
        g++ -ffreestanding -m32 -fno-pie -fstack-protector-strong -Os -fno-rtti -fno-exceptions -std=c++20 -g \
            -I src/include -I src/external/litehtml/include \
            -c "$file" -o "$outfile"
    done
    # gumbo (C)
    find src/external/litehtml/src/gumbo -name "*.c" | while read -r file; do
        outfile="${file%.c}.o"
        gcc -ffreestanding -m32 -fno-pie -fstack-protector-strong -Os -g \
            -I src/include -I src/external/litehtml/include \
            -c "$file" -o "$outfile"
    done

    # 3. Compile Browser App (C++)
    echo "    Compiling Browser App..."
    find apps/browser -name "*.cpp" | while read -r file; do
        outfile="${file%.cpp}.o"
        g++ -ffreestanding -m32 -fno-pie -fstack-protector-strong -Os -fno-rtti -fno-exceptions -std=c++20 -g \
            -I apps/ -I apps/include -I src/include \
            -I apps/browser \
            -I src/external/mbedtls/include \
            -I src/external/litehtml/include \
            -c "$file" -o "$outfile"
    done

    # 4. Link
    echo "    Linking Browser..."
    MBED_OBJS=$(find src/external/mbedtls/library -name "*.o")
    LITE_OBJS=$(find src/external/litehtml/src -name "*.o")
    BROWSER_OBJS=$(find apps/browser -name "*.o")
    
    ld -m elf_i386 -T apps/linker.ld -o apps/browser.elf \
        $BROWSER_OBJS $MBED_OBJS $LITE_OBJS \
        apps/posix_impl.o apps/minimal_os_api.o apps/Contracts.o
}

build_browser
# build_app "explorer"

echo "  Building apps/posix_test.cpp..."
g++ -m32 -ffreestanding -fno-rtti -fno-exceptions -I apps/ -I apps/include -I src/include -c apps/posix_test.cpp -o apps/posix_test.o
ld -m elf_i386 -T apps/linker.ld -o apps/posix_test.elf apps/posix_test.o apps/posix_impl.o

echo "  Building apps/posix_suite.cpp..."
g++ -m32 -ffreestanding -fno-rtti -fno-exceptions -I apps/ -I apps/include -I src/include -c apps/posix_suite.cpp -o apps/posix_suite.o
ld -m elf_i386 -T apps/linker.ld -o apps/posix_suite.elf apps/posix_suite.o apps/posix_impl.o


# Compile Bootloader
echo "Compiling boot.asm..."
nasm src/boot/boot.asm -f bin -o src/boot/boot.bin -I src/boot/

# Compile Kernel Entry
echo "Compiling kernel_entry.asm..."
nasm src/boot/kernel_entry.asm -f elf32 -o src/boot/kernel_entry.o

echo "Compiling interrupt.asm..."
nasm src/kernel/interrupt.asm -f elf32 -o src/kernel/interrupt.o

echo "Compiling process_asm.asm..."
nasm src/kernel/process_asm.asm -f elf32 -o src/kernel/process_asm.o

echo "Compiling gdt_asm.asm..."
nasm src/kernel/gdt_asm.asm -f elf32 -o src/kernel/gdt_asm.o

# Compile C Sources
echo "Compiling Sources..."
find src -name "*.cpp" | while read -r file; do
    echo "  Compiling $file..."
    outfile="${file%.cpp}.o"
    g++ -ffreestanding -m32 -fno-pie -fstack-protector-strong -Os -fno-rtti -fno-exceptions -std=c++20 -g -I src/include -c "$file" -o "$outfile"
done

find src -name "*.c" | while read -r file; do
    echo "  Compiling $file..."
    outfile="${file%.c}.o"
    gcc -ffreestanding -m32 -fno-pie -fstack-protector-strong -Os -g -I src/include -c "$file" -o "$outfile"
done

# Link
echo "Linking..."
OBJ_FILES=$(find src -name "*.o" ! -name "kernel_entry.o")
ld -m elf_i386 -o src/kernel/kernel.bin -T linker.ld src/boot/kernel_entry.o $OBJ_FILES --oformat binary

# Create OS Image
echo "Creating os.img..."
cat src/boot/boot.bin src/kernel/kernel.bin > os.img
truncate -s 32M os.img

# Inject Files
echo "Injecting Files..."
python3 inject_wallpaper.py

echo "Build Successful: os.img"
