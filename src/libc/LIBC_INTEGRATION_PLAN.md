# Newlib C Library Integration Guide for Retro-OS

We have successfully refined the **System Stubs** (`new_stubs.cpp`) to link Newlib's standard C functions with the Retro-OS kernel. This guide outlines how to use the ported LibC in your applications.

## 🛠️ Updated Stubs (machine/retro-os)

The stubs in `src/libc/new_stubs.cpp` now support:
- **Memory**: `_sbrk` (hooked to `SYS_SBRK`)
- **File I/O**: `_open`, `_close`, `_read`, `_write`, `_lseek`, `_fstat`
- **Process**: `_fork`, `_execve`, `_wait`, `_exit`, `_getpid`, `_kill`
- **Time**: `_gettimeofday`, `_times`
- **Errno**: Full support for thread-safe (currently global) `errno` via `__errno()`.

## 🚀 How to Build Newlib for Retro-OS

Since Newlib is a substantial library, it needs to be compiled once using a cross-compiler. Follow these steps in your build environment (WSL):

1. **Enter Newlib Directory**:
   ```bash
   cd src/libc/newlib
   ```

2. **Configure for i386-retro-os**:
   ```bash
   mkdir build && cd build
   ../configure --target=i686-elf --prefix=/opt/retro-os-toolchain --disable-newlib-supplied-syscalls
   ```
   *Note: `--disable-newlib-supplied-syscalls` ensures Newlib uses our `new_stubs.cpp` instead of its own generic ones.*

3. **Compile and Install**:
   ```bash
   make
   make install
   ```

## 🧪 Testing with `libc_demo.cpp`

We've created a demo app at `apps/libc_demo.cpp`. To compile it manually while we update the build script:

```bash
# Compile stubs
g++ -m32 -ffreestanding -c src/libc/new_stubs.cpp -o src/libc/new_stubs.o -I src/libc/newlib/newlib/libc/include

# Compile demo app
g++ -m32 -ffreestanding -c apps/libc_demo.cpp -o apps/libc_demo.o -I src/libc/newlib/newlib/libc/include

# Link (Assuming libc.a is built)
ld -m elf_i386 -T apps/linker.ld -o apps/libc_demo.elf apps/libc_demo.o src/libc/new_stubs.o /path/to/libc.a
```

## 🔄 Roadmap for Full Integration
1.  **Thread Safety**: Implement `__retent` support in `new_stubs.cpp` once the kernel supports pthreads.
2.  **Floating Point**: ensure `_write` and `_read` handle diverse buffers correctly for buffered `printf`.
3.  **Self-hosting**: Use Newlib to compile the kernel itself on Retro-OS!
