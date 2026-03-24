# PROJECT RETRO-OS: A STRATEGIC PROPOSAL FOR NATIONAL DIGITAL SOVEREIGNTY

---

## VOLUME 6: TECHNICAL APPENDIX A - SYSTEM CALLS AND CORE API REFERENCE

### 6.1. The Interface Between User and Kernel
The System Call (Syscall) interface in Retro-OS is designed for extreme efficiency. Unlike the bloated POSIX implementations in Linux, Retro-OS uses a streamlined `int 0x80` or `sysenter` mechanism to transition from Ring 3 (User Space) to Ring 0 (Kernel Space).

### 6.2. Core File System Syscalls
These calls form the backbone of the **Virtual File System (VFS)** and allow for uniform access to hardware and files.

*   **`sys_open(const char* path, int flags)`**:
    *   **Description**: Opens a file or device.
    *   **Internal Logic**: The kernel traverses the VFS tree, identifies the correct mount point (e.g., `/dev` or `/` for FAT16), and returns a file descriptor.
    *   **Sovereignty Audit**: Every file access is logged in the kernel's internal audit ring buffer, providing a 100% trace of data movement.
*   **`sys_read(int fd, char* buf, int count)`**:
    *   **Description**: Reads bytes from a file descriptor.
    *   **Performance**: Utilizes the kernel-level **Slab Buffer Cache** for near-instant reads of frequently accessed data.
*   **`sys_write(int fd, const char* buf, int count)`**:
    *   **Description**: Writes bytes to a file descriptor.
    *   **Security**: Implements strict boundary checking to prevent "Buffer Overflow" attacks on the kernel's write buffers.

### 6.3. Networking Syscalls (The "Secure Pipe")
This is where Retro-OS diverges from other small-scale operating systems.

*   **`sys_socket_create(int type)`**: Creates a network socket connected to the **Tungsten NetStack**.
*   **`sys_http_get(const char* url, char* response, int max_len)`**:
    *   **Description**: A high-level, security-hardened syscall that performs a full HTTPS GET request.
    *   **Internal Logic**: The kernel handles the DNS resolution, TCP handshake, TLS 1.3 negotiation (including SNI/ALPN), and HTTP/1.1 headers.
    *   **Why it's Better**: By moving the HTTPS logic into the kernel services layer, we ensure that user applications do not need to link against large, insecure libraries. This is the **Retro-OS "Secure Pipe"** architecture.

### 6.4. Process and Memory Management Syscalls
*   **`sys_fork()`**: Creates a copy of the current process. Our implementation uses a modern **Copy-On-Write (COW)** mechanism to minimize memory overhead.
*   **`sys_execve(const char* path, char** args, char** env)`**: Loads and executes an ELF (Executable and Linkable Format) binary.
*   **`sys_sbrk(int increment)`**: Expands the process heap. This is the foundation for our native `malloc` and `free` implementations in the standard C library.

### 6.5. Graphical Subsystem Syscalls (The "BackBuffer" API)
*   **`sys_gfx_init(int width, int height, int bpp)`**: Initializes the VBE/BGA graphics card.
*   **`sys_gfx_draw_rect(int x, int y, int w, int h, uint32_t color)`**: Performs high-speed rectangular draws directly to the **double buffer**.
*   **`sys_gfx_swap_buffers()`**: The critical "Wait for VSync" call that flushes the off-screen buffer to the video RAM, ensuring flicker-free rendering.

### 6.6. Strategic Importance of This API
This API is designed specifically to be **Human-Readable and Secure**.
1.  **Lower Barrier to Entry**: Indian developers can learn this API in days, compared to the years required for the complex Windows Win32 or Linux glibc APIs.
2.  **Audit-Ready**: Every syscall has a clear, well-defined entry and exit point, making it trivial for agencies like **CERT-In** to verify the system's integrity.
3.  **Future-Proofing**: The API can be easily mapped to different CPU architectures (x86, ARM, RISC-V) without changing the user-space applications.

---

*Volume 7 will detail the Benchmark Analysis of the Retro-OS Networking and File System layers.*
