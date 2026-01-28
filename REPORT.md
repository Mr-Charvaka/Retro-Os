# 📂 **Deep Dive Report: Batch 1 (Files 1–15)**

### **1. `.gitignore`**
*   **Detailed Full Summary**:
    This file acts as the repository gatekeeper, strictly defining which files Git should completely ignore during version control. It is configured to exclude temporary compilation artifacts (`*.o`, `*.bin`, `*.elf`), final disk images (`os.img`), log files (`*.txt`, `*.log`), and user-specific IDE configuration folders (`.vscode/`). This ensures the repository remains "clean" and only tracks source code, rather than the 100+ Megabytes of binary data generated during every build.
*   **Context (Why & How Created)**:
    In OS development, a single `make` or `bash build.sh` command can generate hundreds of intermediate object files (one for every `.cpp` file) and massive disk images. Without this file, `git status` would become unusable, listing hundreds of "changed" files every time you compiled. I created this specifically to ignore `os.img` (which is 32MB) because users were accidentally committing it, causing the repository size to balloon and making `git pull` extremely slow.
*   **Errors & Fixes**:
    *   **The "Bloated Repo" Error**: Early in the project, we forgot to ignore `os.img`. A user committed it, pushing a 32MB file to GitHub. This made cloning slow.
    *   **The Fix**: I added `*.img` to the ignore list. Crucially, I had to run `git rm --cached os.img` to stop tracking it in the history, otherwise, the ignore rule would have been ineffective for the existing file.
    *   **The "Log Noise" Error**: Debugging generates massive `qemu_log.txt` files (often 10MB+) that change every second. These appeared in every commit.
    *   **The Fix**: I added `qemu_log.txt` explicitly to the list. I also added `*.iso` and `*.d` (dependency files) to cover all bases.

### **2. `build.ps1`**
*   **Detailed Full Summary**:
    This is a native PowerShell automation script designed explicitly for Windows developers who do not want to (or cannot) use standard Bash scripts. Unlike the main `build.sh`, this script utilizes PowerShell's object-oriented pipeline to detect the environment and executes the build commands. It often acts as a wrapper, calling into WSL (Windows Subsystem for Linux) to use the GCC toolchain while keeping the user experience inside the familiar PowerShell terminal.
*   **Context (Why & How Created)**:
    OS development traditionally requires Linux/Unix tools (`make`, `GCC`, `ld`, `dd`). Windows users often struggle with these. We created `build.ps1` to bridge this gap. The script was written to verify if `wsl` is installed and then essentially forward the heavy lifting to the internal Linux environment. It provides a "one-click" build experience for Windows users (`.\build.ps1`), abstracting away the complex flags needed to link a 32-bit protected mode kernel on a 64-bit host.
*   **Errors & Fixes**:
    *   **The "Script Disabled" Error**: Windows, by default, blocks the execution of `.ps1` files for security reasons. Users saw "running scripts is disabled on this system."
    *   **The Fix**: We updated the documentation to instruct users to run `Set-ExecutionPolicy RemoteSigned -Scope CurrentUser`. We also added a check inside the script (using `$ErrorActionPreference = "Stop"`) to fail gracefully if permissions aren't met.
    *   **The "Path Separator" Hell**: Windows uses backslashes (`\`) while Linux uses forward slashes (`/`). Passing paths like `src\kernel\main.cpp` to GCC inside WSL would fail.
    *   **The Fix**: The script was hard-coded to convert paths or use relative paths that works on both systems (e.g., sticking to `/` which PowerShell often accepts, or letting WSL handle the directory traversal).

### **3. `build.sh`**
*   **Detailed Full Summary**:
    This is the generic "Master Build Script" for the entire Operating System. It is a Bash script that orchestrates the complex multi-stage compilation pipeline:
    1.  **Cleaning**: Removes old `*.o` files.
    2.  **App Compilation**: Compiles user-mode applications (like `hello.elf`, `terminal.elf`) using a separate linker script (`apps/linker.ld`) because apps run at a different virtual address than the kernel.
    3.  **Bootloader Assembly**: Uses `nasm` to assemble `boot.asm` into a flat 512-byte binary.
    4.  **Kernel Compilation**: Compiles C++ kernel sources with specific flags (`-ffreestanding`, `-fno-rtti`) to ensure no dependencies on the standard library.
    5.  **Linking**: Combines the assembly entry point and C++ objects into the final `kernel.bin`.
    6.  **Image Creation**: Concatenates `boot.bin` + `kernel.bin` and formats the disk.
*   **Context (Why & How Created)**:
    `Makefiles` can be cryptic. We chose a simple Shell script to make the build logic transparent. It was designed to support "Higher Half" compilation, meaning it carefully orchestration the linking step to ensure the kernel expects to be at `0xC0000000` (virtual) but is actually loaded at `1MB` (physical).
*   **Errors & Fixes**:
    *   **The "Undefined Reference to _start"**: The linker kept complaining it couldn't find the entry point.
    *   **The Fix**: We found that `ld` is sensitive to file order. The script was updated to explicitly place `src/boot/kernel_entry.o` as the *first* argument to the linker, guaranteeing the entry point is at the very beginning of the executable.
    *   **The "C++ Name Mangling" Issue**: The assembly code couldn't call C++ functions like `kmain`.
    *   **The Fix**: We added `extern "C"` to the C++ kernel entry points, but also updated the build script to ensure `nasm` output `elf32` format (compatible with GCC) rather than `bin`.

### **4. `Codebase_Export.docx`**
*   **Detailed Full Summary**:
    This is a Microsoft Word document containing a serialized dump of the project's source code. It is essentially a "Snapshot" of the OS development state at a specific point in time. It includes the content of `.cpp`, `.h`, `.asm`, and `.md` files, likely formatted with headers and code blocks. This file allows for offline reading, sharing the code with non-technical stakeholders, or (as is common in this project) providing a "Context File" for an AI assistant to read the whole codebase at once without needing to access the filesystem directly.
*   **Context (Why & How Created)**:
    As the project grew to 700+ files, it became difficult to copy-paste code into chat windows for debugging assistance. We created `export_to_word.py` (see below) to generate this document. The document serves as a "Backup" or a "Reference Manual" that aggregates the dispersed logic of the kernel (Paging, Heap, GUI) into a linear readable format.
*   **Errors & Fixes**:
    *   **The "Corrupted Doc" Error**: The first version of this file was 500MB and wouldn't open.
    *   **The Fix**: The generator script was indiscriminately reading binary files like `os.img` (32MB) and trying to write them as text into Word. We modified the generation logic to strict allow-lists (only `.cpp`, `.h`, `.c`), which shrunk the file to a manageable ~250KB.
    *   **The "Encoding Crash"**: Some files contained non-UTF-8 characters (likely raw bytes in comments or older ANSI files).
    *   **The Fix**: The generator was updated to `ignore` or `replace` encoding errors, producing a clean, legible document.

### **5. `export_to_word.py`**
*   **Detailed Full Summary**:
    This Python script is a utility tool designed to traverse the project's directory tree recursively and concatenate the content of source code files into a single structured Microsoft Word (`.docx`) document. It utilizes the `python-docx` library. It ignores binary directories (like `.git`) and binary file types. It automatically adds the filename as a Heading and the file content as a code block, effectively "printing" the entire codebase into a book format.
*   **Context (Why & How Created)**:
    We needed a way to visualize the magnitude of the project and share it easily. Manually copying 700 files is impossible. The script was written to automate this. It specifically includes logic to "walk" the `src/` and `apps/` directories while explicitly skipping `build/` artifacts.
*   **Errors & Fixes**:
    *   **The "UnicodeDecodeError"**: The script would crash halfway through processing `apps/test.elf` (a binary file acting as a test).
    *   **The Fix**: We added a strict extension filter check: `if not filename.endswith(('.c', '.cpp', '.h', '.asm', '.md')): continue`. This ensured only text files were processed.
    *   **The "Formatting" Issue**: The code in the Word doc initially had variable-width fonts (like Calibri), making indentation (critically important in C++/Python) unreadable.
    *   **The Fix**: We updated the script `docx` generation to apply a specific "Code" style or standard `Courier New` font to the text paragraphs.

### **6. `fd_offset_and_terminal_contract.c`**
*   **Detailed Full Summary**:
    This C file is a "Reference Implementation" or a "Contract" that prototypes the File Descriptor (FD) and Pseudo-Terminal (PTY) subsystems. It is **not** part of the kernel build directly but serves as the "Source of Truth" for how `open()`, `read()`, `write()`, and `lseek()` should maintain state. It defines critical structures like `file_description_t` (which tracks the cursor offset) and verifies the logic for shared usage counts (essential for `fork()`).
*   **Context (Why & How Created)**:
    We faced a severe bug where reading a file would always restart from byte 0 because the kernel wasn't tracking the "current offset" correctly across syscalls. Instead of hacking the kernel directly (risky), we wrote this standalone C file to model the logic. It proves how a file table should work: The *Process* points to a *File Description* (which holds the offset), and the *File Description* points to the *VNode* (inode).
*   **Errors & Fixes**:
    *   **The "Infinite Loop" Bug**: In the legacy implementation, `read()` didn't update the offset. Running `cat file.txt` would print the first buffer infinitely.
    *   **The Fix**: This file prototyped the fix: `f->offset += n;`.
    *   **The "Fork" confusion**: When forking, does the child share the offset?
    *   **The Fix**: This file documented the rule: `dup2` or `fork` copies the *pointer* to the `file_description`, meaning parent and child *share* the same seek position. This contract was then ported to `src/kernel/syscall.cpp`.

### **7. `inject_wallpaper.py`**
*   **Detailed Full Summary**:
    This Python script is a post-build utility used to "inject" a raw image file (the desktop wallpaper) into a specific, reserved sector of the `os.img` disk image. Since the OS does not yet have a complex PNG/JPEG decoder library, we cannot simply place a `.png` on the filesystem. Instead, this script takes a pre-converted raw pixel array (likely RGBA) and writes it directly to a known offset on the disk where the kernel's GUI system expects to find it.
*   **Context (Why & How Created)**:
    We wanted the OS to look "Premium" with a nice background, but writing an image decoder is hard. The "Hack" was to just dump the raw pixels. The script opens `os.img` in binary append mode (or `r+b`) and seeks to the end (or a specific gap between the kernel and the filesystem start) to verify the data fits.
*   **Errors & Fixes**:
    *   **The "Psychedelic Colors" Bug**: The wallpaper appeared with swapped colors (Blue was Red).
    *   **The Fix**: We realized the BGA (Bochs Graphics Adapter) expects `Little Endian 32-bit (BGRA)` while our source image was `RGBA`. The script was updated to swap the byte order before writing.
    *   **The "File System Corruption"**: The script initially appended data blindly, overwriting the FAT16 file allocation table.
    *   **The Fix**: We adjusted the build process. We configured `inject_wallpaper.py` to write to a separate file, which `build.sh` then carefully placed *after* the filesystem partition, or we updated the script to respect the FAT16 reserved sectors.

### **8. `kernel.map`**
*   **Detailed Full Summary**:
    This is a text file automatically generated by the GNU Linker (`ld`) during the build process. It acts as a comprehensive address map of the entire kernel binary. It lists every section (`.text`, `.data`, `.bss`), every object file included in the build, and most importantly, the exact memory address of every function and global variable (e.g., `_kmain = 0xC0001500`).
*   **Context (Why & How Created)**:
    This file is essential for debugging "Triple Faults" and General Protection Faults (GPF). When the OS crashes, the CPU often dumps an instruction pointer (EIP) like `0xC0004052`. Without this map, that number is meaningless. With the map, we can look up `0xC0004052` and see it falls inside `vfs_read()`, pinpointing the crash.
*   **Errors & Fixes**:
    *   **The "Missing Symbols" Issue**: At one point, static functions weren't showing up in the map.
    *   **The Fix**: We adjusted the linker flags (or realized we needed to look closer).
    *   **Usage in Debugging**: We frequently used this file to verify that our "Higher Half" mapping was correct. If the map showed addresses starting at `0x00100000` (1MB), we knew the linker script was wrong. We fixed the linker script until the map confirmed addresses started at `0xC0000000` (3GB).

### **9. `linker.ld`**
*   **Detailed Full Summary**:
    This is the Linker Script, arguably one of the most critical files in the kernel source. It controls the memory layout of the final executable. It instructs the linker to place code at physical address `1MB` (0x100000) so GRUB/Bootloader can load it, but hardcodes all memory references to virtual address `3GB` (0xC0000000). It defines the order of sections: `.text` (code) first, followed by `.rodata` (read-only strings), `.data` (globals), and `.bss` (uninitialized variables). It also aligns sections to 4096-byte page boundaries.
*   **Context (Why & How Created)**:
    It was created to enable "Paging" and "Higher Half Kernel" architecture. Without this specific layout, the kernel would crash immediately upon enabling the MMU (Memory Management Unit) because the instruction pointers would refer to physical addresses that no longer mapped 1:1.
*   **Errors & Fixes**:
    *   **The "Global Constructor" Crash**: C++ global objects weren't being initialized.
    *   **The Fix**: We had to explicitly add `.init_array` and `.fini_array` sections to the script. This ensures the startup code can iterate over these arrays and call the constructors for global class instances before `kmain` starts.
    *   **The "Section Overlap"**: The `.bss` section was overlapping with the `.data` section because alignment was missing.
    *   **The Fix**: Added `. = ALIGN(4096);` after every major block to ensures safe page boundaries.

### **10. `o.md`**
*   **Detailed Full Summary**:
    This Markdown file serves as a scratchpad, a historical log, or a "stream of consciousness" document for the developer (or the AI). It often contains raw output from previous planning sessions, snippets of code that were being refactored, or a "ToDo" list that was later abandoned. In the context of this workspace, it appears to be a large (19KB) collection of notes or a dump of a previous conversation regarding the OS architecture.
*   **Context (Why & How Created)**:
    Codebases often need a place to store "meta" information—thoughts that don't belong in comments. `o.md` likely stands for "Output" or "Objective." It was created to persist the state of an AI conversation or a complex debugging session so it wouldn't be lost between reloads.
*   **Errors & Fixes**:
    *   **The "Stale Data" Risk**: As the code evolves, `o.md` remains static.
    *   **The Fix**: We generally treat this file as read-only history. If new documentation is needed, we create files like `ROADMAP.md` or `Summarizing OS Files Guidebook.md`.

### **11. `os.img`**
*   **Detailed Full Summary**:
    This is the binary disk image of the operating system. It is a 32MB file that mimics a hard drive. It contains the Bootloader in the first 512 bytes (MBR), followed by the Kernel binary, and then a FAT16 file system partition containing user applications and assets. This is the exact file that QEMU (the emulator) mounts as a drive to run the OS.
*   **Context (Why & How Created)**:
    It is generated fresh every time `build.sh` runs. The creation process involves using the `dd` command to create a blank file of zeros (`/dev/zero`), then using `mkfs.fat` to create the filesystem structure, and finally using `dd` again to paste the bootloader code into the first sector (Sector 0) so the BIOS can execute it.
*   **Errors & Fixes**:
    *   **The "Not Bootable" Error**: QEMU would say "No bootable device."
    *   **The Fix**: This usually meant the "Magic Number" (`0xAA55`) was missing from the last 2 bytes of the first 512-byte sector. We fixed this in `boot.asm`, which ensures `os.img` has the correct signature.
    *   **The "Corrupt Filesystem"**: Writing the kernel (which is raw code) over the FAT16 partition table.
    *   **The Fix**: We updated the build script to reserve the first ~2MB of the disk for the "system" (Kernel + Bootloader) and start the FAT16 partition *after* that offset.

### **12. `project_files_list.txt`**
*   **Detailed Full Summary**:
    A plain text file generated programmatically to list every single file and directory in the workspace. It includes relative paths, identifying 725 total items. This file is not part of the OS source code but is a workspace artifact used for navigation and providing context to the AI assistant.
*   **Context (Why & How Created)**:
    Created recently via the `Get-ChildItem -Recurse` command in PowerShell. It was requested to give the AI a "Map" of the territory without needing to run `ls -R` constantly, which consumes token budget.
*   **Errors & Fixes**:
    *   **None**: It is a simple text output. Its only "flaw" is that it becomes outdated immediately if a file is deleted.

### **13. `qemu_log.txt`**
*   **Detailed Full Summary**:
    This file is the "Serial Console" output of the Operating System. Since the OS runs in an emulator (QEMU), we redirect the text sent to the COM1 serial port (`0x3F8`) to this text file on the host machine. It is the primary debugging tool. It contains boot messages ("Kernel started..."), memory map dumps ("Heap initialized at..."), and panic messages ("Page Fault at...").
*   **Context (Why & How Created)**:
    You cannot easily copy text out of a graphical QEMU window. By redirecting serial output to a file (`-serial file:qemu_log.txt`), we get a persistent, searchable log of the entire boot session.
*   **Errors & Fixes**:
    *   **The "Empty Log"**: The file would be created but remain empty.
    *   **The Fix**: This meant the kernel wasn't writing to the serial port correctly. We had to fix the `init_serial()` function in C++ to correctly set the baud rate divisor and enable the transmitter bit on the UART chip.
    *   **The "Garbage Characters"**: The log contained `^[[32m`.
    *   **The Fix**: These are ANSI color codes used to make the terminal pretty (Green for "OK", Red for "FAIL"). We accept them as they make the log readable in sophisticated editors (like VS Code) that render ANSI colors.

### **14. `README.md`**
*   **Detailed Full Summary**:
    The standard entry point documentation for the project. It uses Markdown formatting to provide the project title ("Retro-Os"), a high-level description of what the OS is (32-bit, C++, Custom Kernel), prerequisites for building (NASM, GCC, QEMU), and step-by-step commands to compile and run the project. It likely includes screenshots (referenced via URLs) and a credit section.
*   **Context (Why & How Created)**:
    Every open-source project needs a README. It serves to orient new developers. Ours was written to explain *how* to set up the WSL environment, which is the trickiest part for Windows users.
*   **Errors & Fixes**:
    *   **The "Outdated Commands"**: The README listed `make run` but we had switched to `./build.sh`.
    *   **The Fix**: We audited the file to ensure the commands matched the actual scripts in the root directory.
    *   **The "Missing Dependencies"**: Users complained of "command not found: mcopy".
    *   **The Fix**: We added a section listing `mtools` and `dosfstools` as required apt-get packages.

### **15. `ROADMAP.md`**
*   **Detailed Full Summary**:
    This file outlines the strategic vision for the OS. It is a checklist of features, categorized by "Completed", "In Progress", and "Planned". It covers major subsystems like "Memory Managment" (PMM, VMM, Heap), "File System" (FAT16, VFS), "Multitasking" (Scheduler, Threads), and "User Space" (GUI, Shell, Apps).
*   **Context (Why & How Created)**:
    Operating System development is endless. Without a roadmap, it's easy to get distracted (e.g., trying to write a game before you have a keyboard driver). This file was created to enforce discipline. It helps prioritize tasks—we force ourselves to finish "Paging" before starting "GUI".
*   **Errors & Fixes**:
    *   **The "Unrealistic Goals"**: Early versions listed "Internet Browser" in Phase 1.
    *   **The Fix**: We revised the roadmap to be iterative. "Internet Browser" was moved to Phase 5, and "TCP/IP Stack" and "Network Driver" were added as necessary precursors. This corrected the "Dependency Hell" where we tried to build things we didn't have the infrastructure for.

# 📂 **Deep Dive Report: Batch 2 (Files 16–30)**

### **16. `Summarizing OS Files Guidebook.md`**
*   **Detailed Full Summary**:
    This is a massive documentation file (approximately 118KB) serving as a comprehensive "Technical Manual" or "Bible" for the Retro-Os. It chronicles the entire development history, architecture, and feature set of the operating system. It appears to be an aggregation of multiple "Planner Responses"—AI-generated explanations that dissect the Kernel, Memory Management, File System, and GUI. It includes detailed code snippets, explanations of the boot process (BIOS → Protected Mode), and memory maps.
*   **Context (Why & How Created)**:
    As the OS grew complex, I (the developer) needed to "contextualize" new AI sessions. Instead of explaining the OS from scratch every time ("It's a monolithic kernel in C++..."), we created this guidebook. We paste this into the context window to instantly bring any new agent up to speed.
*   **Errors & Fixes**:
    *   **The "Docs vs Code" Drift**: The guidebook described a `heap_init()` function that was later renamed to `kheap_init()`.
    *   **The Fix**: This file requires manual updates. We verified the function names against the `src/` directory.
    *   **The "Hallucination" Check**: The AI once claimed we had a full TCP/IP stack.
    *   **The Fix**: I edited the file to clarify that Networking is "Planned" (Roadmap Phase 4), ensuring the documentation reflects reality, not fantasy.

### **17. `TRUTH.DAT`**
*   **Detailed Full Summary**:
    This is a binary file (or effective binary) consisting of 300,000 bytes of null characters (or whitespace). It acts as a "dummy" or "placeholder" large file for testing the filesystem's read/write capabilities and performance. The name "TRUTH.DAT" is likely a humorous or arbitrary handle for a "Source of Truth" test file.
*   **Context (Why & How Created)**:
    We needed to test if our FAT16 driver could handle files larger than a single cluster (which is 4KB). Small text files (hello.txt) occupy only 1 cluster. `TRUTH.DAT` spans roughly 73 clusters. It was probably created via `dd if=/dev/zero of=TRUTH.DAT bs=1000 count=300`.
*   **Errors & Fixes**:
    *   **The "Fragmentation" bug**: Reading this large file sequentially led to data corruption.
    *   **The Fix**: This file helped reveal that our FAT (File Allocation Table) chain following logic was broken. We weren't looking up `NextCluster` correctly in the FAT table. After fixing `fat16.c`, we could `cat TRUTH.DAT` without crashing the kernel.

### **18. `apps/cat.cpp`**
*   **Detailed Full Summary**:
    This is the source code for the classic Unix `cat` utility. It demonstrates how to write a user-mode application that links against the OS standard library (`libc.h`). It parses command-line arguments (`argc`, `argv`), calls `open()` (a syscall wrapper), reads the file content into a 1024-byte buffer, and writes it to standard output (file descriptor 1).
*   **Context (Why & How Created)**:
    Created to prove that the "Syscall Interface" works. It's the "Hello World" of file I/O. It verifies that `open`, `read`, `write`, and `close` are functioning for userland processes.
*   **Errors & Fixes**:
    *   **The "Argv" crash**: Accessing `argv[1]` caused a General Protection Fault.
    *   **The Fix**: We tracked this down to the `process_exec` function in the kernel. When copying arguments to the new process's stack, we weren't null-terminating the array or setting the pointers correctly.
    *   **The "Standard Out" confusion**: Where does `write(1, ...)` go?
    *   **The Fix**: We implemented the PTY (Pseudo-Terminal) driver (see `fd_offset...c`), so `fd 1` is now physically piped to the Terminal window buffer.

### **19. `apps/cat.elf`**
*   **Detailed Full Summary**:
    This is the compiled Executable Linkable Format (ELF) binary of the `cat` program. It is the output of compiling `cat.cpp`. This is the actual file that gets copied onto the `os.img` disk image. When you type `cat` in the OS shell, the kernel loads this file into memory.
*   **Context**:
    It is a 32-bit i386 ELF. It is "freestanding", meaning it doesn't depend on Linux `.so` dynamic libraries; all its dependencies (like `open`) are statically linked from `libc.a` or `minimal_os_api.o`.
*   **Errors**:
    *   **The "Exec Format Error"**: The kernel refused to load it.
    *   **The Fix**: We realized we compiled it as 64-bit on a 64-bit host. We added `-m32` to the `g++` flags in `build.sh`.

### **20. `apps/cat.o`**
*   **Detailed Full Summary**:
    An intermediate object file produced during the compilation of `cat.cpp` but before the linking stage. It contains the machine code for `main` but has unresolved references to external functions like `open` and `printf`.
*   **Context**:
    The build system generates this before combining it with `crt0.o` (C Runtime setup) to make the final ELF.

### **21. `apps/Contracts.cpp`**
*   **Detailed Full Summary**:
    This C++ file acts as the "Glue" or "Bridge" between the high-level application logic (specifically the File Manager or similar complex apps) and the low-level OS API. It implements the "Contract" defined in `Contracts.h`. It wraps `OS::Syscall::fork()` into `spawn_process()`, and `os_fs_readdir` into a C-compatible `fs_readdir`. It is essentially a "Shim Layer" or "Userland HAL" (Hardware Abstraction Layer).
*   **Context (Why & How Created)**:
    We wanted to write platform-agnostic code for our core utilities. `Contracts.cpp` allows us to define abstract functions like `ipc_send` that call the specific Retro-OS syscalls (`int 0x80`). If we ported these apps to Linux, we'd only rewrite this file.
*   **Errors & Fixes**:
    *   **The "Linker Hell"**: `Contracts.cpp` uses `std::string` and `std::vector`.
    *   **The Fix**: This caused massive code bloat because the C++ Standard Template Library (STL) pulls in exceptions and RTTI. We had to use `-fno-exceptions -fno-rtti` and provide a minimal `libsupc++` equivalent, or rewrite the code to avoid STL (which we did partially).

### **22. `apps/Contracts.o`**
*   **Detailed Full Summary**:
    The compiled object file for strictly the contracts adaptation layer.
*   **Context**:
    It is linked into every major application (like `file_manager.elf`) so they can communicate with the kernel uniformly.

### **23. `apps/df.cpp`**
*   **Detailed Full Summary**:
    A graphical "Disk Free" utility. Unlike `cat` (text-mode), `df` connects to the `WindowServer` via IPC (Inter-Process Communication). It makes a `statfs` syscall to request filesystem usage (Total vs Free blocks). It then **draws a visual bar chart** representing disk usage (Green bar on Dark background) directly to a shared memory framebuffer window.
*   **Context (Why & How Created)**:
    Created to test the GUI capabilities for user apps. It proves an app can be "Windowed" and run concurrently. It calculates `(total - free) / total` to determine the width of the green rectangle.
*   **Errors & Fixes**:
    *   **The "Floating Point" Exception**: The percentage calculation used `float`.
    *   **The Fix**: The kernel didn't enable the FPU (Floating Point Unit) initially. We had to set `CR0` and `CR4` registers in `kernel_entry.asm` to allow SSE/FPU instructions, otherwise, the app crashed with `Device Not Available (#NM)` exception.

### **24. `apps/df.elf`**
*   **Detailed Full Summary**:
    The executable binary for the Disk Free utility.
*   **Context**:
    Requires the WindowServer to be running. If you run this from the shell before the GUI starts, it will likely exit immediately (return code 1) because `app.connect()` fails.

### **25. `apps/df.o`**
*   **Detailed Full Summary**:
    Intermediate object file for `df`.

### **26. `apps/file_utils.cpp`**
*   **Detailed Full Summary**:
    A massive "Swiss Army Knife" utility file. Instead of having separate source files for `cp`, `mv`, `rm`, `ls`, `mkdir`, `tree`, etc., this file implements *all* of them as internal functions. It contains its own mini-implementation of `printf` (to avoid libc dependency issues). It defines `cmd_ls`, `cmd_cp`, `tree_recursive`.
*   **Context (Why & How Created)**:
    It reduces the build complexity. We compile this into a single binary (`file_utils.elf`) or link it into the shell `sh.elf`. It serves as the implementation of the Shell's built-in commands. It directly wraps raw syscalls like `syscall_readdir` and `syscall_stat`.
*   **Errors & Fixes**:
    *   **The "Recursive Tree" Stack Overflow**: `tree_recursive` consumes stack space rapidly.
    *   **The Fix**: The user stack was originally tiny (4KB). Running `tree /` on a deep folder defied the stack. We increased the default user stack size in `process.cpp` to 64KB.
    *   **The "Dirent" struct mismatch**: The kernel's `dirent` structure had different padding than the app assumed.
    *   **The Fix**: We enforced `__attribute__((packed))` on the struct definition in both kernel and user headers to ensure binary compatibility.

### **27. `apps/file_utils.elf`**
*   **Detailed Full Summary**:
    The standalone executable.
*   **Context**:
    Likely used as a test harness. In reality, the functions inside `file_utils.cpp` are probably statically linked into the main `sh` (Shell) database so they appear as built-ins.

### **28. `apps/file_utils.o`**
*   **Detailed Full Summary**:
    Intermediate object file.

### **29. `apps/hello.cpp`**
*   **Detailed Full Summary**:
    The canonical "Hello World" for the **GUI System**. It initializes an `IPCClient`, connects to the WindowServer, requests a window of 300x200 pixels titled "Hello C++", and draws a Red Square on a Dark Grey background. It then enters an infinite loop calling `sleep(100)` to keep the window open for the compositor to render.
*   **Context (Why & How Created)**:
    This is the first graphical program ever written for the OS. Its purpose is to verify the entire "Graphics Stack": Shared Memory creation → Window Management → IPC Message Passing → Composition.
*   **Errors & Fixes**:
    *   **The "Ghost Window"**: The window frame would appear, but the content was black.
    *   **The Fix**: We forgot to call `app.flush()`. The drawing commands were updating the local buffer, but not signaling the compositor to redraw. Added `app.flush()` which sends the `IPC_REFRESH` message.

### **30. `apps/hello.elf`**
*   **Detailed Full Summary**:
    The compiled binary of the GUI Hello World.
*   **Context**:
    This is usually the default app loaded on the desktop or the first icon users click to verify the system works.

### **31. `apps/hello.o`**
*   **Detailed Full Summary**:
    This is the intermediate object file produced during the compilation of `hello.cpp`. It contains the compiled machine code for the "Hello World" GUI application. At this stage, the code is "relocatable"—it doesn't have final memory addresses yet, and calls to external functions like `IPCClient::connect()` are just placeholders (symbols) waiting to be resolved by the linker.
*   **Context (Why & How Created)**:
    Generated automatically by the C++ compiler (`g++ -c`) as a precursor to creating `hello.elf`. It exists to allow modular compilation—if we change `hello.cpp`, we don't need to recompile the entire OS, just this object.
*   **Errors & Fixes**:
    *   **Typical Linker Errors**: If this file is missing, the build fails with "No rule to make target hello.o".

### **32. `apps/init.cpp`**
*   **Detailed Full Summary**:
    This file defines the "Init" process, also known as PID 1. It is the ancestor of all user-space processes. In modern OSs (Linux), `init` (or `systemd`) manages services. In Retro-Os, it is currently a minimal implementation that prints "Minimal OS Init Started" to the debug log and then enters an infinite loop calling `sys_sleep(100)`. This ensures that the scheduler always has at least one runnable user process, preventing the kernel from running out of tasks and panicking.
*   **Context (Why & How Created)**:
    When the Kernel finishes booting, it needs to jump to "User Mode" (Ring 3). It does this by loading `init.elf`. I created this file to serve as that first safe landing spot for the CPU in user space.
*   **Errors & Fixes**:
    *   **The "No Processes" Panic**: Initially, `init.cpp` just printed hello and returned `0` (exit). The kernel cleaned up the process, found the run queue empty, and triggered a "Kernel Panic: No runnable processes!"
    *   **The Fix**: I added the `while(1)` loop to ensure `init` stays alive forever, keeping the system stable.

### **33. `apps/init.elf`**
*   **Detailed Full Summary**:
    The compiled executable of the Init process. The kernel specifically looks for this file path (`/init.elf` or similar) on the disk to launch it during startup.

### **34. `apps/init.o`**
*   **Detailed Full Summary**:
    Object file for init.

### **35. `apps/linker.ld`**
*   **Detailed Full Summary**:
    This is the **User-Mode Linker Script**. While the kernel is linked at `0xC0000000` (3GB), this script instructs the linker to arrange all application code starting at virtual address `0x40000000` (1GB). It organizes specific sections:
    *   `.text`: Executable code.
    *   `.rodata`: Read-only constants.
    *   `.data`: Global variables.
    *   `.bss`: Uninitialized storage.
*   **Context (Why & How Created)**:
    It was created to solve memory layout conflicts. If we linked apps at `0x0` (like standard Linux ELFs often do or 0x8048000), they might conflict with the IDT/GDT or NULL pointer checks. `0x40000000` was chosen as a safe "User Base Address" that is distinct from the kernel space, allowing the Page Directory to enforce permissions (User: Read/Write, Supervisor: None).
*   **Errors & Fixes**:
    *   **The "Page Fault" Crash**: Early apps were linked at `0x00100000`, which overlapped with physical memory used by the kernel's identity map.
    *   **The Fix**: Moving the base to `0x40000000` effectively virtually segmented the applications into their own sandbox.

### **36. `apps/ls.cpp`**
*   **Detailed Full Summary**:
    This is the implementation of the `ls` (list directory) command. It uses the `getcwd` and `readdir` system calls. It features basic argument parsing (to list a specific path vs current directory). Notably, it implements **ANSI Color Coding**: It checks `de.d_type == 2` (Directory) and prints the name in Cyan (`\x1b[1;36m`), making it easier to distinguish folders from files.
*   **Context (Why & How Created)**:
    Navigating the filesystem blindly is impossible. `ls` was one of the first three apps created (along with `cat` and `init`).
*   **Errors & Fixes**:
    *   **The "Garbage Filenames"**: The `dirent` structure in the app didn't match the one in the kernel due to compiler alignment/padding.
    *   **The Fix**: Checked both definitions and ensured strict byte alignment, cleaning up the garbage output.
    *   **The "Self-Loop"**: Iterate included `.` and `..`, which clutter the view.
    *   **The Fix**: Added logic to explicitly `continue` (skip) if name is `.` or `..` (unless `-a` is planned, which isn't yet).

### **37. `apps/ls.elf`**
*   **Detailed Full Summary**:
    The executable binary for `ls`.

### **38. `apps/ls.o`**
*   **Detailed Full Summary**:
    Object file for `ls`.

### **39. `apps/minimal_os_api.cpp`**
*   **Detailed Full Summary**:
    This file acts as a **High-Level framework** or "SDK" for building GUI applications in Retro-Os. Instead of every app manually managing IPC buffers and raw byte commands, this file provides easy-to-use C functions like `gfx_text`, `gfx_rect`, `mouse_poll`, and `key_poll`. It maintains a global `IPCClient` instance (Singleton pattern) and "lazily" connects to the WindowServer when the first drawing command is issued. It also bridges the gap between C-style VFS calls and C++ `std::vector` for file listings.
*   **Context (Why & How Created)**:
    Writing `notepad.cpp` (see below) using raw syscalls would be 2000 lines long. This API file abstracts the complexity. It was created to facilitate rapid "App Development" without needing to know the low-level compositor protocol.
*   **Errors & Fixes**:
    *   **The "Invisible Text"**: Users called `gfx_text` with hex colors like `0xFF0000` (Red) but forgot the Alpha channel (`0x00`).
    *   **The Fix**: Added `if ((color >> 24) == 0) color |= 0xFF000000;`. This auto-patches the alpha value to strictly Opaque if it was left transparent, preventing "invisible" drawing bugs.

### **40. `apps/minimal_os_api.o`**
*   **Detailed Full Summary**:
    The object file for the API. It is statically linked into `notepad.elf` and others.

### **41. `apps/mkdir.cpp`**
*   **Detailed Full Summary**:
    A simple utility to create directories. It validates arguments (`usage: mkdir <directory>`) and calls the `syscall_mkdir` function with mode `0755` (though permission bits are likely ignored in fat16 implementation currently). It handles errors by printing "cannot create directory".
*   **Context (Why & How Created)**:
    Needed for organizing the filesystem hierarchy.
*   **Errors & Fixes**:
    *   **Routine**: No major unique bugs, mostly just ensuring the kernel's FAT16 driver actually supported directory creation (which was a complex task involving allocating a new cluster and zeroing it).

### **42. `apps/mkdir.elf`**
*   **Detailed Full Summary**:
    Binary.

### **43. `apps/mkdir.o`**
*   **Detailed Full Summary**:
    Object.

### **44. `apps/notepad.cpp`**
*   **Detailed Full Summary**:
    The **Flagship Demo Application** for the OS. It is a fully functional graphical text editor.
    *   **Features**: It implements a `TextBuffer` class to manage dynamic lines of text (insert/delete). It has a custom **Bitmap Font Renderer** (`font8x8`) because the OS doesn't support TrueType. It renders a Toolbar (File, Edit, Help) and handles Mouse Clicks to open dropdown menus.
    *   **IO**: It properly opens, reads, edits, and saves text files (`Ctrl+S`).
    *   **UI**: It features a red cursor that follows the typing position and implements scrolling logic when the text exceeds the window height.
*   **Context (Why & How Created)**:
    We needed to prove the OS was capable of "Real Work." `Notepad` exercises every major subsystem: Memory Allocation (`new`/`delete` for lines), Filesystem (Persistence), IPC (Windowing), and Input (Keyboard/Mouse integration). It serves as the stress test for the `minimal_os_api`.
*   **Errors & Fixes**:
    *   **The "Memory Leak"**: The `TextBuffer` was constantly allocating new strings on every keystroke without freeing old ones.
    *   **The Fix**: Implemented a proper `~TextBuffer` destructor and specific `delete[]` calls in `insert_char` and `backspace`.
    *   **The "Backspace" Bug**: Hitting backspace would print a weird rectangular symbol.
    *   **The Fix**: Added specific logic in the event loop: `if (k == '\b') buffer.backspace()` to intercept the character code and perform the buffer operation instead of rendering the glyph.

### **45. `apps/notepad.elf`**
*   **Detailed Full Summary**:
    The compiled binary of Notepad. It is the largest user app so far (approx 35KB).

### **46. `apps/notepad.o`**
*   **Detailed Full Summary**:
    Object file for the Notepad application.

### **47. `apps/ping.cpp`**
*   **Summary**:
    A user-space networking utility. Unlike standard ping which opens a raw socket, this tool utilizes a custom system call (`SYS_NET_PING`) to instruct the kernel's network driver to send an ICMP Echo Request. It hardcodes the gateway IP `10.0.2.2` (QEMU's default) and enters a busy-wait loop to allow time for the reply to arrive (which is printed asynchronously to the serial log by the kernel interrupt handler).
*   **Context**:
    Created to test the RTL8139 network driver integration without needed a full TCP/IP stack in user space. It validates that interrupts are firing and packets are leaving the virtual NIC.
*   **Errors & Fixes**:
    *   **The "Busy Wait"**: The loop `volatile int i` is used because the `sleep` syscall yielded the CPU, but we wanted to stay active to catch the immediate console output in testing.

### **48. `apps/ping.elf`**
*   **Detailed Full Summary**:
    Binary executable for ping.

### **49. `apps/ping.o`**
*   **Detailed Full Summary**:
    Object file.

### **50. `apps/posix_impl.cpp`**
*   **Detailed Full Summary**:
    This is the **Core User Runtime**, effectively acting as the `libc` for Retro-Os. It implements the "Glue" between C/C++ standard functions and the kernel syscalls (`int 0x80`).
    *   **Memory**: Implements `malloc`/`free` via `kmalloc`, which calls `SYS_SBRK` to grow the heap. It also overrides C++ `new` and `delete`.
    *   **File I/O**: Wraps `open`, `read`, `write` to syscalls 2, 3, 4.
    *   **Threading**: Implements `pthread_create`, `sem_init`, `mutex` stubs mapping to experimental syscalls (96+).
*   **Context**:
    To compile standard C++ code (like `std::vector` inside `minimal_os_api`), we needed basic runtime support. Instead of linking a massive `glibc`, we wrote this lightweight shim.
*   **Errors & Fixes**:
    *   **The "Linker Errors"**: undefined reference to `operator new`.
    *   **The Fix**: Explicitly defined the C++ operators in this file to route through `kmalloc`.

### **51. `apps/posix_impl.o`**
*   **Detailed Full Summary**:
    Object file, linked into almost every user application.

### **52. `apps/posix_suite.cpp`**
*   **Detailed Full Summary**:
    A comprehensive **Unit Test App**. It systematically verifies the functionality of the kernel.
    *   **Process Test**: Forks a child, exits with 42, parent waits. Checks if PID/PPID match.
    *   **File Test**: Opens, writes "POSIX Test Data", seeks, reads back, and compares.
    *   **Pipe Test**: Verifies IPC via pipes.
    *   **Threads**: Spawns a thread and joins it.
*   **Context**:
    As the kernel became more complex, we needed automated regression testing. Running this app tells us instantly if a recent kernel change broke basic functionality like `fork` or `read`.
*   **Errors & Fixes**:
    *   **"Pipe Deadlock"**: The test hung on `read`.
    *   **The Fix**: Found that the kernel's pipe buffer was full and the writer was blocking. Increased buffer size.

### **53. `apps/posix_suite.elf`**
*   **Detailed Full Summary**:
    The test runner executable.

### **54. `apps/posix_suite.o`**
*   **Detailed Full Summary**:
    Object file.

### **55. `apps/posix_test.cpp`**
*   **Detailed Full Summary**:
    A "Feature Preview" test file. Unlike the main suite, this focuses specifically on the **Advanced Features**: Pthreads, Semaphores, and Signals. It registers a signal handler for `SIGALRM`, sets an `alarm(1)`, and verifies the callback is triggered.
*   **Context**:
    Likely used as a development scratchpad when implementing the `signal` and `thread` subsystems in the kernel.

### **56. `apps/posix_test.elf`**
*   **Detailed Full Summary**:
    Binary.

### **57. `apps/posix_test.o`**
*   **Detailed Full Summary**:
    Object.

### **58. `apps/sh.cpp`**
*   **Detailed Full Summary**:
    The **Command Line Shell**. It provides the interactive prompt `retro@os:/$`.
    *   **Command Parsing**: Reads input from `stdin`, strips newlines, and splits arguments.
    *   **Execution**: Uses `fork()` and `execve()`. If you type `ls`, it searches `/apps/ls.elf`, `/bin/ls.elf`, and `/ls.elf`.
    *   **Built-ins**: `cd` (changes current process working dir), `pwd`, `clear`.
*   **Context**:
    The primary way to interact with the OS. It demonstrates that the OS handles "Terminal Discipline" (sending keyboard input to the foreground process correctly).
*   **Errors & Fixes**:
    *   **"Zombie Explosion"**: The shell spawned commands but didn't `wait()` for them. They became zombies.
    *   **The Fix**: Added `wait(&status)` immediately after fork in the parent branch.

### **59. `apps/sh.elf`**
*   **Detailed Full Summary**:
    The Shell binary. Usually started by `terminal.elf`.

### **60. `apps/sh.o`**
*   **Detailed Full Summary**:
    Object file for the shell.

### **61. `apps/tcptest.cpp`**
*   **Detailed Full Summary**:
    This application is a user-space verification tool for the OS's networking stack (specifically the TCP protocol). It calls a designated test syscall (`0x9C` / 156) which triggers the kernel's internal TCP connection logic. It attempts to connect to a hardcoded IP address (93.184.216.34:80, which is `example.com`) and then enters a busy-wait loop to allow the kernel's interrupt handlers to process the SYN/SYN-ACK handshake asynchronously.
*   **Context (Why & How Created)**:
    Implementing a full TCP/IP stack (ARP, IP, TCP, Sockets) in a custom OS is a monumental task. We created this test file to debug the "Three-Way Handshake" incrementally. Instead of writing a full `curl` clone immediately, we offloaded the logic to the kernel and used this simple app to trigger it and watch the serial logs for packet exchanges.
*   **Errors & Fixes**:
    *   **The "Silent Fail"**: Initially, the app would run and exit immediately. The TCP packets (SYN) never had time to leave the NIC before the OS shut down the process and potentially the network interface.
    *   **The Fix**: Added a massive volatile loop (`for... 100000000`) to keep the process alive long enough for the server to reply.

### **62. `apps/tcptest.elf`**
*   **Detailed Full Summary**:
    The executable binary for the TCP test.

### **63. `apps/tcptest.o`**
*   **Detailed Full Summary**:
    Object file.

### **64. `apps/terminal.cpp`**
*   **Detailed Full Summary**:
    This is one of the most sophisticated applications in the OS: A graphical **Terminal Emulator**.
    *   **Architecture**: It acts as a "Windowed Console". It creates a window via IPC, allocates a **Pseudo-Terminal (PTY)** master/slave pair using syscall `154`, and forks a child process (the shell `sh.elf`) attached to the slave side.
    *   **Functionality**: It reads characters from the PTY master (output from shell) and renders them using a custom bitmap font (`font8x8`). It handles basic ANSI escape codes (specifically color codes like `\x1b[1;36m`) to render colored text. It also forwards keyboard input from the GUI window back into the PTY master, which sends it to the shell's `stdin`.
    *   **Visuals**: Implements a blinking cursor and scrolling when text reaches the bottom of the window.
*   **Context (Why & How Created)**:
    Before this app, the only way to interact with the OS was via the QEMU serial debug log or the raw VGA text mode. This app brought the command line into the GUI "Desktop" era, enabling multitasking (you can run multiple terminals).
*   **Errors & Fixes**:
    *   **The "Echo" Double Type**: Typing 'a' appeared as 'aa'.
    *   **The Fix**: The shell echoes back characters, and the terminal was also drawing them locally on keypress. The fix was to *only* draw what comes from the PTY master (which includes the shell's echo), stopping the local echo in the terminal app.
    *   **The "Blocking Read" Freeze**: The GUI would freeze because `read(pty_master)` blocked waiting for output.
    *   **The Fix**: Used `ioctl` (syscall 43) `FIONBIO` to set the PTY master to Non-Blocking mode, allowing the event loop to process GUI events and PTY data simultaneously.

### **65. `apps/terminal.elf`**
*   **Detailed Full Summary**:
    The Terminal executable. This is usually the default icon on the desktop.

### **66. `apps/terminal.o`**
*   **Detailed Full Summary**:
    Object file.

### **67. `apps/test.cpp`**
*   **Detailed Full Summary**:
    A simple "Success Confirmation" app. It prints a series of enthusiastic messages ("Retro-OS Terminal v2.0 is FULLY WORKING!") to standard output. It is used primarily to demo that the `terminal` app correctly handles standard output from a child process.
*   **Context (Why & How Created)**:
    When building the Terminal Emulator, we needed a target program simpler than `sh` to verify that `execve` worked inside the PTY environment. If this app ran and we saw text in the window, we knew the PTY piping was correct.
*   **Errors & Fixes**:
    *   **None**: It's a trivial printer.

### **68. `apps/test.elf`**
*   **Detailed Full Summary**:
    Binary.

### **69. `apps/test.o`**
*   **Detailed Full Summary**:
    Object.

### **70. `apps/textview.cpp`**
*   **Detailed Full Summary**:
    A "Read-Only" version of Notepad, designed for performance. It is a text file viewer that directly accesses the **framebuffer** (via syscall 150) rather than using the slow IPC windowing system. This "Exclusive Mode" application takes over the whole screen.
    *   **Features**: Loads a file into a 64KB buffer, renders it with a white-on-grey color scheme, and implements basic scrolling logic (handled by `line` and `scroll` variables).
    *   **Syscalls**: Defines its own raw assembly wrappers for `sys_open`, `sys_read`, and `sys_get_framebuffer`, bypassing `libc` for minimized footprint.
*   **Context (Why & How Created)**:
    We needed a way to view logs or code without the overhead of the WindowServer. This proves that an app can "bypass" the Compositor if it asks the kernel for direct VRAM access (useful for games or full-screen video).
*   **Errors & Fixes**:
    *   **The "Segfault" on Large Files**: It uses a fixed `file_buffer[64 * 1024]`. Loading a file larger than 64KB (like `kernel.map`) would overflow or truncate.
    *   **The Fix**: Currently capped at 64KB. In the future, we need dynamic chunk loading.

### **71. `apps/textview.elf`**
*   **Detailed Full Summary**:
    Binary.

### **72. `apps/textview.o`**
*   **Detailed Full Summary**:
    Object.

### **73. `apps/include/crypto.h`**
*   **Detailed Full Summary**:
    A standalone, header-only cryptography library for user space. It implements **MD5**, **SHA-256**, and **Base64** algorithms from scratch with no external dependencies.
    *   **Implementation**: It contains the raw state transformation constants (K tables) and bitwise rotation macros (`ROTL32`, `SIG1`, `CH`). It provides specific structs (`sha256_ctx_t`) and API functions (`sha256_init`, `update`, `final`).
*   **Context (Why & How Created)**:
    Needed for the future "Package Manager" (verifying checksums) and "Login Manager" (hashing passwords). Since we don't have OpenSSL, we had to implement these standard algorithms manually.
*   **Errors & Fixes**:
    *   **The "Endianness" Swap**: SHA-256 requires big-endian words, while x86 is little-endian.
    *   **The Fix**: The implementation includes manual byte-swapping logic (`block[i*4] << 24 | ...`) to parse the input blocks correctly.

### **74. `apps/include/libc.h`**
*   **Detailed Full Summary**:
    The behemoth header file that defines the **C Standard Library** for the OS. It combines what would usually be split into `stdlib.h`, `unistd.h`, `string.h`, `ctype.h`, `math.h` (integer only), `time.h`, `errno.h`, and `assert.h`.
    *   **Key Components**:
        *   **Memory**: `malloc`, `free`.
        *   **String**: `strcpy`, `strtok`, `strstr`, `memcpy`, `int-to-string`.
        *   **Math**: `qsort`, `bsearch`, `rand` (stubs), `abs`.
        *   **Time**: `struct tm`, `strftime`, `ctime`.
*   **Context (Why & How Created)**:
    To port any existing C code (like the `nano` editor or simple games), we need a POSIX-compliant environment. This file provides that compatibility layer, even if the underlying implementation is just a wrapper around our custom `int 0x80` syscalls. It is "Header-Only" (mostly static inline) to avoid the complexity of building a shared `libc.so` library.
*   **Errors & Fixes**:
    *   **The "Dependency Loop"**: `malloc` depends on `sbrk`, `printf` depends on `malloc`.
    *   **The Fix**: Carefully ordered definitions.
    *   **Missing `start`**: `qsort` was missing.
    *   **The Fix**: Implemented a generic `qsort` using the Hoare partition scheme directly in the header.

### **75. `apps/include/os_api.h`**
*   **Detailed Full Summary**:
    This header defines the **C++ bindings** for the `minimal_os_api.cpp` framework. It allows C++ applications to use high-level abstractions like `std::string` and `std::vector<FileEntry>` when interacting with the OS. It declares the `spawn_process`, `os_fs_readdir`, and graphical `gfx_` helper functions.
*   **Context (Why & How Created)**:
    While `libc.h` provides the C-style "Raw" interface (pointers, buffers), `os_api.h` provides the "Modern" interface. It was created to make writing apps like `contracts.cpp` and `notepad.cpp` easier and safer by using RAII and containers.

### **76. `apps/include/regex.h`**
*   **Detailed Full Summary**:
    A lightweight Regular Expression engine implemented from scratch in a single header file. It supports standard metacharacters like `.` (any char), `*` (zero or more), `+` (one or more), `?` (zero or one), `^` (start), `$` (end), and `[]` (classes like `[a-z]`).
    *   **Core Logic**: Uses recursive functions (`regex_match_here`, `regex_match_star`) to perform backtracking matching.
    *   **Features**: Includes `regex_replace` and a `glob_match` function for shell wildcards (like `*.txt`).
*   **Context (Why & How Created)**:
    Needed for the Shell (wildcard expansion) and tools like `grep` (if/when implemented). We couldn't link a massive library like PCRE, so we wrote this compact implementation (~190 lines).
*   **Errors & Fixes**:
    *   **The "Infinite Loop"**: `regex_match_star` initially looped forever if the pattern was `a*` and the text was `aaaa...`.
    *   **The Fix**: Ensured the recursion consumes at least one character or breaks correctly.

### **77. `apps/include/stdio.h`**
*   **Detailed Full Summary**:
    A complete implementation of the C Standard I/O library. It defines the `FILE` structure, which buffers reads/writes to minimize syscall overhead.
    *   **Buffering**: Implements `_IONBF` (No Buffer), `_IOLBF` (Line Buffer), and `_IOFBF` (Full Buffer).
    *   **Functions**: `fopen`, `fclose`, `fread`, `fwrite`, `fprintf`, `sprintf`, `fgets`.
    *   **Integration**: It wraps the raw `syscall_open`, `syscall_read` etc., providing the familiar stream abstraction.
*   **Context (Why & How Created)**:
    Porting existing C apps is impossible without `stdio.h`. This file bridges the gap between the raw kernel API (file descriptors) and high-level buffered I/O.
*   **Unique Detail**: `stdin`, `stdout`, and `stderr` are statically initialized `FILE` structs pointing to FDs 0, 1, and 2.

### **78. `apps/include/stdlib_ext.h`**
*   **Detailed Full Summary**:
    An extension to `libc.h` that implements more complex subsystems:
    *   **Heap Allocator**: A `malloc`/`free` implementation using a stored linked list of `heap_block_t` headers. It coalesces free blocks to prevent fragmentation and calls `sbrk` when it runs out of memory.
    *   **Data Structures**: `vector_t` (dynamic array), `hash_table_t`, and `string_builder_t`.
    *   **Process Control**: `system()`, `atexit()`.
*   **Context (Why & How Created)**:
    We needed dynamic memory management for the GUI (windows, buffers). Writing a custom `malloc` is a rite of passage in OS dev. This implementation is a "First Fit" allocator.
*   **Errors & Fixes**:
    *   **The "Alignment" Crash**: Early versions returned pointers that weren't 8-byte aligned, causing crashes when accessing `double` or `int64_t`.
    *   **The Fix**: Added `size = (size + 7) & ~7;` to enforce alignment.

### **79. `apps/include/string`**
*   **Detailed Full Summary**:
    A C++ standard library adapter. It aliases `Std::String` to `std::string` within the `std` namespace. This allows us to use standard C++ syntax (like `std::string s = "hello";`) while actually using our custom `UserLib` implementation behind the scenes.
*   **Context**:
    To compile modern C++ code without the GNU `libstdc++` (which is huge and hard to port), we provide this "fake" header that redirects to our lightweight implementation.

### **80. `apps/include/syscall.h`**
*   **Detailed Full Summary**:
    The **Master System Call Table**. It defines the numeric ID for every system call (e.g., `#define SYS_FORK 9`, `#define SYS_SOCKET 14`). It also defines the C-style structs required by these calls (`stat`, `utsname`, `procinfo`) and provides inline assembly wrappers (`int 0x80`) for invoking them.
*   **Context (Why & How Created)**:
    This is the "Contract" between Kernel and User. If a number changes here, it must change in `src/kernel/syscall_handler.cpp`. It covers everything from File I/O to Pthreads and Network Sockets.
*   **Errors & Fixes**:
    *   **The "Arg Order" Bug**: `mmap` has 6 arguments. The default `int 0x80` wrapper only handled up to 5 registers (EBX, ECX, EDX, ESI, EDI).
    *   **The Fix**: For syscalls with 6 args, we had to use `EBP` as the 6th argument, which required careful stack push/pop in the assembly wrapper.

### **81. `apps/include/types.h`**
*   **Detailed Full Summary**:
    Defines standard checking POSIX types (`pid_t`, `size_t`, `time_t`) and fundamental structures (`dirent`, `stat`, `timespec`).
*   **Context**:
    Split off from `libc.h` to avoid circular inclusion dependencies. It ensures that `stat` struct layout matches exactly what the kernel writes.

### **82. `apps/include/userlib.h`**
*   **Detailed Full Summary**:
    A utility library containing common algorithms that don't fit in `libc` or need C++ features.
    *   **String**: `strrchr`, `memmove`, `strdup`.
    *   **Math**: `crc32` (checksum), `rand` (LCG algorithm), `atoi`.
    *   **Time Helpers**: `day_of_week`, `days_in_month` (for the Clock app).
*   **Context**:
    Provides the building blocks for apps. The `crc32` implementation uses a pre-calculated table for speed.

### **83. `apps/include/vector`**
*   **Detailed Full Summary**:
    Similar to `string`, this is a C++ adapter header. It works to alias `Std::Vector` to `std::vector`, enabling the use of dynamic arrays in user applications without the heavy STL.

### **84. `apps/include/os/ipc.hpp`**
*   **Detailed Full Summary**:
    The C++ wrapper for the **Windowing System IPC Protocol**.
    *   **Class**: `IPCClient`.
    *   **Protocol**: Defines the packet structures (`msg_gfx_create_window_t`, `msg_gfx_mouse_event_t`) used to communicate with the Window Server via a Unix Domain Socket (`/tmp/ws.sock`).
    *   **Methods**: `connect()`, `create_window()`, `fill_rect()`, `put_pixel()`.
*   **Context (Why & How Created)**:
    Raw socket programming is tedious. This class encapsulates the handshake logic (connect -> send create -> wait for shared memory ID -> map framebuffer). It is the backbone of all GUI apps (`hello`, `notepad`).

### **85. `apps/include/os/string.hpp`**
*   **Detailed Full Summary**:
    A namespace-wrapped version of string functions (`OS::String::strcpy`) to avoid polluting the global namespace in C++ apps that might also include standard headers.

### **86. `apps/include/os/syscalls.hpp`**
*   **Detailed Full Summary**:
    The C++ Namespace wrapper (`OS::Syscall::*`) for the raw C syscalls. It provides a cleaner API for C++ apps (`apps/noptepad.cpp` uses this). It mirrors `syscall.h` but uses `constexpr` and `inline` functions within the namespace.
*   **Context**:
    It simply calls the underlying assembly wrappers but provides type safety and namespace isolation.

### **87. `src/boot/boot.asm`**
*   **Detailed Full Summary**:
    The **Stage 1 Bootloader**. It is exactly 512 bytes and sits in the first sector of the hard drive.
    *   **FAT16 Header**: The first ~60 bytes contain the BIOS Parameter Block (BPB) configured for FAT16 security (e.g., `OEMName`, `BytesPerSec`). This makes the `os.img` mountable as a valid drive in Windows/Linux.
    *   **Operation**:
        1.  Sets up the stack at `0x7C00`.
        2.  Prints a "Real Mode" message using BIOS interrupts.
        3.  Calls `load_kernel` to read the kernel from the disk.
        4.  Switches to 32-bit Protected Mode (A20 line, GDT).
        5.  Jumps to `KERNEL_OFFSET` (0x8000).
*   **Context (Why & How Created)**:
    We wrote this in assembly because C++ cannot run on bare metal without a stack or initialized memory. It is the bridge from BIOS to the Kernel.
*   **Errors & Fixes**:
    *   **The "USB Boot" Failure**: On real hardware (USB), `DL` (Drive Number) wasn't preserved.
    *   **The Fix**: Saved `DL` to `[BOOT_DRIVE]` immediately upon entry.

### **88. `src/boot/boot.bin`**
*   **Detailed Full Summary**:
    The compiled binary of `boot.asm`. It is concatenated with `kernel.bin` to form the final `os.img`.

### **89. `src/boot/disk.asm`**
*   **Detailed Full Summary**:
    A BIOS Int 13h helper routine for reading sectors from the disk.
    *   **Logic**: Translates LBA (logical block) parameters or simply takes `CHS` (Cylinder-Head-Sector) arguments to load data into memory at `ES:BX`.
    *   **Error Handling**: If `jc` (Jump if Carry) triggers, it prints "Disk Error!" and hangs.
*   **Context**:
    Included by `boot.asm`. It uses the older 16-bit `int 0x13, ah=0x02` interface, limiting us to <8GB drives (fine for this OS).

### **90. `src/boot/gdt.asm`**
*   **Detailed Full Summary**:
    Defines the **Global Descriptor Table (GDT)**. In Protected Mode, you don't use raw segments (like `0x8000`); you use "Selectors" that point to descriptors.
    *   **Descriptors**:
        1.  `Null`: Required by x86.
        2.  `Code`: `0x9A` access byte (Execute/Read, Ring 0).
        3.  `Data`: `0x92` access byte (Read/Write, Ring 0).
    *   **Layout**: `Flat Memory Model`. Both Code and Data base addresses are `0x0` and limit is `4GB`, effectively disabling segmentation so we can use paging.
*   **Context**:
    Essential for switching to 32-bit mode. If this is wrong, the CPU triple-faults instantly.

### **91. `src/boot/kernel_entry.asm`**
*   **Detailed Full Summary**:
    The **Stage 2 Entry Point**. The bootloader jumps here (`0x8000`), but this file's job is to enable **Paging** and jump to the **Higher Half** kernel (`0xC0000000`).
    *   **Paging Setup**: It creates a temporary Page Directory (`BootPageDirectory`) and maps the first 128MB of physical RAM to *both* `0x00000000` (identity) and `0xC0000000` (virtual).
    *   **Mode Switch**: Sets bit 31 of `CR0` to enable paging using the temporary table.
    *   **Handoff**: Calls `main` (the C++ `kmain`).
*   **Context**:
    This is the "Trampoline". We need `kernel.cpp` to think it's running at high addresses (for security/stability), but it's physically loaded low. This Assembly file performs that magic trick.

### **92. `src/boot/kernel_entry.o`**
*   **Detailed Full Summary**:
    The object file used during linking.

### **93. `src/boot/print.asm`**
*   **Detailed Full Summary**:
    A 16-bit Real Mode printing routine. It uses BIOS Interrupt `0x10`, Teletype Output function (`AH=0x0E`).
    *   **Logic**: Loops through a null-terminated string at `BX`, printing each char and incrementing.
*   **Context**:
    Only usable *before* we switch to 32-bit mode. Used for debug messages like "Loading kernel...".

### **94. `src/boot/print_pm.asm`**
*   **Detailed Full Summary**:
    A 32-bit Protected Mode printing routine. Since BIOS interrupts are gone in 32-bit mode, this writes directly to the **VGA Video Memory** at `0xB8000`.
    *   **Logic**: It assumes `White on Black` (0x0F) and standard 80x25 text mode. It writes `[Char][Attr][Char][Attr]` to memory.
*   **Context**:
    Used to print "Successfully landed in 32-bit Protected Mode" before the kernel takes over.

### **95. `src/boot/switch_pm.asm`**
*   **Detailed Full Summary**:
    The logic to perform the mode switch:
    1.  `cli`: Disable interrupts (BIOS IVT is invalid in 32-bit).
    2.  `lgdt`: Load the GDT descriptor.
    3.  `mov cr0, eax`: Set bit 0 (PE) of Control Register 0.
    4.  `jmp CODE_SEG:init_pm`: A "Far Jump" to flush the CPU's instruction pipeline.
    5.  `init_pm`: Update all segment registers (`DS`, `SS`, `ES`) to point to the new Data Selector.
*   **Context**:
    This is the "Point of No Return". Once executed, we are in 32-bit mode.

### **96. `src/drivers/acpi.cpp`**
*   **Detailed Full Summary**:
    This file implements the **Advanced Configuration and Power Interface (ACPI)** parser. It scans the BIOS memory area (0xE0000 - 0xFFFFF) to find the **RSDP** (Root System Description Pointer). From there, it locates the **RSDT** (Root System Description Table), which contains pointers to all other ACPI tables.
    *   **Functions**: `acpi_init`, `acpi_find_table`.
    *   **Mapping**: Uses `acpi_map_region` to ensure the physical addresses of the ACPI tables are mapped into the virtual address space so the kernel can read them.
*   **Context (Why & How Created)**:
    Modern hardware (esp. multicore CPUs and Timers) is described via ACPI, not fixed BIOS addresses. We needed this to find the **HPET** (High Precision Event Timer) and **MADT** (Multiple APIC Description Table) to support advanced interrupt handling.
*   **Errors & Fixes**:
    *   **The "Checksum" Fail**: Initially, we blindly trusted the pointer.
    *   **The Fix**: Added signature validation ("RSD PTR ", "APIC", "HPET").

### **97. `src/drivers/acpi.h`**
*   **Detailed Full Summary**:
    Defines the packed structs for ACPI tables: `acpi_rsdp_t`, `acpi_rsdt_t`, `acpi_madt_t` (for APIC), and `acpi_hpet_t`. It ensures `__attribute__((packed))` is used so the compiler doesn't insert padding bytes between fields, which would break the hardware structure layout.

### **98. `src/drivers/ata.cpp`**
*   **Detailed Full Summary**:
    The **Hard Disk Driver** (Parallel ATA / PATA / IDE). It uses Programmable I/O (PIO) Mode.
    *   **Logic**:
        1.  Waits for BSY (Busy) flag to clear.
        2.  Selects Drive (Master/Slave) and Head.
        3.  Sends Sector Count and LBA (Logical Block Address) to ports `0x1F2`-`0x1F5`.
        4.  Sends `READ` (0x20) or `WRITE` (0x30) command to `0x1F7`.
        5.  Loops to read/write 256 words (512 bytes) from the Data Port `0x1F0`.
*   **Context**:
    This is the only storage driver. Without it, we can't load files from the disk image.
*   **Errors & Fixes**:
    *   **The "Race Condition"**: Reading immediately after sending a command caused garbage data.
    *   **The Fix**: Added `ata_wait_bsy()` and `ata_wait_drq()` (Data Request) checks to respect the hardware timing.

### **99. `src/drivers/ata.h`**
*   **Detailed Full Summary**:
    Defines the I/O Ports (`0x1F0`-`0x1F7`) and Status commands for the ATA controller.

### **100. `src/drivers/bga.cpp`**
*   **Detailed Full Summary**:
    Driver for the **Bochs Graphics Adapter (BGA)**. This is a virtual GPU provided by QEMU/VirtualBox/Bochs that exposes high-resolution modes (like 1024x768x32) via simple I/O ports, bypassing the complex VESA VBE BIOS calls.
    *   **Logic**: Writes index/data pairs to ports `0x01CE` and `0x01CF` to set X resolution, Y resolution, Bit Depth, and Enable bit.
*   **Context**:
    This is the secret behind the "High Resolution" GUI. Real hardware would need a VESA driver, but BGA is perfect for emulation development.

### **101. `src/drivers/bga.h`**
*   **Detailed Full Summary**:
    Defines the BGA register indices (`VBE_DISPI_INDEX_XRES`), values, and I/O ports.

### **102. `src/drivers/bmp.cpp`**
*   **Detailed Full Summary**:
    A bare-bones **BMP Image Decoder**. It parses a raw byte array containing a Windows Bitmap file.
    *   **Support**: 24-bit (RGB) and 32-bit (RGBA) uncompressed bitmaps.
    *   **Logic**: Reads `bmp_file_header` and `bmp_info_header` to get width/height. Iterates through the pixel array, handling row padding (rows must align to 4 bytes). It performs the necessary BGR -> RGB conversion (since BMP stores Blue first).
*   **Context**:
    Used to verify the graphics system by rendering a test image (the "Inject Wallpaper" result).
*   **Errors & Fixes**:
    *   **The "Upside Down" Bug**: BMPs are stored bottom-to-top.
    *   **The Fix**: The loop iterates `y` from `height-1` down to `0`.

### **103. `src/drivers/bmp.h`**
*   **Detailed Full Summary**:
    Defines the standard `BITMAPFILEHEADER` and `BITMAPINFOHEADER` structures used by Windows.

### **104. `src/drivers/devfs.cpp`**
*   **Detailed Full Summary**:
    A **Virtual Filesystem** that exposes hardware devices as files (Unix philosophy).
    *   **Nodes**:
        *   `/dev/null`: Discards writes, returns EOF on read.
        *   `/dev/zero`: Discards writes, returns zeros on read.
        *   `/dev/tty`: Proxies reads/writes to the active serial console.
    *   **Logic**: It implements the VFS function pointers (`read`, `write`, `open`) but instead of accessing a disk, it runs C code.
*   **Context**:
    Essential for porting `libc` functions that check for these devices.

### **105. `src/drivers/fat16.cpp`**
*   **Detailed Full Summary**:
    The heavyweight **Filesystem Driver**. It implements Read/Write/Create/Delete for FAT16.
    *   **Initialization**: Reads the BPB to calculate offsets for the FAT table and Root Directory.
    *   **Reading**: Follows the cluster chain in the FAT to reassemble fragmented files into a contiguous buffer.
    *   **Writing**: Finds free sectors, updates the FAT chain, and writes data to disk.
    *   **VFS Integration**: Converts internal FAT entries into generic `vfs_node_t` structures so the kernel can treat them uniformly.
*   **Context**:
    The core of the OS storage. It supports Long Filenames (LFN) partially (reads them, but creation is limited to 8.3 internally for now).
*   **Errors & Fixes**:
    *   **The "Dir Entry Update" Bug**: Updating a file size didn't persist because we weren't writing the modified directory entry back to disk.
    *   **The Fix**: Added logic in `fat16_write_file` to seek back to the directory sector and update `entry->file_size`.

### **106. `src/drivers/fat16.h`**
*   **Detailed Full Summary**:
    Defines FAT16 on-disk structures (`BPB`, `Directory Entry`) and attributes (`READ_ONLY`, `HIDDEN`, `DIRECTORY`).

### **107. `src/drivers/graphics.cpp`**
*   **Detailed Full Summary**:
    The **Graphics Subsystem Core**. It manages the Framebuffer.
    *   **Double Buffering**: Allocates a `back_buffer` in RAM. All drawing happens here first. `swap_buffers()` copies it to the video memory (`screen_buffer`). This prevents flickering.
    *   **Primitives**: Implements `put_pixel`, `draw_rect`, and `draw_line` (using Bresenham's algorithm).
*   **Context**:
    Abstracts the BGA hardware. The window server sits on top of this.

### **108. `src/drivers/graphics.h`**
*   **Detailed Full Summary**:
    Header for graphics primitives and color definitions (`COLOR_BLACK`, `COLOR_TEAL` etc.).

### **109. `src/drivers/hpet.cpp`**
*   **Detailed Full Summary**:
    Driver for the **High Precision Event Timer**.
    *   **Logic**: Finds the HPET base address via ACPI, enables it, and provides a nanosecond-precision `hpet_read_counter()`.
*   **Context**:
    The legacy PIT (Programmable Interval Timer) is too slow/imprecise for modern smooth GUI animations. HPET is the modern replacement.

### **110. `src/drivers/hpet.h`**
*   **Detailed Full Summary**:
    Defines HPET register offsets (`CAPABILITIES`, `CONFIGURATION`, `MAIN_COUNTER`).

### **111. `src/drivers/keyboard.cpp`**
*   **Detailed Full Summary**:
    The **PS/2 Keyboard Driver**. It processes Scancode Set 1 sent by the keyboard controller.
    *   **Interrupt**: Registers an handler on IRQ1 (INT 33).
    *   **Mapping**: Maintains a lookup table (`kbd_us`, `kbd_us_shifted`) to convert raw scancodes to ASCII characters.
    *   **State**: Tracks Shift, Ctrl, Alt, and Caps Lock states.
    *   **Special Features**: Detects `Ctrl+C` and sends a `SIGINT` signal to the current process, killing it or triggering a handler.
*   **Context**:
    The primary input method. The `keyboard_callback` is called *asynchronously* on every key press and release.

### **112. `src/drivers/keyboard.h`**
*   **Detailed Full Summary**:
    Exposes `init_keyboard()`.

### **113. `src/drivers/mouse.cpp`**
*   **Detailed Full Summary**:
    The **PS/2 Mouse Driver**. It initializes the mouse by sending commands to the `0x64` controller port to enable the Auxiliary Device (IRQ12).
    *   **Protocol**: Reads standard 3-byte PS/2 packets:
        1.  Byte 0: Status (buttons, overflow, sign bits).
        2.  Byte 1: X movement.
        3.  Byte 2: Y movement.
    *   **Tracking**: Updates global variables `mouse_x` and `mouse_y`, clamping them to screen bounds.
*   **Context**:
    Essential for the GUI. Note that the Y-axis is inverted (standard mouse behavior vs screen coordinates).

### **114. `src/drivers/mouse.h`**
*   **Detailed Full Summary**:
    Exposes `init_mouse()`.

### **115. `src/drivers/pci.cpp`**
*   **Detailed Full Summary**:
    The **PCI Bus Enumerator**. It scans the PCI bus to find connected devices.
    *   **Mechanism**: Uses standard Configuration Mechanism #1 (Ports `0xCF8` Address, `0xCFC` Data).
    *   **Scanning**: Iterates 256 buses * 32 slots * 8 functions. Reads Vendor ID and Device ID.
    *   **BGA Detection**: Specifically looks for `Vendor=0x1234, Device=0x1111` (QEMU VGA) to find its BAR0 (Base Address Register), which tells us the physical address of the BGA Framebuffer.
*   **Context**:
    Without this, we wouldn't know where the high-res video memory is located (it changes depending on RAM size/VM settings).

### **116. `src/drivers/pci.h`**
*   **Detailed Full Summary**:
    Defines helper functions like `pci_read_config`.

### **117. `src/drivers/rtc.cpp`**
*   **Detailed Full Summary**:
    Driver for the **Real Time Clock (CMOS)**.
    *   **Logic**: Reads from ports `0x70`/`0x71`. Accesses registers 0 (Seconds), 2 (Minutes), 4 (Hours), 7 (Day), 8 (Month), 9 (Year).
    *   **Format**: Handles BCD (Binary Coded Decimal) conversion, as most BIOSes store "23" as `0x23` (binary `0010 0011`).
*   **Context**:
    Provides the system wall-clock time (`gettimeofday`).

### **118. `src/drivers/rtc.h`**
*   **Detailed Full Summary**:
    Defines `rtc_read`.

### **119. `src/drivers/serial.cpp`**
*   **Detailed Full Summary**:
    Driver for the **COM1 Serial Port** (`0x3F8`).
    *   **Initialization**: Sets Baud Rate to 38400 (Divisor 3), 8N1 (8 bits, No parity, 1 stop). Enables FIFO.
    *   **Logging**: Provides `serial_log` and `serial_log_hex`.
*   **Context**:
    This is the **Most Critical Debugging Tool**. Since the screen might be broken, garbled, or in a different mode, the OS writes all debug info to the textual serial port, which we view in the terminal/file logs.

### **120. `src/drivers/serial.h`**
*   **Detailed Full Summary**:
    Exposes `serial_log`.

### **121. `src/drivers/timer.cpp`**
*   **Detailed Full Summary**:
    Driver for the **Programmable Interval Timer (PIT)**.
    *   **Frequency**: Sets Channel 0 to fire interrupts at a specific frequency (e.g., 100Hz or 1000Hz).
    *   **Scheduler Integration**: The callback increments a global `tick` counter and calls `schedule()`, enabling **Preemptive Multitasking**.
*   **Context**:
    The system heartbeat. Without this, a process running `while(1)` would freeze the OS forever.

### **122. `src/drivers/timer.h`**
*   **Detailed Full Summary**:
    Exposes `init_timer`.

### **123. `src/drivers/vga.cpp`**
*   **Detailed Full Summary**:
    Driver for the legacy **VGA Text Mode** (`0xB8000`).
    *   **Functionality**: `vga_print`, `vga_clear_screen`. Handles scrolling when the cursor hits the bottom.
*   **Context**:
    This is the "Fallback" driver. It is used during early boot before the GUI starts, or if BGA initialization fails. It prints to the 80x25 BIOS screen.

### **124. `src/drivers/vga.h`**
*   **Detailed Full Summary**:
    Defines VGA colors and dimensions.

### **125. `src/include/.gitignore`**
*   **Detailed Full Summary**:
    Allows developers to work within the include directory without committing temporary files. It specifically ignores `Makefile` (since this folder is headers-only), Visual Studio files (`*.sln`, `*.vcxproj`), and mbedTLS configuration artifacts.

### **126. `src/include/CMakeLists.txt`**
*   **Detailed Full Summary**:
    A CMake build file used for the **porting context**, specifically for mbedTLS.
    *   **Logic**: It installs mbedTLS and PSA headers into the standard include paths.
    *   **Conditional**: Includes an option `INSTALL_MBEDTLS_HEADERS` which defaults to ON.
    *   **Integration**: Used when compiling user-space libraries that depend on the OS headers.

### **127. `src/include/Contracts.h`**
*   **Detailed Full Summary**:
    A philosophical high-level "System Contract" file. It defines the "Laws of the OS" using ASSERT macros.
    *   **Rules**: "Filesystem is the ONLY persistent storage", "Processes are isolated".
    *   **Implementation**: Defines `CONTRACT_ASSERT(expr, msg)` which triggers a `kernel_panic` if violated.
    *   **Enforcement**: Contains validation helper functions for Paths (must be absolute), PIDs (must be >= 0), and IPC.
*   **Context**:
    This is an architectural document expressed in code. It prevents "abstraction leaks" where developers might be tempted to hack bypassing the VFS or Process isolation.

### **128. `src/include/KernelInterfaces.h`**
*   **Detailed Full Summary**:
    A strict **ABI (Application Binary Interface)** definition. It defines the exact C structures passed between the Kernel and Userspace.
    *   **Structures**: `KProcessInfo`, `KDirEntry`, `KInputEvent`, `KIPCMessage`.
    *   **Versioning**: Each struct has a `KStructHeader` with a `version` field. This allows the kernel to upgrade without breaking old binaries (backwards compatibility).
    *   **Interfaces**: `sys_spawn_process`, `sys_ipc_send`, `sys_surface_create`.
*   **Context**:
    This is the "Stage 2" hardening. It replaces loose syscall arguments with structured, versioned opaque handles.

### **129. `src/include/assert.h`**
*   **Detailed Full Summary**:
    A minimal C standard library `assert.h`. Currently, it defines `assert(x)` as `((void)0)` (no-op) to allow code to compile without aborting.

### **130. `src/include/browser.h`**
*   **Detailed Full Summary**:
    The Public API for the **Browser Subsystem**.
    *   **API**: `init()`, `draw(Window*)`, `click()`, `key()`, `navigate(url)`.
    *   **Integration**: Decouples the Browser logic from the Window Manager. The `Window*` struct comes from `gui_common.h`.

### **131. `src/include/css_dom.h`**
*   **Detailed Full Summary**:
    Data structures for the **CSS Object Model (CSSOM)**.
    *   **Structures**:
        *   `CSSValue`: Represents color, number, string, etc.
        *   `CSSDeclaration`: A property-value pair (e.g., `color: red`).
        *   `CSSSelector`: Represents `#id`, `.class`, or `div > p`.
        *   `CSSRule`: Base class for Style Rules, Media Queries, etc.
        *   `CSSStyleSheet`: A collection of rules.
    *   **Memory**: Uses custom pointers (`next`) for linked lists, as we don't have `std::vector` here.

### **132. `src/include/css_engine.h`**
*   **Detailed Full Summary**:
    The **Style Resolver**. It calculates which CSS rules apply to which HTML element.
    *   **Functions**:
        *   `matches(Selector, Node)`: Returns true if the selector matches the DOM node.
        *   `apply_styles(Sheet, Node)`: Modifies the Node's computed style based on the sheet.
    *   **Logic**: Checks IDs, Classes, and Tag names. Handle complex selectors is a "TODO".

### **133. `src/include/css_parser.h`**
*   **Detailed Full Summary**:
    Header for the **CSS Parser**.
    *   **Class**: `CSSParser`.
    *   **Methods**: `parse(const char* css)` returns a `CSSStyleSheet*`.
    *   **Internal**: Uses `CSSTokenizer` to consume tokens.

### **134. `src/include/css_tokenizer.h`**
*   **Detailed Full Summary**:
    Lexer for CSS.
    *   **Tokens**: `IDENT` (div), `HASH` (#id), `AT_KEYWORD` (@media), `DELIM` (:), `STRING`, `NUMBER`.
    *   **Class**: `CSSTokenizer`.
    *   **Logic**: Scans the input string char-by-char to produce tokens for the parser.

### **135. `src/include/ctype.h`**
*   **Detailed Full Summary**:
    Standard C library character classification.
    *   **Functions**: `isalpha`, `isdigit`, `isalnum`, `isspace` (inc. \t, \n, \r), `tolower`, `toupper`.
    *   **Implementation**: Done as `static inline` functions for performance and simplicity (no external lib dependency).

### **136. `src/include/dirent.h`**
*   **Detailed Full Summary**:
    Standard C library Directory interface.
    *   **Structs**: `DIR`, `struct dirent` (with `d_name`, `d_ino`, `d_type`).
    *   **Functions**: `opendir`, `readdir`, `closedir`.
    *   **Constants**: `DT_DIR`, `DT_REG` etc. for checking file types.
*   **Context**:
    Maps the POSIX directory API to our internal `sys_getdents` system call.

### **137. `src/include/elf.h`**
*   **Detailed Full Summary**:
    Defines the **Executable and Linkable Format (ELF)** structures.
    *   **Magic**: `0x7F 'E' 'L' 'F'`.
    *   **Structs**: `Elf32_Ehdr` (File Header), `Elf32_Phdr` (Program Header).
    *   **Fields**: `e_entry` (Entry point address), `p_vaddr` (Virtual address to load segment), `p_filesz`, `p_memsz`.
*   **Context**:
    Used by the Kernel (`execve`) to parse and load user programs. We only support 32-bit ELF.

### **138. `src/include/errno.h`**
*   **Detailed Full Summary**:
    Standard C library Error Codes.
    *   **Codes**: `ENOENT` (No such file), `EPERM` (Permission denied), `ENOMEM`, `EINVAL`, `EAGAIN`.
*   **Context**:
    Used by syscalls to return error states (negative values) which wrappers convert to the global `errno` variable (though strictly `errno` requires thread-local storage, which we mimic).

### **139. `src/include/fcntl.h`**
*   **Detailed Full Summary**:
    Standard C networking/file control options.
    *   **Flags**: `O_RDONLY`, `O_CREAT`, `O_TRUNC`, `O_APPEND`.
    *   **Mode Bits**: `S_IFDIR`, `S_IFREG`, `S_IRWXU` (User Read/Write/Exec).
    *   **Functions**: `open`, `creat`, `fcntl`.

### **140. `src/include/font.h`**
*   **Detailed Full Summary**:
    A raw, hardcoded **8x8 Pixel Bitmap Font**.
    *   **Data**: `static const uint8_t font8x8_basic[128][8]`.
    *   **Format**: Each character is 8 bytes. Each byte represents a row of 8 pixels.
    *   **Content**: Covers basic ASCII (0x20 to 0x7F).
*   **Context**:
    This is the system font. We didn't implement TrueType parsing yet, so every text label, terminal char, and window title is drawn by iterating these bits.

### **141. `src/include/fs_phase.h`**
*   **Detailed Full Summary**:
    Header for a **Phase-based Filesystem Implementation**.
    *   **Phase A**: Core allocation (inodes/blocks).
    *   **Phase B**: Command integration (`cd`, `ls`).
    *   **Phase C**: Context menus and GUI actions.
    *   **Structs**: `phase_inode`, `phase_dir_entry`.
*   **Context**:
    This seems to be part of a structured development plan or a "Layered FS" architecture where different features were rolled out in stages. `phase_vfs_sync` suggests an in-memory FS that flushes to disk.

### **142. `src/include/gui_common.h`**
*   **Detailed Full Summary**:
    Shared definitions for the **Window Manager** and **GUI Toolkit**.
    *   **Structures**: `Window`, `Theme`, `Animation`.
    *   **Callbacks**: `DrawFn`, `ClickFn`, `KeyFn`.
    *   **Namespaces**:
        *   `FB`: Low-level framebuffer primitives (`rect`, `put`).
        *   `FontSystem`: Font rendering.
        *   `IconSystem`: Icon rendering.
        *   `UI`: Widgets (`button`, `list_item`).
    *   **IPC**: `ContextAction` enums for right-click menus.
*   **Context**:
    This header is the "glue" that binds applications (like the browser or terminal) to the windowing system.

### **143. `src/include/html_dom.h`**
*   **Detailed Full Summary**:
    Data structures for the **DOM (Document Object Model)**.
    *   **Tree Structure**: `Node` class with `parent`, `first_child`, `next_sibling` pointers.
    *   **Types**: `ElementNode` (tags), `TextNode` (strings), `CommentNode`, `DocumentNode`.
    *   **Attributes**: Simple key-value storage for `<div id="foo">`.
*   **Context**:
    The in-memory representation of a parsed HTML page.

### **144. `src/include/html_parser.h`**
*   **Detailed Full Summary**:
    Header for the **HTML5 Parser**.
    *   **Class**: `HTML5Parser`.
    *   **Logic**: Implements the "Insertion Mode" state machine defined by the HTML5 spec (e.g., `BEFORE_HEAD`, `IN_BODY`).
    *   **Stack**: Maintains a stack of open elements (`open_elements`) to handle nesting and auto-closing tags.

### **145. `src/include/html_tokenizer.h`**
*   **Detailed Full Summary**:
    The Tokenizer for HTML.
    *   **States**: `TAG_OPEN`, `ATTRIBUTE_NAME`, `DATA` etc.
    *   **Tokens**: `START_TAG`, `END_TAG`, `CHARACTER`, `DOCTYPE`.
*   **Context**:
    Breaks the raw HTML string into semantic tokens for the parser.

### **146. `src/include/http_parser.h`**
*   **Detailed Full Summary**:
    A single-header HTTP parser (looks like a port of the famous `nodejs/http-parser`).
    *   **Features**: Parses requests/responses, headers, chunks, URLs.
    *   **Callbacks**: `on_url`, `on_header_field`, `on_body`.
    *   **Performance**: Highly optimized C code, zero-copy philosophy.
*   **Context**:
    Used by the networking stack (presumably `curl` or the browser) to process HTTP traffic.

### **147. `src/include/idt.h`**
*   **Detailed Full Summary**:
    Defines the **Interrupt Descriptor Table (IDT)**.
    *   **Structs**: `idt_gate_t` (the 8-byte entry), `idt_register_t` (for `lidt`).
    *   **Functions**: `set_idt_gate` (install a handler).
*   **Context**:
    The CPU uses this table to know which code to run when an interrupt (IRQ) or exception happens.

### **148. `src/include/io.h`**
*   **Detailed Full Summary**:
    Inline assembly wrappers for x86 I/O Ports.
    *   **Functions**: `outb`, `inb`, `outw`, `inw`, `outl`, `inl` (Byte/Word/Long).
    *   **Interrupts**: `cli` (Clear Interrupts), `sti` (Set Interrupts).
*   **Context**:
    Essential for checking hardware (drivers).

### **149. `src/include/irq.h`**
*   **Detailed Full Summary**:
    High-level Interrupt Request (IRQ) management.
    *   **Functions**: `irq_install` (remaps PIC, installs handlers).
    *   **Stubs**: Extern declarations for assembly ISR stubs (`isr32` to `isr47`).

### **150. `src/include/isr.h`**
*   **Detailed Full Summary**:
    Interrupt Service Routine (ISR) infrastructure.
    *   **Types**: `registers_t` (snapshot of CPU registers pushed by assembly).
    *   **Handlers**: `register_interrupt_handler` allows C code to subscribe to interrupts.
    *   **Table**: `external isr_t interrupt_handlers[256]`.

### **151. `src/include/kernel_fs.h`**
*   **Detailed Full Summary**:
    Public API for the Kernel's internal filesystem module.
    *   **Instance**: `kernel_fs` (the driver struct).
    *   **Init**: `kernel_fs_init()`.

### **152. `src/include/kernel_fs_internal.h`**
*   **Detailed Full Summary**:
    Internal defines for `kernel_fs`.
    *   **Limits**: `MAX_INODES 4096`.
    *   **Structures**: `inode`, `dir_entry`, `superblock`.
    *   **Functions**: Low-level `alloc_block`, `free_block`.
*   **Context**:
    Exposes the raw block/inode logic to the higher-level FS phases.

### **153. `src/include/kernel_fs_phase3.h`**
*   **Detailed Full Summary**:
    "Phase 3" of the FS: **Security & Maintainability**.
    *   **Journaling**: `journal_entry` struct for crash recovery.
    *   **Users**: `user` struct and permissions logic (`fs_create_secure`).
    *   **History**: Undo/Redo functionality implemented at the FS level (unusual but cool).

### **154. `src/include/kernel_vfs_phase4.h`**
*   **Detailed Full Summary**:
    "Phase 4" of the FS: **Integration**.
    *   **Smart API**: `v_open`, `v_read` etc. that presumably handle path resolution, permissions, and journaling automatically, wrapping the lower layers.

### **155. `src/include/limits.h`**
*   **Detailed Full Summary**:
    Standard C library limits definitions.
    *   **Values**: `CHAR_BIT 8`, `INT_MAX 2147483647`, `LLONG_MAX` etc.
*   **Context**:
    Required by many standard libraries (and mbedTLS) to know integer sizes.

### **156. `src/include/mbedtls/aes.h`**
*   **Detailed Full Summary**:
    Header for the **AES (Advanced Encryption Standard)** module in mbedTLS.
    *   **Context**: `mbedtls_aes_context` holds round keys.
    *   **Functions**:
        *   `mbedtls_aes_setkey_enc`: Key expansion for encryption.
        *   `mbedtls_aes_crypt_ecb`: Encrypt/Decrypt a single 16-byte block.
        *   `mbedtls_aes_crypt_cbc`: Cipher Block Chaining mode.
    *   **Modes**: Supporting XTS, CTR, CFB, OFB.
*   **Context**:
    This is a third-party library file we integrated to support encryption (likely for HTTPS).

### **157. `src/include/msg.h`**
*   **Detailed Full Summary**:
    Defines the **IPC Message Protocol** for the Windowing System.
    *   **Envelope**: `gfx_msg_t` containing type, size, and a union of data payloads.
    *   **Messages**:
        *   `MSG_GFX_CREATE_WINDOW`: App asks server for a window.
        *   `MSG_GFX_WINDOW_CREATED`: Server responds with ShmID.
        *   `MSG_GFX_MOUSE/KEY_EVENT`: Server sends input to App.
*   **Context**:
    The core communication definition between GUI applications and the Window Server.

### **158. `src/include/netinet/in.h`**
*   **Detailed Full Summary**:
    Standard networking structures.
    *   **Structs**: `sockaddr_in` (IPv4 address + port).
    *   **Macros**: `IPPROTO_TCP`, `INADDR_ANY`.
    *   **Utils**: `htonl`, `ntohs` (byte swapping for network endianness).

### **159. `src/include/poll.h`**
*   **Detailed Full Summary**:
    Standard `poll()` and `select()` interfaces.
    *   **Struct**: `pollfd` (fd, events, revents).
    *   **Flags**: `POLLIN`, `POLLOUT`.
    *   **Functions**: `poll`, `select`, `FD_SET`, `FD_ISSET`.
*   **Context**:
    Allows waiting on multiple file descriptors (sockets, pipes, input) simultaneously.

### **160. `src/include/pthread.h`**
*   **Detailed Full Summary**:
    **POSIX Threads** (pthreads) header.
    *   **Threads**: `pthread_create`, `pthread_join`.
    *   **Synchronization**:
        *   `pthread_mutex_t` (Locks).
        *   `pthread_cond_t` (Condition Variables).
        *   `pthread_rwlock_t` (Read-Write Locks).
        *   `pthread_barrier_t`, `pthread_spinlock_t`.
    *   **TSD**: Thread-Specific Data keys.
*   **Context**:
    Defines the threading API used by porting libraries and multi-threaded apps.

### **161. `src/include/pwd.h`**
*   **Detailed Full Summary**:
    User/Password Database interface.
    *   **Structs**: `passwd` (User info), `group`.
    *   **Functions**: `getpwnam`, `getuid`, `setuid`.
*   **Context**:
    Standard POSIX API for user management and permission checks.

### **162. `src/include/semaphore.h`**
*   **Detailed Full Summary**:
    POSIX Semaphores and Shared Memory.
    *   **Semaphores**: `sem_t`, `sem_wait`, `sem_post`.
    *   **Message Queues**: `mq_open`, `mq_send`.
    *   **Shared Memory**: `shm_open`.
*   **Context**:
    Inter-process synchronization primitives.

### **163. `src/include/shm.h`**
*   **Detailed Full Summary**:
    System V style Shared Memory interface (specifically for our OS).
    *   **Struct**: `shm_segment_t` (phys_addr, ref_count).
    *   **Functions**: `sys_shmget`, `sys_shmat` (Map), `sys_shmdt` (Unmap).
*   **Context**:
    Used heavily by the GUI to share framebuffers between the Window Server and Clients without copying pixels.

### **164. `src/include/signal.h`**
*   **Detailed Full Summary**:
    Signal handling.
    *   **Signals**: `SIGINT` (Ctrl+C), `SIGSEGV` (Crash), `SIGKILL`.
    *   **Structs**: `sigaction`, `siginfo_t`.
    *   **Functions**: `kill`, `sigaction`, `sigprocmask`.
*   **Context**:
    The mechanism for asynchronous event notifications and process control.

### **165. `src/include/stdio.h`**
*   **Detailed Full Summary**:
    Standard Input/Output.
    *   **Definitions**: `FILE`, `stdout`, `stderr`.
    *   **Stubs**: Wrapper/stub functions for `printf`, `sprintf`.
*   **Context**:
    Minimal libc implementation to satisfy dependencies. `printf` is defined but currently does nothing or returns 0 (which is a TODO).

### **166. `src/include/stdlib.h`**
*   **Detailed Full Summary**:
    Standard Library utility functions.
    *   **Memory**: `malloc`, `free`, `calloc`.
    *   **Process**: `exit` (loops forever).
*   **Context**:
    Minimal libc subset.

### **167. `src/include/string.h`**
*   **Detailed Full Summary**:
    String manipulation.
    *   **Functions**: `memcpy`, `memset`, `strlen`, `strcpy`, `strcmp`.
    *   **Extras**: `itoa` (Integer to ASCII), `strstr`.
*   **Context**:
    Crucial utilities used everywhere in the kernel and userspace.

### **168. `src/include/sys/socket.h`**
*   **Detailed Full Summary**:
    BSD Sockets API.
    *   **Constants**: `AF_INET`, `SOCK_STREAM`, `SOL_SOCKET`.
    *   **Functions**: `socket`, `bind`, `connect`, `listen`, `accept`, `send`, `recv`.
*   **Context**:
    The standard interface for network programming.

### **169. `src/include/sys/stat.h`**
*   **Detailed Full Summary**:
    File status and attributes.
    *   **Struct**: `stat` (size, mode, uid, gid).
    *   **Functions**: `mkdir`, `stat`, `fstat`.
    *   **Macros**: `S_ISDIR`, `S_IRWXU`.
    *   Note: I am inferring this file's existence and content based on standard patterns and the `fcntl.h` content, as the direct read failed. System headers usually follow a predictable structure.

### **170. `src/include/sys_wait.h`**
*   **Detailed Full Summary**:
    POSIX Process Wait API.
    *   **Macros**: `WEXITSTATUS`, `WIFEXITED`, `WIFSIGNALED` to inspect process exit status.
    *   **Options**: `WNOHANG` (return immediately if no child exited).
    *   **Functions**: `wait`, `waitpid`.
*   **Context**:
    Fundamental for process management (shell waiting for commands to finish).

### **171. `src/include/termios.h`**
*   **Detailed Full Summary**:
    Terminal I/O Control interfaces.
    *   **Struct**: `termios` (input/output/control/local flags).
    *   **Flags**: `ECHO` (print typed chars), `ICANON` (line buffering vs raw mode), `ISIG` (enable Ctrl+C signals).
    *   **Functions**: `tcgetattr`, `tcsetattr`, `cfmakeraw`.
*   **Context**:
    Required by text editors (like `vim` or `nano`) and the shell to handle raw input and cursor movement.

### **172. `src/include/time.h`**
*   **Detailed Full Summary**:
    Standard Time API.
    *   **Structs**: `timespec`, `timeval`, `itimerspec`.
    *   **Clocks**: `CLOCK_REALTIME`, `CLOCK_MONOTONIC`.
    *   **Functions**: `clock_gettime`, `nanosleep`, `gettimeofday`, `timer_create`.
*   **Context**:
    Provides high-resolution timing for animations, schedulers, and network timeouts.

### **173. `src/include/types.h`**
*   **Detailed Full Summary**:
    The "Root" Type Definition file.
    *   **Core**: `u8`, `u16`, `u32` (similar to Rust/Go shorthands).
    *   **POSIX**: `pid_t`, `off_t`, `uid_t`.
    *   **Structs**: `timespec`, `stat`, `dirent`.
*   **Context**:
    Almost every kernel and driver file includes this. It standardizes integer widths across the OS.

### **174. `src/include/unistd.h`**
*   **Detailed Full Summary**:
    Standard Unix Symbolic Constants and Functions.
    *   **Descriptors**: `STDIN_FILENO`, `STDOUT_FILENO`.
    *   **Functions**: `fork`, `execve`, `read`, `write`, `close`, `lseek`, `dup2`.
    *   **System**: `sysconf`, `getpid`, `getuid`.
*   **Context**:
    The core userspace API for interacting with the kernel.

### **175. `src/include/vfs.h`**
*   **Detailed Full Summary**:
    The **Virtual Filesystem (VFS)** Core.
    *   **Enums**: `VFS_FILE`, `VFS_DIRECTORY`, `VFS_DEVICE`.
    *   **Contract**: `struct filesystem` defines function pointers (`read`, `write`, `mount`) that any FS driver (FAT16, ext2) must implement.
    *   **Node**: `vfs_node_t` is the in-memory object representing an open file/dir. It has properties (`size`, `inode`) and operation pointers overrideable by drivers.
    *   **API**: `vfs_open`, `vfs_read`, `vfs_mount`.
*   **Context**:
    This is the most complex header. It abstracts "everything is a file". Code can `vfs_read` from a keyboard, a file, or a socket via the same interface.

### **176. `src/include/sys/types.h`**
*   **Detailed Full Summary**:
    A subset of `types.h` specifically for POSIX compliance in the `sys/` namespace (e.g. used by `socket.h`).
    *   **Typedefs**: `ssize_t`, `off_t`, `pid_t`.

### **177. `src/kernel/Contracts.cpp`**
*   **Detailed Full Summary**:
    Implements a set of "Contracts" - high-level wrappers and assertions used to verify kernel functionality.
    *   **Functions**: `fs_readdir` (wrapper), `spawn_process`, `kernel_panic`.
    *   **Safety**: `system_self_test()` verifies FS persistence and process spawning.
*   **Context**:
    Acts as a middleware or compatibility layer, likely for testing "Stages" of kernel development.

### **178. `src/kernel/DeviceModel.cpp`**
*   **Detailed Full Summary**:
    "Stage 6" Device Manager Model.
    *   **Logic**: Maintains a list of registered devices (`Keyboard`, `Mouse`, `Disk`).
    *   **API**: `register_device`, `get_device_by_id`.
*   **Context**:
    Simplified device manager used during early development stages.

### **179. `src/kernel/ExecutionModel.cpp`**
*   **Detailed Full Summary**:
    "Stage 3" Cooperative Scheduler Model.
    *   **States**: `Runnable`, `Running`, `Blocked`.
    *   **Logic**: Round-robin scheduler (`scheduler_pick_next`).
    *   **PCB**: Simple process structure with `entry_point`.

### **180. `src/kernel/FileSystemTruth.cpp`**
*   **Detailed Full Summary**:
    "Stage 5" RAM Filesystem verification model.
    *   **Structure**: In-memory tree of `FSNode`.
    *   **Functions**: `fs_mkdir`, `fs_create_file`, `fs_resolve`.
*   **Context**:
    A reference implementation to verify the real VFS against.

### **181. `src/kernel/HighLevelModel.cpp`**
*   **Detailed Full Summary**:
    "Stage 10" POSIX & Package Manager Model.
    *   **Features**: Mocks file descriptors (`posix_open`), threads, and a package registry.

### **182. `src/kernel/Kernel.cpp`**
*   **Detailed Full Summary**:
    **The Kernel Entry Point**.
    *   **Boot Flow**:
        1.  Initializes GDT, IDT, ISRs.
        2.  Sets up Physical Memory (PMM) and Paging.
        3.  Installs Interrupts (APIC/IO-APIC).
        4.  initializes Hardware (Keyboard, Mouse, HPET, TSC).
        5.  Initializes Heap, Filesystem (VFS, FAT16).
        6.  Detects Network (e1000) and Graphics (BGA).
    *   **Startup**:
        *   Maps a 16MB Framebuffer.
        *   Starts `gui_main` (Window Server) as a kernel thread.
        *   Starts `net_thread` (Network Stack).
        *   Spawns `INIT.ELF` as the first userspace process.
    *   **Legacy**: Mocks `sys_fs_call` and `sys_move`.

### **183. `src/kernel/MemoryModel.cpp`**
*   **Detailed Full Summary**:
    "Stage 4" Memory Allocator Model.
    *   **Logic**: Simple block-based allocator tracking ownership (Kernel vs Process).

### **184. `src/kernel/TaskModel.cpp`**
*   **Detailed Full Summary**:
    "Stage 9" Multitasking Model.
    *   **IPC**: Implements a message queue (`ipc_send_model`).

### **185. `src/kernel/ahci.h`**
*   **Detailed Full Summary**:
    **AHCI / SATA** Controller Definitions.
    *   **Structs**: `HBA_MEM`, `HBA_PORT`, `HBA_CMD_HEADER` (Real hardware specs).
*   **Context**:
    Ready for SATA disk driver implementation (currently unused in `Kernel.cpp`).

### **186. `src/kernel/apic.cpp`**
*   **Detailed Full Summary**:
    **Advanced Programmable Interrupt Controller (APIC)** Driver.
    *   **LAPIC**: Local APIC (per CPU). Enables via Spurious Interrupt Vector.
    *   **IO-APIC**: Chipset Interrupt Controller.
    *   **Logic**: Disables legacy 8259 PIC. Maps legacy IRQs (0-15) to Vectors 32-47. Handles Active High/Low polarity differences for ISOs (Interrupt Source Overrides).

### **187. `src/kernel/block_device.cpp`**
*   **Detailed Full Summary**:
    Block Device Manager.
    *   **Registry**: Stores pointers to `block_device_t` (e.g., SATA, RAMDisk).
    *   **Ops**: `block_read`, `block_write` proxy functions.

### **188. `src/kernel/browser.cpp`**
*   **Detailed Full Summary**:
    **Kernel-Mode Web Browser Engine**.
    *   **Architecture**:
        *   The browser logic runs **inside the kernel**.
        *   `navigate(url)`: Fetches HTTP content via `net_advanced`.
        *   `parse_html()`: Uses `HTML5Parser` to build a DOM.
        *   `layout_content()`: Calculates positions of nodes (Divs, Text).
        *   `draw(Window*)`: Renders the UI directly to a GUI Window's framebuffer.
    *   **Features**:
        *   Basic CSS support (inline styles, `style` tags).
        *   Tabs, URL bar, Back/Forward hooks.
        *   Handling of HTML entities (`&nbsp;`).
*   **Context**:
    Validates that this is a specialized OS where the browser is a first-class kernel citizen (or at least tightly integrated for performance/simplicity).

### **189. `src/kernel/buddy.cpp`**
*   **Detailed Full Summary**:
    **Buddy Memory Allocator**.
    *   **Algorithm**: Manages physical memory in power-of-2 blocks (Pages).
    *   **Ops**: `buddy_alloc` splits blocks; `buddy_free` coalesces generic neighbors.
    *   **Range**: Orders 12 (4KB) to 28 (256MB).

### **190. `src/kernel/gdt.cpp`**
*   **Detailed Full Summary**:
    **Global Descriptor Table (GDT)** Manager.
    *   **Entries**:
        *   Null (0).
        *   Kernel Code/Data (0x08, 0x10).
        *   User Code/Data (0x18, 0x20) (Rings 3).
        *   TSS (Task State Segment) for hardware context switching support (Stack Switching).
    *   **Functions**: `init_gdt`, `gdt_set_gate`, `write_tss`.

### **191. `src/kernel/idt.cpp`**
*   **Detailed Full Summary**:
    **Interrupt Descriptor Table (IDT)** Manager.
    *   **Logic**: Maps Interrupt Numbers (0-255) to Handler Functions (ISRs).
    *   **Security**: Sets DPL (Descriptor Privilege Level) to 3 for Syscall (0x80) to allow user access.

### **192. `src/kernel/isr.cpp`**
*   **Detailed Full Summary**:
    **Interrupt Service Routines (ISRs)**.
    *   **Handlers**: Catches CPU Exceptions (DivZero, PageFault, GPF).
    *   **Logic**: Logs register state on crash. Kills user process on crash (Segfault), or panics kernel if inside kernel.

### **193. `src/kernel/irq.cpp`**
*   **Detailed Full Summary**:
    **IRQ Manager**.
    *   **Mapping**: Remaps PIC/APIC IRQs to vectors 32-47 to avoid collision with CPU exceptions.
    *   **Handler**: Dispatches hardware interrupts (Timer, Keyboard) to registered drivers.

### **194. `src/kernel/paging.cpp`**
*   **Detailed Full Summary**:
    **Virtual Memory Manager (VMM)**.
    *   **Structure**: 2-Level Paging (Page Directory -> Page Table).
    *   **Layout**:
        *   **Identity Map**: 0-512MB (for legacy DMA/ACPI).
        *   **Higher Half**: Kernel at 0xC0000000 (3GB).
    *   **Demand Paging**: `page_fault_handler` lazily allocates physical pages when a process touches valid virtual memory (Heap, Stack). Handles Copy-On-Write (implied).

### **195. `src/kernel/pmm.cpp`**
*   **Detailed Full Summary**:
    **Physical Memory Manager (PMM)**.
    *   **Algorithm**: Bitmap-based allocator. Each bit represents a 4KB page.
    *   **API**: `pmm_alloc_block` (Alloc 4KB), `pmm_alloc_contiguous_blocks` (for DMA).

### **196. `src/kernel/heap.cpp`**
*   **Detailed Full Summary**:
    **Kernel Heap Allocator**.
    *   **Hybrid Strategy**:
        *   **Slab Allocator**: For small objects (< 2KB), highly efficient.
        *   **Buddy Allocator**: For large objects (via `src/kernel/buddy.cpp`).
    *   **Functions**: `kmalloc`, `kfree`. Includes canary checks (`0xCAFEBABE`) to detect corruption.

### **197. `src/kernel/process.cpp`**
*   **Detailed Full Summary**:
    **Process Scheduler & Manager**.
    *   **Scheduling**: Round-Robin with Priority and Time Slices.
    *   **Multitasking**: `init_multitasking`, `create_user_process`, `sys_fork`, `sys_exec`.
    *   **Lifecycle**: Handles Zombies (`waitpid`), Orphan cleanup.
    *   **Security**: Implements `pledge` (restrict syscalls) and `unveil` (restrict filesystem views).
    *   **Features**: Signals (`sys_kill`), Alarms (`sys_alarm`), File Descriptors (per process).

### **198. `src/kernel/syscall.cpp`**
*   **Detailed Full Summary**:
    **System Call Interface**.
    *   **Handler**: Dispatches calls from User Mode (via Int 0x80 usually, verified in ISR).
    *   **Implemented Calls**:
        *   **File I/O**: `open`, `read`, `write`, `close`, `seek`, `stat`.
        *   **Process**: `fork`, `execve`, `exit`, `waitpid`, `getpid`.
        *   **Network**: `socket`, `bind`, `connect`, `http_get` (Kernel-mode HTTP!).
        *   **GUI**: `get_framebuffer` (Direct framebuffer access for Window Server).
        *   **Security**: `pledge`, `unveil`.
    *   **Validation**: `validate_user_pointer` ensures users can't crash kernel by passing bad pointers.

### **199. `src/kernel/gui_system.cpp`**
*   **Detailed Full Summary**:
    **The "Window Server" & Desktop Environment**.
    *   **Architecture**: Monolithic GUI subsystem inside the kernel.
    *   **Components**:
        *   **FB**: Direct framebuffer manipulation (`put`, `rect`, `blend_rect`).
        *   **Input**: Mouse/Keyboard polling and event dispatch (`double_click`).
        *   **IconSystem**: Procedural drawing of icons (Folder, Terminal, Browser) without external assets.
        *   **UI**: Basic widget toolkit (Buttons, ListItems, Scrollbars).
        *   **WindowManager**: Manages overlapping windows (though code focuses on a single desktop view with overlays).
        *   **ContextMenu**: Right-click menu implementation with actions (`Open`, `Rename`, `Delete`).
        *   **Clipboard**: Global clipboard for file paths (Cut/Copy/Paste).
    *   **Apps**: Mocks launch points for `Explorer`, `Terminal`, `Browser`.

### **200. `src/kernel/css_engine.cpp`**
*   **Detailed Full Summary**:
    **CSS Selector Engine**.
    *   **Logic**: Matches CSS selectors against DOM nodes.
    *   **Support**: Type (`div`), ID (`#id`), Class (`.class`), Universal (`*`), and Compound selectors (`div.class`).
    *   **Status**: Basic support for descendant combinators (implied).

### **201. `src/kernel/css_parser.cpp`**
*   **Detailed Full Summary**:
    **CSS Parser**.
    *   **Algorithm**: Recursive Descent Parser.
    *   **Grammar**: Parses Rulesets -> Selectors -> Declarations (Property: Value).
    *   **Features**: Handles Units (`px`, `%`, `em`), Colors (Hex), Strings, and Keywords (`!important`).

### **202. `src/kernel/css_tokenizer.cpp`**
*   **Detailed Full Summary**:
    **CSS Tokenizer**.
    *   **Output**: Stream of `IDENT`, `HASH`, `DELIM`, `STRING`, `NUMBER`, `DIMENSION` tokens.

### **203. `src/kernel/html_parser.cpp`**
*   **Detailed Full Summary**:
    **HTML5 Tree Construction**.
    *   **Algorithm**: State machine based on the HTML5 spec phases (`INITIAL`, `BEFORE_HTML`, `IN_HEAD`, `IN_BODY`, `AFTER_BODY`).
    *   **Output**: Builds a DOM tree of `ElementNode`, `TextNode`, `DoctypeNode`.
    *   **Error Handling**: Basic handling for auto-closing tags and void elements (like `<img>`).

### **204. `src/kernel/html_tokenizer.cpp`**
*   **Detailed Full Summary**:
    **HTML5 Tokenizer**.
    *   **States**: `DATA`, `TAG_OPEN`, `TAG_NAME`, `ATTRIBUTE_NAME`, `ATTRIBUTE_VALUE`, `COMMENT`.
    *   **Features**: Handles self-closing tags, comments, and character references (`&amp;` simplified).

### **205. `src/kernel/http.cpp`**
*   **Detailed Full Summary**:
    **Kernel HTTP Client**.
    *   **Dependencies**: Uses `net_advanced` (TCP) and `http_parser`.
    *   **Features**:
        *   DNS Resolution (`dns_resolve`).
        *   HTTP/1.1 GET Requests.
        *   HTTPS Support (via `tls_init` hooks).
        *   Chunked Transfer Encoding handling (via callback).
    *   **Context**: Allows the kernel to download files directly (e.g., for `wget` or the browser).

### **206. `src/kernel/http_parser.c`**
*   **Detailed Full Summary**:
    **Joyent HTTP Parser** (Embedded).
    *   **Source**: The standard C library used in Node.js.
    *   **Role**: robustness parsing of HTTP headers and bodies, handling edge cases like split packets and weird encodings.

### **207. `src/kernel/e1000.cpp`**
*   **Detailed Full Summary**:
    **Intel e1000 Network Driver**.
    *   **Functionality**:
        *   PCI configuration/enabling (Bus Master).
        *   MMIO mapping for control registers using `map_mmio`.
        *   Descriptors: Ring-based DMA (TX/RX rings with 16-byte alignment).
        *   Initialization: Resets NIC, sets up Multicast/Broadcast filters, enables RX/TX.
        *   `e1000_send`: Prepares TX descriptor, updates Tail pointer, waits for write-back.
        *   `e1000_receive`: Polls RX ring for descriptors with "DD" (Descriptor Done) bit set.
        *   **Context**: Primary NIC for QEMU/VirtualBox environments.

### **208. `src/kernel/net_stack.cpp`**
*   **Detailed Full Summary**:
    **Integrated TCP/IP Stack Core**.
    *   **Layers**:
        *   **Ethernet**: Routes packets to Gateway MAC for Internet access, handles ARP resolution.
        *   **ARP**: Caches MAC addresses (`arp_table`), sends ARP requests if entry missing.
        *   **IP**: Constructs IPv4 headers, calculates checksums, handles routing.
        *   **TCP**: Implements a state machine (`SYN_SENT`, `ESTABLISHED`) and handshake logic (`SYN` -> `SYN-ACK` -> `ACK`).
        *   **Features**:
            *   Basic congestion control/window management.
            *   Retransmission timer for unacknowledged SYNs.
    *   **Handlers**: `net_stack_rx` dispatches ARP, IP, TCP packets.

### **209. `src/kernel/dhcp.cpp`**
*   **Detailed Full Summary**:
    **DHCP Client**.
    *   **Protocol**: Implements RFC 2131 (Discover -> Offer -> Request -> Ack).
    *   **State Machine**: `dhcp_state` tracks progess (0=Init, 1=Discovering...).
    *   **Parsing**: Extracts Options 1 (Subnet), 3 (Router), 6 (DNS), 51 (Lease).
    *   **API**: `dhcp_configure()` blocks until network is auto-configured.

### **210. `src/kernel/dns.cpp`**
*   **Detailed Full Summary**:
    **DNS Resolver**.
    *   **Protocol**: UDP-based DNS query generation (Recursion Desired).
    *   **Encoding**: Converts "www.google.com" -> `3www6google3com0`.
    *   **Features**:
        *   **Caching**: In-memory LRU cache (`dns_cache`) with TTL expiration.
        *   **Async/Sync**: Supports both blocking `dns_resolve` and non-blocking `dns_resolve_async`.
    *   **Decoding**: Handles DNS response parsing, including pointer compression (`0xC0`).

### **211. `src/kernel/net_advanced.h`**
*   **Detailed Full Summary**:
    **Unified Networking Header**.
    *   **Definitions**: Structures for HTTP, Sockets, DNS configuration.
    *   **API**: Declares high-level functions like `http_get`, `net_socket`, `dns_resolve`.

### **212. `src/kernel/tcp.cpp`**
*   **Detailed Full Summary**:
    **Full TCP Implementation**.
    *   **TCB**: Transmission Control Block (`tcp_tcb_t`) tracks connection state (Seq/Ack numbers, Windows).
    *   **Logic**:
        *   `tcp_connect`: Initiates 3-way handshake.
        *   `tcp_handle_packet`: State machine processing (`SYN_SENT`, `ESTABLISHED`, `FIN_WAIT`).
        *   `tcp_send_segment`: Constructs TCP segments with correct flags/checksums.
        *   **Buffering**: Manages a 32KB Ring Buffer for RX data.

### **213. `src/kernel/udp.cpp`**
*   **Detailed Full Summary**:
    **UDP Protocol Support**.
    *   **Architecture**: Simple port-based dispatch table (`udp_port_table`).
    *   **Logic**:
        *   `udp_send`: Wraps data in UDP header + Pseudo-header checksum.
        *   `udp_receive`: Demultiplexes incoming packets to registered handlers.

### **214. `src/kernel/socket.cpp`**
*   **Detailed Full Summary**:
    **Socket VFS Interface (Kernel Side)**.
    *   **Concept**: Bridges POSIX syscalls (`read`, `write`, `close`) to network stack logic.
    *   **Support**:
        *   `AF_INET` (Internet): Redirects calls to `net_socket`, `net_connect`, `net_send`.
        *   `AF_UNIX` (Local): Implements local socket pairs/pipes via ring buffers.
    *   **VFS Integration**: Creates VFS nodes with `VFS_SOCKET` flag so `sys_read`/`sys_write` work on them.

### **215. `src/kernel/socket_api.cpp`**
*   **Detailed Full Summary**:
    **BSD Socket API Implementation**.
    *   **Role**: The user-facing API implementation (the "syscall handlers").
    *   **Functions**: `socket`, `bind`, `connect`, `listen`, `accept`, `send`, `recv`.
    *   **Logic**:
        *   Manages a global table of `socket_entry` structures.
        *   Handles blocking/timeouts for `recv` and `connect`.
        *   Abstracts TCP/UDP differences (stream vs datagram).

### **216. `src/kernel/net.cpp`**
*   **Detailed Full Summary**:
    **Legacy/Core Network Dispatcher**.
    *   **Role**: Original/Legacy entry point for network packets.
    *   **Features**:
        *   Basic ICMP handling (Ping Echo Reply).
        *   Central `net_rx_handler` that routes to `handle_tcp`, `handle_udp`.
        *   Low-level IP send logic with Gateway MAC routing (Critical for QEMU/SLIRP).

### **217. `src/kernel/vfs.cpp`**
*   **Detailed Full Summary**:
    **Virtual File System (VFS) Core**.
    *   **Structure**: Central registry of filesystem nodes (`vfs_node_t`) representing files, directories, and devices.
    *   **Mount Points**: Manages mount points (`/`, `/home`, `/dev`). Defaults to RAMFS if FAT16 fails, otherwise bootstraps FAT16 and Phase A RAMFS overlay.
    *   **Path Resolution**:
        *   `vfs_resolve_path`: Tokenizes paths (`/home/user`) and walks the directory tree.
        *   **Phase A Integration**: Special hacks to seamlessly inspect "Phase A" memory-based filesystem nodes inside the main VFS tree (`vfs_phase_a_lookup`).
    *   **Operations**:
        *   `vfs_create`, `vfs_read`, `vfs_write`: High-level wrappers that dispatch to node-specific or filesystem-specific function pointers.
        *   `vfs_init`: Sets up the standard hierarchy (`/home/user/Desktop`, `/dev`, `/system`).

### **218. `src/kernel/file_ops.cpp`**
*   **Detailed Full Summary**:
    **POSIX File System Call Implementation**.
    *   **Role**: Implements the logic behind standard file syscalls.
    *   **Functions**:
        *   `sys_pread`/`sys_pwrite`: Atomic read/write at offset.
        *   `sys_lseek`: Modifies file descriptor offset.
        *   `sys_dup`: Duplicates file descriptors.
        *   `sys_stat`: Returns file attributes (`st_size`, `st_mode`).
        *   `sys_ioctl`: Handles device control (e.g., getting terminal window size).
        *   `sys_access`: Checks permissions (currently permissive).

### **219. `src/kernel/tty.cpp`**
*   **Detailed Full Summary**:
    **Terminal Subsystem (TTY)**.
    *   **Modes**:
        *   **Canonical**: Buffer lines until `Enter` is pressed (supports Backspace).
        *   **Raw**: Returns characters immediately (for editors like `vi`).
    *   **Features**:
        *   **Echoing**: Prints typed characters back to screen.
        *   **Signals**: Translates `Ctrl+C` (ASCII 3) into `SIGINT` for the foreground process group.
    *   **Buffer**: Uses `tty_line_buffer` for canonical input processing.

### **220. `src/kernel/pty.cpp`**
*   **Detailed Full Summary**:
    **Pseudo-Terminal (PTY) Implementation**.
    *   **Architecture**: Master/Slave pairs for terminal emulators (e.g., the GUI Terminal app).
    *   **Data Flow**:
        *   Host writes to Master -> Input appears on Slave (Shell reads this).
        *   Shell writes to Slave -> Data appears on Master (GUI reads this to display).
    *   **Integration**: Registers `/dev/pts/N` and `/dev/ptm/N` nodes.

### **221. `src/kernel/pipe.cpp`**
*   **Detailed Full Summary**:
    **Inter-Process Pipes**.
    *   **Logic**: Circular buffer implementation protected by process scheduling (blocking read/write).
    *   **Syscall**: `sys_pipe` creates two file descriptors: one for reading, one for writing.
    *   **Synchronization**: Wakes up waiting readers when data is written, and waiting writers when space frees up.

### **222. `src/kernel/elf_loader.cpp`**
*   **Detailed Full Summary**:
    **ELF Executable Loader**.
    *   **Function**: `load_elf` reads standard ELF32 binaries.
    *   **Validation**: Checks Magic (`7F 45 4C 46`).
    *   **Mapping**: Scans Program Headers (`PT_LOAD`), allocates physical memory via PMM, and maps it to the requested virtual addresses using `vm_map_page`.
    *   **BSS**: Zeroes out the `.bss` section (uninitialized data).

### **223. `src/kernel/vm.cpp`**
*   **Detailed Full Summary**:
    **Virtual Memory Manager**.
    *   **Page Tables**: Manages x86 Page Directories (PD) and Page Tables (PT).
    *   **Functions**:
        *   `vm_map_page`: Maps a physical frame to a virtual address.
        *   `vm_get_phys`: Resolves virtual-to-physical translation.
        *   `pd_create`/`pd_switch`: Manages per-process address spaces (CR3 switching).

### **224. `src/kernel/shm.cpp`**
*   **Detailed Full Summary**:
    **System V Shared Memory**.
    *   **Mechanism**: Allows processes to share physical memory pages.
    *   **Features**:
        *   `sys_shmget`: Allocates a contiguous physical memory block.
        *   `sys_shmat`: Maps that block into a fixed virtual address range (`0x70000000` + index * 8MB) to facilitate pointer sharing (simplified design).

### **225. `src/kernel/char_device.cpp`**
*   **Detailed Full Summary**:
    **Character Device Registry**.
    *   **Role**: Maintains a list of registered character drivers (like `tty`, `random`, `fb`).
    *   **Lookup**: Finds devices by Major/Minor number or by Name.

### **226. `src/kernel/sysconf.cpp`**
*   **Detailed Full Summary**:
    **System Configuration & Resource Usage**.
    *   **POSIX Compliance**: Implements `sysconf` (page size, clock ticks) and `pathconf` (filename lengths).
    *   **Resources**: Implements `getrlimit`/`setrlimit` (stack size, open files) and `getrusage` (CPU time accounting).

### **227. `src/kernel/slab.cpp`**
*   **Detailed Full Summary**:
    **SLAB Memory Allocator**.
    *   **Purpose**: Efficient allocation of fixed-size small objects to reduce fragmentation.
    *   **Design**: Maintains caches for powers of two (32B to 2048B).
    *   **Implementation**: Uses a linked list of free objects embedded within the free blocks themselves.

### **228. `src/kernel/clock.cpp`**
*   **Detailed Full Summary**:
    **Timekeeping Subsystem**.
    *   **POSIX Clocks**: Implements `CLOCK_REALTIME` (via RTC + offset), `CLOCK_MONOTONIC` (ticks since boot).
    *   **Sleeping**: Implements `nanosleep` by setting a `sleep_until` target in the current process and rescheduling.
    *   **Timers**: Supports interval timers (`timer_create`) using a delta-list approach in `check_timers`.

### **229. `src/kernel/tsc.cpp`**
*   **Detailed Full Summary**:
    **Time Stamp Counter**.
    *   **Low-Level**: Reads the CPU's 64-bit cycle counter (`rdtsc`) for high-precision timing or calibration.

### **230. `src/kernel/fs_kernel_bridge.c`**
*   **Detailed Full Summary**:
    **Standalone Kernel-GUI Bridge**.
    *   **Nature**: A seemingly self-contained implementation of a file system bridge, possibly used for a specific GUI environment/simulator or as verifyable reference code.
    *   **Components**: Defines its own `KernelFile`, `VfsNode` structures, and a simple memory-backed file dispatcher (`kernel_fs_dispatch`).

### **231. `src/kernel/compiler_runtime.cpp`** & **`cpp_support.cpp`**
*   **Detailed Full Summary**:
    **Runtime Support**.
    *   **Math**: Provides 64-bit division/modulo (`__udivdi3`, `__umoddi3`) which are often missing in bare-metal environments.
    *   **C++**: Implements global constructor calling (`__cxx_global_ctor_init`), `new`/`delete` operators (wrapping `kmalloc`/`kfree`), and stack smashing protection stubs.

### **232. `src/drivers/acpi.cpp`**
*   **Detailed Full Summary**:
    **ACPI Table Scanner**.
    *   **Logic**: Scans `0xE0000`-`0xFFFFF` for "RSD PTR " signature.
    *   **Function**: `acpi_find_table` walks the RSDT (Root System Description Table) to find other tables like HPET or APIC.

### **233. `src/drivers/pci.cpp`**
*   **Detailed Full Summary**:
    **PCI Bus Enumerator**.
    *   **Access**: Uses I/O ports `0xCF8` (Address) and `0xCFC` (Data).
    *   **Features**:
        *   `pci_find_device`: Scans 256 buses / 32 slots / 8 functions for a specific Vendor/Device ID.
        *   `pci_get_bga_bar0`: Specifically implemented to find the Bochs Graphics Adapter framebuffer.

### **234. `src/drivers/ata.cpp`**
*   **Detailed Full Summary**:
    **ATA (IDE) Disk Driver**.
    *   **Mode**: PIO (Programmed Input/Output), polling-based (busy wait).
    *   **Functions**: `ata_read_sector`, `ata_write_sector` (512 bytes).
    *   **Target**: Primary Master drive.

### **235. `src/drivers/serial.cpp`**
*   **Detailed Full Summary**:
    **Serial Port Driver (COM1)**.
    *   **Config**: 38400 baud, 8N1.
    *   **Purpose**: Debug logging host-side (QEMU redirects this to stdio).

### **236. `src/drivers/timer.cpp`**
*   **Detailed Full Summary**:
    **PIT (Programmable Interval Timer)**.
    *   **Frequency**: Configures Channel 0 to 100Hz.
    *   **Handler**: Increments global `tick`, wakes up sleeping processes, triggers scheduling.

### **237. `src/drivers/rtc.cpp`**
*   **Detailed Full Summary**:
    **Real Time Clock (CMOS)**.
    *   **IO**: Ports `0x70`/`0x71`.
    *   **Function**: Reads Year/Month/Day/Hour/Min/Sec to set the system clock. Assumes BCD encoding.

### **238. `src/drivers/keyboard.cpp`**
*   **Detailed Full Summary**:
    **PS/2 Keyboard Driver**.
    *   **Map**: Hardcoded US QWERTY layout (with Shift support).
    *   **Logic**: Handles Make/Break codes, tracks modifier state (Ctrl, Alt, Shift).
    *   **Signal**: Sends `SIGINT` to current process on `Ctrl+C`.

### **239. `src/drivers/mouse.cpp`**
*   **Detailed Full Summary**:
    **PS/2 Mouse Driver**.
    *   **Protocol**: Standard PS/2 3-byte packets (State, X-rel, Y-rel).
    *   **State**: Updates global `mouse_x`, `mouse_y` (clamped to screen dims) and `mouse_btn`.

### **240. `src/drivers/bga.cpp`**
*   **Detailed Full Summary**:
    **Bochs Graphics Adapter Driver**.
    *   **Registers**: Uses VBE extensions IO ports (`0x1CE`/`0x1CF`) to set resolution and enable Linear Frame Buffer (LFB).

### **241. `src/drivers/vga.cpp`**
*   **Detailed Full Summary**:
    **Legacy VGA Text Driver**.
    *   **Context**: Typically used for early boot text before BGA switch.
    *   **Memory**: Writes to `0xB8000`.

### **242. `src/drivers/graphics.cpp`**
*   **Detailed Full Summary**:
    **Graphics Primitives**.
    *   **Architecture**: Software Double-Buffering. Draws to `back_buffer` (malloc'd), swaps to `screen_buffer` (LFB).
    *   **Drawing**: `put_pixel`, `draw_line` (Bresenham), `draw_rect`.

### **243. `src/drivers/bmp.cpp`**
*   **Detailed Full Summary**:
    **BMP Image Decoder**.
    *   **Format**: Supports 24-bit and 32-bit BMPs.
    *   **Rendering**: Handles coordinate flip (bottom-up storage) and row padding logic.

### **244. `src/drivers/fat16.cpp`**
*   **Detailed Full Summary**:
    **FAT16 File System Implementation**.
    *   **Features**:
        *   Read/Write support.
        *   Cluster chain traversal (FAT table lookup).
        *   Directory iteration (Root + Subdirectories).
        *   Legacy 8.3 filename parsing.
        *   Implements `fat16_read_vfs`, `fat16_write_vfs`, `fat16_readdir_vfs` to binding to VFS.

### **245. `src/drivers/devfs.cpp`**
*   **Detailed Full Summary**:
    **Device File System**.
    *   **Structure**: Synthetic filesystem exposing `/dev`.
    *   **Nodes**:
        *   `null`: EOF on read, discard on write.
        *   `zero`: Zeroes on read, discard on write.
        *   `tty`: Proxy to current console.
        *   `pts`: Directory listing active pseudo-terminals.

### **246. `src/drivers/hpet.cpp`**
*   **Detailed Full Summary**:
    **High Precision Event Timer**.
    *   **Role**: Used for calibration of TSC.
    *   **IO**: Memory mapped configuration (found via ACPI).

### **247. `src/boot/boot.asm`**
*   **Detailed Full Summary**:
    **Master Boot Record (MBR) Bootloader**.
    *   **Structure**: 512-byte boot sector (`org 0x7c00`).
    *   **Filesystem Context**: Contains a valid FAT16 BIOS Parameter Block (BPB) to be recognized as a valid partition.
    *   **Logic**:
        1.  Initializes Stack at `0x7C00`.
        2.  Prints a "Real Mode" welcome message.
        3.  Enables A20 Line.
        4.  **Loads Kernel**: Reads 1300 sectors (650KB) from disk into memory at `0x8000`. Handles cross-segment loading (increments ES every 64KB).
        5.  `switch_to_pm`: Jumps to 32-bit Protected Mode and executes the kernel.

### **248. `src/boot/disk.asm`**
*   **Detailed Full Summary**:
    **BIOS Disk Routines (Real Mode)**.
    *   **Function**: `disk_read_sector` uses INT 13h / AH=02h to read sectors.
    *   **Error Handling**: Prints "Disk Error!" if Carry Flag is set.
    *   **Context**: Helper for `boot.asm`; now simplified as `load_kernel` in `boot.asm` handles the CHS (Cylinder-Head-Sector) calculation logic directly.

### **249. `src/boot/gdt.asm`**
*   **Detailed Full Summary**:
    **Global Descriptor Table (GDT)**.
    *   **Purpose**: Defines memory segments for Protected Mode.
    *   **Segments**:
        *   `Null`: Mandatory empty descriptor.
        *   `Code`: Base 0, Limit 4GB, Executable/Readable (`0x9A`).
        *   `Data`: Base 0, Limit 4GB, Writable (`0x92`).
    *   **Constants**: Exports `CODE_SEG` (0x08) and `DATA_SEG` (0x10).

### **250. `src/boot/kernel_entry.asm`**
*   **Detailed Full Summary**:
    **Kernel Entry Point**.
    *   **Role**: The bridge between `boot.asm` (raw binary) and `main()` (C++ kernel).
    *   **Paging**: Sets up a rudimentary Page Directory (`BootPageDirectory`) and Page Tables (`BootPageTables`) to identity-map the first 64MB of physical memory to `0xC0000000` (Higher Half Kernel).
    *   **Execution**: Enables Paging (CR0 bit 31), sets Stack Pointer (`stack_top`), and calls `main`.

### **251. `src/boot/print.asm`**
*   **Detailed Full Summary**:
    **Real Mode Printing**.
    *   **Routine**: `print_string` uses BIOS INT 10h / AH=0Eh (Teletype output) to print null-terminated strings.

### **252. `src/boot/switch_pm.asm`**
*   **Detailed Full Summary**:
    **Protected Mode Switcher**.
    *   **Steps**:
        1.  `cli`: Disable interrupts.
        2.  `lgdt`: Load GDT Descriptor.
        3.  `cr0`: Set bit 0 (PE).
        4.  `jmp`: Far jump to `CODE_SEG:init_pm` to flush pipeline.
        5.  **Init PM**: Updates segment registers (DS, SS, etc.) to `DATA_SEG`.
        6.  **Stack**: Moves stack to `0x1E000000` (480MB), well clear of lower memory.
        7.  Jumps to `BEGIN_PM` in `boot.asm`.

### **253. `src/lib/Assertions.cpp`**
*   **Detailed Full Summary**:
    **Kernel Assertion Library**.
    *   **Namespace**: `Std`.
    *   **Function**: `crash(msg, file, line, func)` logs an error report to Serial and halts the CPU (`cli; hlt`).

### **254. `src/lib/string.cpp`**
*   **Detailed Full Summary**:
    **Standard String Library Implementation**.
    *   **Functions**: `memcpy`, `memmove`, `memset`, `strlen`, `strcpy`, `strcmp`, `strncmp`, `strcat`, `strchr`, `strstr`.
    *   **Extras**: `itoa` (Integer to ASCII) with base support.
    *   **Optimization**: `memcpy` attempts 32-bit word copies for aligned buffers.

### **255. `apps/Contracts.cpp`**
*   **Detailed Full Summary**:
    **App-Kernel Userland Bridge**.
    *   **Purpose**: Provides C-compatible wrappers (`fs_readdir`, `spawn_process`, `ipc_send`) around C++ OS API calls.
    *   **Integration**: Used to allow simpler C-style applications to interact with the object-oriented OS API.

### **256. `apps/minimal_os_api.cpp`**
*   **Detailed Full Summary**:
    **Minimal Userland OS API**.
    *   **Graphics**: Implements `gfx_text`, `gfx_rect`, `gfx_clear` by connecting to the OS Window Server via `g_ipc`.
    *   **Input**: Polls IPC messages for `MSG_GFX_MOUSE_EVENT` and `MSG_GFX_KEY_EVENT`.
    *   **Filesystem**: Wraps POSIX syscalls (`open`, `readdir`, `mkdir`) into C++ `std::vector<FileEntry>` results.

### **257. `apps/posix_impl.cpp`**
*   **Detailed Full Summary**:
    **Userland POSIX Syscall Wrappers (libc-ish)**.
    *   **Mechanism**: Uses `int $0x80` to invoke kernel system calls.
    *   **Functions**:
        *   **Process**: `fork` (9), `execve` (10), `exit` (12), `wait` (11).
        *   **File**: `open` (2), `read` (3), `write` (4), `close` (5), `mkdir` (25), `unlink` (24).
        *   **Memory**: `kmalloc` via `SYS_SBRK` (6).
        *   **Threading**: Stubs for `pthread_create` (96), `sem_init` (106), `sem_wait` (109).

### **258. `apps/sh.cpp`**
*   **Detailed Full Summary**:
    **Command Shell**.
    *   **Features**:
        *   **Built-ins**: `cd`, `pwd`, `help`, `exit`, `clear`.
        *   **External Execution**: Forks and executing binaries found in `/`, `/bin/`, or `/apps/`.
        *   **Prompt**: Shows current working directory (ANSI colored).

### **259. `apps/ls.cpp`**
*   **Detailed Full Summary**:
    **List Directory Utility**.
    *   **Logic**: Opens directory using `open()`, iterates with `readdir()`.
    *   **Output**: Prints filenames. Directories are highlighted in Cyan using ANSI escape codes (`\x1b[1;36m`).

### **260. `apps/cat.cpp`**
*   **Detailed Full Summary**:
    **Concatenate File Utility**.
    *   **Logic**: Opens file, reads in 1024-byte chunks, and writes to stdout (fd 1).
    *   **Error Handling**: Reports "No such file" if open fails.

### **261. `apps/mkdir.cpp`**
*   **Detailed Full Summary**:
    **Make Directory Utility**.
    *   **Logic**: Wrapper around `syscall_mkdir` with default permission `0755`.

### **262. `apps/df.cpp`**
*   **Detailed Full Summary**:
    **Disk Free GUI Utility**.
    *   **Interface**: Graphical window "Disk Usage".
    *   **Logic**: Calls `statfs` syscall (not standard POSIX, likely custom) to get total/free blocks.
    *   **Rendering**: Draws a usage bar (Green on Dark Grey) representing disk consumption.

### **263. `apps/file_utils.cpp`**
*   **Detailed Full Summary**:
    **File Utilities Library/Tool**.
    *   **Role**: A consolidated tool/library offering functions for `pwd`, `cd`, `mkdir`, `ls`, `cp`, `mv`, `rm`, and `tree`.
    *   **Tree**: Implements a recursive directory walker (`tree_recursive`) to visualize folder structure.
    *   **Entry Point**: `_start` runs a quick demonstration of these features.

### **264. `apps/init.cpp`**
*   **Detailed Full Summary**:
    **Init Process**.
    *   **Role**: The first userland process.
    *   **Loop**: Prints a start message and enters an eternal sleep loop (`sleep(100)`), keeping the system alive.

### **265. `apps/ping.cpp`**
*   **Detailed Full Summary**:
    **Network Ping Utility**.
    *   **Logic**: Sends a hardcoded ICMP request to Gateway (`10.0.2.2`).
    *   **Mechanism**: Uses custom syscall `SYS_NET_PING`.
    *   **Feedback**: Currently relies on checking the kernel serial log.

### **266. `apps/tcptest.cpp`**
*   **Detailed Full Summary**:
    **TCP Connectivity Verification**.
    *   **Target**: Hardcoded `example.com` (93.184.216.34:80).
    *   **Mechanism**: Uses custom syscall `156` (`syscall_tcp_test`).
    *   **Purpose**: Validates the `src/kernel/tcp.cpp` stack implementation.

### **267. `apps/notepad.cpp`**
*   **Detailed Full Summary**:
    **Graphical Text Editor**.
    *   **Framework**: Uses `OS::IPCClient` for windowing and `OS::gfx_msg_t` for input.
    *   **UI Components**:
        *   **Toolbar**: File/Edit/Help menus with drop-downs.
        *   **Status Bar**: Shows file path and status messages.
        *   **Text Area**: Renders text using an internal bitmap font renderer (`font8x8`).
    *   **Core**: `TextBuffer` manages a dynamic array of lines (`char**`) with support for insertion, backspace, newline, and scrolling.
    *   **IO**: Loads/Saves files via OS syscalls (`open`, `read`, `write`).

### **268. `apps/terminal.cpp`**
*   **Detailed Full Summary**:
    **Graphical Terminal Emulator**.
    *   **Backend**: Creates a PTY Master/Slave pair (`sys_pty_create` syscall 154). Fork executes `/apps/sh.elf` attached to the PTY Slave.
    *   **Emulation**:
        *   Supports basic ANSI escape codes for color (e.g., `\x1b[1;36m` for Cyan).
        *   Handles scrolling when cursor hits bottom.
    *   **Input**: Forwards Keyboard events to the PTY Master fd.

### **269. `apps/textview.cpp`**
*   **Detailed Full Summary**:
    **Text File Viewer**.
    *   **Mechanism**: Direct Framebuffer Access (Syscalls 150-153) instead of Window Server IPC (Lower level approach compared to `notepad`).
    *   **Rendering**: Own implementation of `font8x8` rendering.
    *   **Features**: Read-only scrolling view of a file passed as argument.

### **270. `apps/hello.cpp`**
*   **Detailed Full Summary**:
    **Graphical "Hello World" Demo**.
    *   **Window**: Creates a 300x200 window.
    *   **Content**: Draws a dark grey background and a red square using `fill_rect` via IPC.

### **271. `apps/posix_suite.cpp`**
*   **Detailed Full Summary**:
    **Comprehensive POSIX Compliance Test Suite**.
    *   **Scope**:
        *   **Processes**: `fork`, `wait`, `getpid`, `getppid`.
        *   **File I/O**: `open` (O_CREAT), `read`, `write`, `stat`, `unlink`, `lseek`.
        *   **IPC**: `pipe` creation and data transfer.
        *   **Threads**: `pthread_create` and `pthread_join`.
        *   **Signals**: `sigaction` registration and `alarm` triggering.
    *   **Output**: Logs [PASS]/[FAIL] for each assertion to the console.

### **272. `apps/posix_test.cpp`**
*   **Detailed Full Summary**:
    **Legacy/Simple POSIX Test**.
    *   **Focus**: Specifically tests Threading and Synchronization primitives (`sem_init`, `sem_wait`, `sem_post`).

### **273. `apps/test.cpp`**
*   **Detailed Full Summary**:
    **Console Integration Test**.
    *   **Role**: Validates that the shell can successfully execute a user program (`run test.elf`).
    *   **Action**: Prints a success banner to stdout confirming the toolchain is working.

### **274. `build.sh`**
*   **Detailed Full Summary**:
    **Build System Script**.
    *   **Logic**:
        1.  Cleans previous builds.
        2.  Compiles **Apps** (`apps/*.cpp`) first, linking them against `minimal_os_api` and `posix_impl`. Uses `ld -T apps/linker.ld`.
        3.  Compiles **Bootloader** (`src/boot/*.asm`).
        4.  Compiles **Kernel Sources** (`src/kernel/*.cpp`, `src/drivers/*.cpp`, etc.) with `gcc/g++ -m32`.
        5.  Links Kernel (`src/kernel/kernel.bin`) using `linker.ld`.
        6.  Concatenates `boot.bin` + `kernel.bin` -> `os.img`.
        7.  Injects files into the FAT16 image using `inject_wallpaper.py`.

### **275. `linker.ld`**
*   **Detailed Full Summary**:
    **Kernel Linker Script**.
    *   **Layout**:
        *   **Load Address**: `0xC0008000` (Higher Half Kernel, shifted by 3GB).
        *   **Sections**: `.text`, `.rodata`, `.init_array`/`.fini_array` (for C++), `.data`, `.bss`.
    *   **Alignment**: 4KB page alignment for sections.

### **276. `fd_offset_and_terminal_contract.c`**
*   **Detailed Full Summary**:
    **Reference Implementation / "Source of Truth"**.
    *   **Purpose**: This file isn't compiled directly into the OS but serves as the architectural specification for:
        1.  **File Descriptors**: Struct `file_description` with `offset` handling (fixing previous bugs where offsets were ignored).
        2.  **PTY Driver**: Circular buffer logic for Pseudo-Terminals.
        3.  **Shell Logic**: How `read(0)` and `write(1)` interact with PTYs.
        4.  **GUI Integration**: How the Window Server pipes events to the PTY Master.

### **277. `inject_wallpaper.py`**
*   **Detailed Full Summary**:
    **FAT16 Injection Utility**.
    *   **Role**: Post-build tool to insert files into the `os.img` disk image without mounting it (which requires sudo/loopback).
    *   **Logic**:
        *   Parses FAT16 BPB (Hardcoded geometry to match `boot.asm`).
        *   Traverses/Creates FAT chains to allocate clusters.
        *   Writes directory entries to Root Directory.
        *   **Manifest**: Injects `WALL.BMP` and all compiled `.ELF` apps into the disk image.

### **278. `export_to_word.py`**
*   **Detailed Full Summary**:
    **Documentation Tool**.
    *   **Function**: Scans the codebase (ignoring binaries/git) and compiles all source files into a single `Codebase_Export.docx` for external review/printing.

### **279. `src/kernel/main.cpp`**
*   **Detailed Full Summary**:
    **Kernel C++ Entry Point**.
    *   **Role**: This small file acts as a proof-of-concept for the C++ runtime within the kernel.
    *   **Function**: `cpp_kernel_entry` demonstrates that:
        1.  **Heap Allocation**: `new TestClass()` works, implying `kmalloc` is hooked up to the `new` operator.
        2.  **Constructors**: `TestClass()` is called, logging "Constructor called!".
        3.  **Methods**: `t->hello()` confirms vtable/dispatch works.
        4.  **Logging**: All actions are logged to `serial_log`.
    *   **Note**: Explicitly mentions that `kfree` / `delete` might not be fully safe/ready in this context yet.

### **280. `src/kernel/Kernel.cpp`**
*   **Detailed Full Summary**:
    **The Kernel Core ("The Brain")**.
    *   **Initialization Sequence (`main`)**:
        1.  **Early Init**: Sets up Serial logging, GDT (`init_gdt`), and IDT (`isr_install`).
        2.  **Memory**: Initializes Physical Memory Manager (PMM) at 8MB and marks kernel/IO regions as used.
        3.  **Paging**: Enables virtual memory (`init_paging`).
        4.  **Hardware**: Installs specific IRQ handlers (`irq_install`), Keyboard, Mouse, and HPET time source.
        5.  **Runtime**: Initializes Kernel Heap (Slab Allocator), C++ Constructors, FAT16, and VFS.
        6.  **Drivers**:
            *   **PCI**: Scans for E1000 network card and BGA graphics.
            *   **Graphics**: If BGA is found, maps 16MB of VRAM and clears the screen to black.
        7.  **Multitasking**: Starts the scheduler (`init_multitasking`) and spawns:
            *   `gui_main` (The Window Server thread).
            *   `net_thread` (The Network Stack thread).
            *   `INIT.ELF` (The first user-space process).
    *   **Contracts**: Implements bridges for `sys_get_framebuffer`, `sys_spawn`, and `sys_fs_transaction` to link Kernel logic with Drivers and Apps.

### **281. `src/kernel/idt.cpp`**
*   **Detailed Full Summary**:
    **Interrupt Descriptor Table (IDT)**.
    *   **Structure**: Defines the `idt` array of 256 gates.
    *   **Setup**: `set_idt_gate` populates an entry with the handler address, Segment Selector (KERNEL_CS), and Flags (Present, 32-bit Interrupt Gate).
    *   **User Mode**: `set_idt_gate_user` enables DPL=3, allowing Ring 3 apps to trigger specific interrupts (like `int 0x80` for syscalls).

### **282. `src/kernel/gdt.cpp`**
*   **Detailed Full Summary**:
    **Global Descriptor Table (GDT) & TSS**.
    *   **Segments**:
        *   **0**: Null.
        *   **1**: Kernel Code (Ring 0).
        *   **2**: Kernel Data (Ring 0).
        *   **3**: User Code (Ring 3).
        *   **4**: User Data (Ring 3).
        *   **5**: Task State Segment (TSS).
    *   **TSS**: Sets up the Task State Segment required for hardware context switching (specifically for switching the stack pointer `ESP0` when moving from Ring 3 to Ring 0).
    *   **Installation**: `init_gdt` populates these entries and calls assembly `gdt_flush`.

### **283. `src/kernel/irq.cpp`**
*   **Detailed Full Summary**:
    **Interrupt Request (IRQ) Management**.
    *   **PIC Remapping**: `irq_remap` moves the legacy PIC interrupts from 0-15 to 32-47 to avoid conflicts with CPU exceptions (0-31).
    *   **APIC**: Attempts to initialize Advanced PIC (APIC) via ACPI tables; falls back to legacy PIC if missing.
    *   **Handlers**: Installs ISR handlers for IRQ0-15 (IDT 32-47).
    *   **Dispatcher**: `irq_handler` acknowledges the interrupt (sends EOI) and calls the registered C function handler. It also checks for pending signals before returning to user mode.

### **284. `src/kernel/isr.cpp`**
*   **Detailed Full Summary**:
    **Interrupt Service Routines (ISR)**.
    *   **Exception Handling**: Installs handlers for CPU Exceptions 0-31 (Divide by Zero, Page Fault, GPF, etc.).
    *   **Syscall**: Registers `isr128` (0x80) as a User Gate for system calls.
    *   **Logging**: The default `isr_handler` dumps CPU register state (EIP, CS, EFLAGS) to the serial log for debugging crashes.
    *   **FPU**: ISR #7 (Device Not Available) and ISR #16 (FPU Error) hints at floating-point support logic.

### **285. `src/kernel/process.cpp`**
*   **Detailed Full Summary**:
    **Process Management & Scheduler**.
    *   **Structures**: Manages `process_t` (PCB) containing PID, State, Page Directory, Open Files (`fd_table`), and Signal masks.
    *   **Multitasking**: Implements `schedule()` which uses a Round-Robin algorithm with priorities to pick the next `PROCESS_READY` task. Context switching is done by changing `ESP` and `CR3`.
    *   **Creation**:
        *   `create_kernel_thread`: Creates a process sharing kernel space only.
        *   `create_user_process`: Sets up a full user address space, loads an ELF binary, and maps a user stack.
        *   `sys_fork`: Clones the current process (Cow/Page Tables clone), returning 0 to child and Child PID to parent.
    *   **Lifecycle**: `sys_exit` turns a process into a `ZOMBIE`, and `sys_waitpid` allows the parent to reap it.
    *   **Exec**: `exec_process` replaces the current memory map with a new ELF image.

### **286. `src/kernel/syscall.cpp`**
*   **Detailed Full Summary**:
    **System Call Dispatcher**.
    *   **Role**: The central switchboard for `int 0x80` requests.
    *   **Validation**: `validate_user_pointer` ensures pointers passed by apps are safely within user bounds.
    *   **Implemented Calls**:
        *   **FS**: `open`, `read`, `write`, `close`, `lseek`, `stat`, `mkdir`.
        *   **Process**: `fork`, `execve`, `exit`, `wait`, `getpid`.
        *   **Memory**: `sbrk` (heap), `mmap`, `munmap`.
        *   **Network**: `socket`, `connect`, `send`, `recv`, `dns_resolve` (bridged to net stack).
        *   **Graphics**: `get_framebuffer`, `fb_swap` (Direct BGA access).
    *   **Security**: Includes OpenBSD-style `pledge` and `unveil` checks to restrict process capabilities.

### **287. `src/kernel/paging.cpp`**
*   **Detailed Full Summary**:
    **Virtual Memory Manager (Page Tables)**.
    *   **Architecture**: Uses 32-bit x86 Paging (4KB pages, 1024 PTEs per Table, 1024 Tables per Directory).
    *   **Mapping Strategy**:
        *   **Identity Map**: First 512MB (Physical 0-512MB -> Virtual 0-512MB) for bootstrap and legacy drivers (ACPI/VGA).
        *   **Higher Half**: Maps Kernel (0xC0000000+) to Physical (0x0+). This allows user apps to live in the lower half (0-3GB).
    *   **Fault Handler**: `page_fault_handler` (ISR 14).
    *   **Demand Paging**: `handle_demand_paging` catches Page Faults and lazily allocates:
        *   **User Heap**: 0x40000000 - 0x70000000.
        *   **User Stack**: Around 0xB0000000.
        *   **Shared Memory**: 0x70000000 - 0x80000000.
    *   **Context Switch**: `switch_page_directory` loads CR3.

### **288. `src/kernel/pmm.cpp`**
*   **Detailed Full Summary**:
    **Physical Memory Manager (Bitmap Allocator)**.
    *   **Mechanism**: Uses a simple bitmap where 1 bit = 1 Physical 4KB Page (`PMM_BLOCK_SIZE`).
    *   **Initialization**: `pmm_init` sets up the bitmap based on RAM size found by Bootloader. It marks Kernel code/data/BSS and BGA Video RAM as "Used".
    *   **Allocation**: `pmm_alloc_block` scans the bitmap for the first 0 bit (Free), flips it to 1, and returns the physical address.
    *   **Contiguous Allocation**: `pmm_alloc_contiguous_blocks` finds N consecutive free bits, critical for DMA buffers.

### **289. `src/kernel/heap.cpp`**
*   **Detailed Full Summary**:
    **Kernel Heap (General Allocator)**.
    *   **Dual Strategy**:
        1.  **Slab Allocator**: Optimizes small, fixed-size objects (< 2048 bytes) which are frequent in OS (nodes, FDs).
        2.  **Buddy Allocator**: Handles larger allocations (> 2KB) by splitting larger blocks into powers of two.
    *   **Hook**: `kmalloc` delegates to one of these based on size.
    *   **Safety**: Validates `magic` numbers in block headers (`0xCAFEBABE`) on free to detect double-frees or corruption.

### **290. `src/kernel/memory.cpp`**
*   **Detailed Full Summary**:
    **Heap API Facade**.
    *   **Role**: Provides the public API (`kmalloc`, `kfree`, `kmalloc_a` for aligned).
    *   **Bootstrapping**: Implements `placement_kmalloc`—a bump pointer allocator used *before* the main heap and paging are fully initialized (Early Init Phase). Once `heap_ready` flag is set, it switches to the sophisticated Buddy/Slab system.

### **291. `src/kernel/mprotect.cpp`**
*   **Detailed Full Summary**:
    **Memory Protection & POSIX compliance**.
    *   **Syscalls**: Implements `mprotect`, `msync`, `mlock`.
    *   **Logic**: Parses standard unix protection flags (`PROT_READ`, `PROT_WRITE`, `PROT_EXEC`).
    *   **Implementation**: Walks the page tables for the given range and updates PTE flags (e.g., Read-Only bit). Flushes TLB (`invlpg`) to apply changes immediately.
    *   **Note**: `msync` and `mlock` are currently stubs as there is no swap or memory-mapped files support yet.

### **292. `src/kernel/buddy.cpp`**
*   **Detailed Full Summary**:
    **Buddy Memory Allocator**.
    *   **Algorithm**: Splits memory into blocks of order $2^N$.
    *   **Alloc**: Checks free list for requested order. If empty, goes to Order+1, splits it, puts one half in Order's list, and returns the other.
    *   **Free**: Checks if the "buddy" (adjacent block of same size) is also free. If so, merges them into a single block of Order+1.
    *   **Benefit**: Greatly reduces external fragmentation for general-purpose allocations.

### **293. `src/kernel/net.cpp`**
*   **Detailed Full Summary**:
    **Core Network Layer & Ethernet Logic**.
    *   **Role**: This file implements the base Ethernet, ARP, and ICMP protocols. It serves as the primary dispatcher for incoming frames from the network driver.
    *   **MAC & IP Identity**: Hardcodes the interface identity (e.g., `10.0.2.15` and MAC `52:54:00:12:34:56`) to match QEMU's SLIRP environment.
    *   **ARP Management**: Handles Address Resolution Protocol. It can send ARP requests to find the Gateway MAC and responds to ARP requests for itself. Crucially, it stores the `gateway_mac` after learning it from a reply, which is essential for routing traffic outside the local node.
    *   **ICMP (Ping)**: Implements ICMP Echo Reply logic. When a Ping Echo Request arrives, it swaps source/destination and sends back the same payload.
    *   **RX Dispatch**: The `net_rx_handler` inspects the EtherType (0x0806 for ARP, 0x0800 for IP) and hands off packets to `handle_arp` or the IP layer.
    *   **Polling**: `net_poll` continuously checks the E1000 driver for new packets and triggers processing.

### **294. `src/kernel/e1000.cpp`**
*   **Detailed Full Summary**:
    **Intel 8254x (E1000) Network Driver**.
    *   **MMIO & PCI**: Locates the device via PCI Bus/Slot/Function, enables Bus Mastering, and maps BAR0 (Memory Mapped I/O) into the kernel's virtual space.
    *   **DMA Rings**: Sets up Transmit (TX) and Receive (RX) Circular Rings. Each descriptor in the ring points to a 2048-byte physical memory buffer allocated for packet data.
    *   **Initialization**: Configures specific registers like `RCTL` (Receive Control) and `TCTL` (Transmit Control). It enables Broadcast Accept Mode (`BAM`) and Strips Ethernet CRC (`SECRC`).
    *   **Transmit**: `e1000_send` copies packet data to a TX buffer, sets the "End of Packet" (EOP) and "Report Status" (RS) bits in the descriptor, and advances the Tail Pointer (`TDT`) to alert the hardware.
    *   **Receive**: `e1000_receive` polls the "Descriptor Done" (DD) bit in the current RX tail. If a packet is present, it copies it out and moves the Tail Pointer (`RDT`) to give the buffer back to the hardware.

### **295. `src/kernel/net_stack.cpp`**
*   **Detailed Full Summary**:
    **Integrated RFC-Compliant TCP/IP Stack (Alternative/Integrated version)**.
    *   **Purpose**: This file provides a more "monolithic" and standard-aligned implementation of Ethernet, ARP, and IP/TCP headers compared to the split files.
    *   **Checksum Logic**: Implements the Internet Checksum algorithm (RFC 1071), including the "Pseudo-header" calculation for TCP segments which involves source/destination IPs.
    *   **Routing Logic**: Features an `ethernet_send` function that checks if a destination IP is local. If not, it routes the packet to the Gateway MAC retrieved from the internal `arp_table`.
    *   **TCP Engine**: Implements the basics of the TCP State Machine (SYN_SENT, ESTABLISHED, etc.). It manages sequence and acknowledgment numbers to ensure reliable ordered delivery.
    *   **Debugging**: Includes verbose "SYN DUMP" logic to verify header integrity during the initial handshake phase.

### **296. `src/kernel/net_init.cpp`**
*   **Detailed Full Summary**:
    **Network Stack Coordinator**.
    *   **Orchestration**: This file acts as the high-level manager that initializes all sub-components: TCP, UDP, Socket API, DHCP, DNS, and HTTP.
    *   **Boot Sequence**: Specifically triggers `dhcp_configure` early on. If successful, it updates the global `net_status` with dynamic IP/Gateway/DNS info; otherwise, it falls back to static QEMU defaults (`10.0.2.2`).
    *   **Self-Test Suite**: Implements `net_self_test`, which runs a battery of diagnostic checks: Link status verification, DNS resolution of `example.com`, Ping to the gateway, and a sample HTTP GET request. This ensures the entire stack is functional before user-space applications start.

### **297. `src/kernel/dhcp.cpp`**
*   **Detailed Full Summary**:
    **DHCP Client (RFC 2131)**.
    *   **State Machine**: Implements the four-step "DORA" process:
        1.  **Discover**: Broadcasts a UDP packet asking for an IP.
        2.  **Offer**: Receives an IP offer from a server.
        3.  **Request**: Formally requests the offered IP.
        4.  **Acknowledge (ACK)**: Receives the final confirmation and lease terms.
    *   **Configuration**: Extracts crucial network parameters from the DHCP Options field: Subnet Mask (Opt 1), Router/Gateway (Opt 3), and DNS Server (Opt 6).
    *   **Lease Management**: Tracks the `lease_start` time and provides `dhcp_check_lease` to trigger a renewal (DHCP REQUEST) when 50% of the lease time (T1) has elapsed.

### **298. `src/kernel/dns.cpp`**
*   **Detailed Full Summary**:
    **DNS Resolver (RFC 1035)**.
    *   **Protocol**: Implements binary DNS queries over UDP Port 53.
    *   **Encoding/Decoding**: Includes logic to convert hostnames like `www.google.com` into the "Label" format (`3www6google3com0`) used in the wire protocol. It also decodes complex response packets that use "Compression Pointers" (0xC0 offset) to save space.
    *   **Caching**: Features a `dns_cache` with an LRU/TTL policy. Successful resolutions are stored for 5 minutes (default), significantly speeding up subsequent requests for the same domain across different processes.
    *   **Blocking & Async**: Provides `dns_resolve` (blocking wait with retries) and `dns_resolve_async` for non-blocking browser-style lookups.

### **299. `src/kernel/socket.cpp`**
*   **Detailed Full Summary**:
    **VFS Socket Integration (AF_UNIX / Legacy)**.
    *   **VFS Bridging**: Maps standard socket operations (`read`, `write`, `close`) to VFS node functions. This allows sockets to be treated as file descriptors (FDs) in user-space.
    *   **Circular Buffers**: Each socket has a 4KB `buffer` for producer/consumer synchronization. 
    *   **Blocking Logic**: Implements process sleeping; if a `read` is called on an empty socket or a `write` on a full one, the `current_process` is set to `WAITING` and the scheduler is called.
    *   **Inter-Process Integration**: When one process writes to its FD, the `socket_write` logic wakes up any processes waiting to read from the peer socket, enabling efficient IPC.

### **300. `src/kernel/socket_api.cpp`**
*   **Detailed Full Summary**:
    **BSD-style Socket Implementation (AF_INET)**.
    *   **State Management**: Manages the `socket_entry` array, tracking states from `CREATED` to `CONNECTED`.
    *   **Ephemeral Ports**: Implements port allocation logic (starting at 49152) for outbound connections to ensure no two local sockets conflict.
    *   **UDP Handling**: Registers a global callback for UDP packets. When a packet arrives, it uses the destination port to find the matching socket and pushes data into that socket's private `rx_buffer`.
    *   **TCP Integration**: Acts as a wrapper around the `tcp.cpp` stack. `net_connect` initiates the handshake and blocks until `tcp_is_connected` becomes true or a timeout occurs.
    *   **POSIX Stubs**: Implements `htons`, `htonl`, `inet_ntoa`, and `inet_aton` for address formatting.

### **301. `src/kernel/tcp.cpp`**
*   **Detailed Full Summary**:
    **TCP Protocol Implementation (Handshake & Data)**.
    *   **TCB (Task Control Block)**: Tracks all metadata for a connection, including source/destination IPs/Ports, and vital RFC sequence variables: `snd_nxt` (next to send), `rcv_nxt` (expected), and `snd_una` (unacknowledged).
    *   **Segment Generation**: `tcp_send_segment` builds the TCP header, calculates the pseudo-header checksum, and updates sequence numbers. It also handles the "Window Size" advertisement to notify the peer how much buffer space is available.
    *   **State Machine Logic**: `tcp_handle_packet` processes flags (SYN, ACK, FIN, PSH). It handles the 3-way handshake (SYN -> SYN-ACK -> ACK) and ensures data segments are only accepted if their sequence number matches `rcv_nxt`.
    *   **Graceful Close**: Implements the FIN/ACK sequence (`FIN_WAIT_1` -> `FIN_WAIT_2` -> `TIME_WAIT`) to cleanly tear down connections.

### **302. `src/kernel/udp.cpp`**
*   **Detailed Full Summary**:
    **UDP Protocol Implementation**.
    *   **Statelessness**: Unlike TCP, it doesn't track connections. It simply provides a `udp_bind` mechanism to register handler functions for specific ports.
    *   **Packet Handling**: `udp_receive` validates the length and port, then executes the registered callback with the raw payload.
    *   **Checksum Implementation**: Includes the UDP Checksum logic over the pseudo-header, although it supports a "Zero Checksum" bypass common in legacy IPv4 for performance/compatibility.
    *   **Efficiency**: Directly interacts with the IP layer (`ip_send`) without buffering, suitable for time-sensitive protocols like DNS or DHCP.

### **303. `src/kernel/http.cpp`**
*   **Detailed Full Summary**:
    **Kernel-Space HTTP Client**.
    *   **Protocol Support**: Implements HTTP/1.1 GET requests. It handles URL parsing (splitting `https://host/path`), DNS resolution, and TCP port 80/443 handshake.
    *   **Header Parsing**: Uses a state-machine parser to extract `Content-Length` and `Transfer-Encoding`. It correctly handles `chunked` encoding, reassembling data into a contiguous buffer for the caller.
    *   **In-Place Processing**: To save memory, it attempts to parse responses in-place within the receive buffer.
    *   **TLS Support**: Transparently switches to the `tls_adapter` if the URL starts with `https://`, allowing the kernel to perform secure requests.

### **304. `src/kernel/tls_adapter.cpp`**
*   **Detailed Full Summary**:
    **MbedTLS Bridge / TLS Adapter**.
    *   **Role**: Interfaces the complex `mbedtls` library with the kernel's simpler TCP stack.
    *   **Handshake**: Orchestrates the SSL/TLS handshake. Key steps include `ssl_setup`, `set_hostname` (for SNI), and a loop calling `mbedtls_ssl_handshake`.
    *   **BIO Callbacks**: Defines `tls_send_cb` and `tls_recv_cb`, which map MbedTLS read/write requests directly to `tcp_send_data` and `tcp_read_data`.
    *   **Security Policy**: Explicitly sets `MBEDTLS_SSL_VERIFY_NONE` for the hobby kernel environment to allow connections to common websites without requiring a complex root CA store initialization.

### **305. `src/kernel/mbedtls_glue.cpp`**
*   **Detailed Full Summary**:
    **Standard Library Shims for MbedTLS**.
    *   **Memory Hooks**: Redirects MbedTLS memory requests (`calloc`, `free`) to the kernel's `kmalloc` and `kfree`.
    *   **Logging**: Maps `retroos_printf` to `serial_log` for debugging encrypted traffic.
    *   **Entropy Source**: Implements a "seed" function for the Random Number Generator. In this kernel, it uses a mix of system time (`sys_time_ms`) and CPU counters to provide the necessary randomness for generating encryption keys.
    *   **Stubs**: Provides minimal implementations of `snprintf` and `exit` required by the library's internal logic.

### **306. `src/kernel/browser.cpp`**
*   **Detailed Full Summary**:
    **Kernel Integrated Web Browser Application**.
    *   **Architecture**: Implements a complete browser application within the kernel namespace `Browser`. It manages a `BrowserState` which includes a 64KB content buffer, URL tracking, and scroll positions.
    *   **Browser Chrome**: The `draw` function renders a modern "Google Chrome" style UI including a tab bar, navigation buttons (Back/Refresh), and a URL bar with focus states.
    *   **Rendering Pipeline**:
        1.  **Navigate**: Uses `http_get` to fetch raw HTML.
        2.  **Parse**: Calls `HTML5Parser` to build a DOM tree.
        3.  **Style**: Traverses the DOM to collect `<style>` tags and applies CSS rules via `CSSEngine`.
        4.  **Flatten**: Converts the hierarchical DOM into a linear `LayoutNode` list for efficient rendering.
        5.  **Layout**: Calculates text wrapping and block positioning based on element styles (`margin`, `display: block`).
        6.  **Paint**: The `draw` loop iterates through layout nodes and uses `FontSystem` to paint text and squares to the window's framebuffer.
    *   **Interactivity**: Handles keyboard input in the URL bar (including backspace/enter) and mouse clicks for navigation and scrolling.

### **307. `src/kernel/gui_system.cpp`**
*   **Detailed Full Summary**:
    **Retro-OS Graphics & Desktop Master Environment**.
    *   **Highest Level Subsystem**: This serves as the "Window Manager" and "Desktop Shell" for the OS. It bridges low-level drivers (BGA, Keyboard, Mouse) to high-level user interaction.
    *   **Desktop Shell**: Renders a taskbar, desktop icons (Terminal, Browser, Calculator, Notepad), and a wallpaper. It tracks "Window Objects" and manages their Z-order for focused window rendering.
    *   **Context Menu System**: Implements a robust right-click system (`ContextMenu`). It supports "Intents" which can trigger kernel actions like "Delete File", "Rename", or "Open Terminal" directly from the GUI.
    *   **File Explorer Integration**: Features deep integration with the VFS. Double-clicking an icon triggers `sys_spawn` or `sys_context_execute`. It supports Drag-and-Drop (`DragState`) and a Clipboard (`Clipboard`) for cut/copy/paste file operations across the desktop.
    *   **Dialogs & Logic**: Handles complex modal states like "Rename Dialog" with its own input loop, ensuring the user can only interact with the dialog until it is dismissed.
    *   **Scaling & Icons**: Uses a custom `IconSystem` to draw vector-like icons (Folders, Houses, Monitors) using basic primitives to maintain a small kernel footprint while achieving a premium look.

### **308. `src/kernel/html_parser.cpp`**
*   **Detailed Full Summary**:
    **HTML5 Tree Construction Engine**.
    *   **Logic**: Implements the "Tree Construction" phase of the HTML5 specification. It uses an `InsertionMode` state machine (INITIAL, BEFORE_HTML, IN_HEAD, IN_BODY, etc.) to correctly handle malformed HTML.
    *   **DOM Tree Construction**: As the tokenizer emits tokens, this parser creates `ElementNode` and `TextNode` objects and links them into a `DocumentNode` hierarchy.
    *   **Auto-Correction**: Features logic to automatically insert required tags (like `<html>`, `<head>`, `<body>`) if they are missing in the source stream.
    *   **Void Elements**: Correct handles self-closing tags (void elements) like `<img>`, `<br>`, and `<link>` by not waiting for a corresponding end tag.
    *   **Text Consolidation**: Automatically merges consecutive character tokens into a single `TextNode` to reduce memory fragmentation and improve layout performance.

### **309. `src/kernel/html_tokenizer.cpp`**
*   **Detailed Full Summary**:
    **HTML5 Lexical Analyzer**.
    *   **Purpose**: Scans raw HTML strings and emits a stream of tokens (`TokenType`).
    *   **State Machine**: Implements a complex state machine (DATA, TAG_OPEN, TAG_NAME, ATTRIBUTE_NAME, etc.) that follows the HTML5 spec's tokenization rules.
    *   **Attribute Parsing**: Robustly handles attributes in tags, including quoted (single/double) and unquoted values. It can store up to 16 attributes per tag.
    *   **Entity Reference Handling**: Provides a `NAMED_CHARACTER_REFERENCE` state to decode common entities like `&lt;`, `&gt;`, and `&amp;` on the fly.
    *   **Comment Support**: Includes states to correctly skip or capture HTML comments (`<!-- ... -->`) and handles DOCTYPE declarations to set the document's rendering mode.

### **310. `src/kernel/css_parser.cpp`**
*   **Detailed Full Summary**:
    **CSS Syntax Parser**.
    *   **Role**: Converts CSS text (from `<style>` tags or external files) into a `CSSStyleSheet` data structure.
    *   **Grammar Support**: Implements recursive descent parsing for:
        - **Rulesets**: Groups of selectors and declarations.
        - **Selectors**: Supports Universal (`*`), Type (`div`), ID (`#main`), and Class (`.active`) selectors.
        - **Combinators**: Recognizes descendant (` `), child (`>`), and sibling (`+`) combinators in complex selectors.
        - **Declarations**: Parses property-value pairs (e.g., `color: red !important`).
    *   **Error Recovery**: Includes logic to skip malformed rules (e.g., missing semicolons or brackets) to ensure the rest of the stylesheet can still be used.
    *   **Values**: Correctly identifies and parses different value types like Colors (`#hex`), Dimensions (`12px`), Keywords (`block`), and Numeric values.

### **311. `src/kernel/css_tokenizer.cpp`**
*   **Detailed Full Summary**:
    **CSS Lexical Scanner**.
    *   **Lexical Rules**: Dedicated tokenizer for CSS syntax. It distinguishes between Identifiers, Hashes, Numbers, Dimensions (numbers with units like `px` or `%`), and Strings.
    *   **Numeric Logic**: Implements a robust `consume_numeric` function that can parse scientific notation and floating-point numbers often found in precise CSS layouts.
    *   **Comment Stripping**: Automatically detects and ignores C-style comments (`/* ... */`) within the CSS stream.
    *   **Hash Discovery**: Specifically identifies Hash tokens as either IDs (if they follow identifier rules) or raw hex color codes.

### **312. `src/kernel/css_engine.cpp`**
*   **Detailed Full Summary**:
    **CSS Cascading & Selector Matching Engine**.
    *   **Matching Logic**: The core algorithm (`matches`) determines if a CSS selector applies to a given DOM node. It checks tag names, IDs, and the `class` attribute (including multi-class support where `has_class` splits the attribute by spaces).
    *   **Pseudo-Class Support**: Includes experimental support for `:first-child` by checking the sibling pointers of the DOM node.
    *   **Cascading Implementation**: Provides `apply_styles` which traverses the entire DOM tree and applies matching rules from a `CSSStyleSheet`. It handles "Specificity" by applying rules in order and supports the `!important` override flag.
    *   **Computed Styles**: Acts as the bridge that fills the `Style` structure used by the Layout Engine, translating high-level CSS properties into layout-relevant flags like `is_block` and `margin`.

### **313. `src/kernel/pthread.cpp`**
*   **Detailed Full Summary**:
    **POSIX Threads (pthreads) Implementation**.
    *   **Architecture**: Implements a complete user-space threading library inside the kernel. It manages a `thread_t` pool (max 64 threads) and provides the standard Pthread API.
    *   **Thread Creation**: `pthread_create` allocates a 16KB stack via `kmalloc`, sets up a `thread_wrapper` to transition into the thread's start routine, and integrates with the kernel's scheduler to begin execution.
    *   **Synchronization Primitives**:
        - **Mutexes**: Implements `pthread_mutex_lock/unlock` using an atomic "Compare-and-Swap" (`atomic_cas`) helper. It supports recursive and error-checking mutex types.
        - **Condition Variables**: Implements `pthread_cond_wait/signal` allowing threads to block efficiently until a specific condition is met, using process sleeping logic.
        - **Read-Write Locks**: Provides `pthread_rwlock` for high-concurrency scenarios where multiple readers are allowed but writers require exclusive access.
    *   **Thread-Specific Data (TSD)**: Manages per-thread storage keys (`pthread_key_create`) with support for custom destructor callbacks that run upon thread exit.
    *   **Advanced Features**: Supports thread cancellation (`pthread_cancel`) with deferred/asynchronous types and cancellation points (`pthread_testcancel`).

### **314. `src/kernel/signal.cpp`**
*   **Detailed Full Summary**:
    **POSIX Signal Management Subsystem**.
    *   **Core Purpose**: Implements the IPC mechanism for asynchronous notifications (Signals) between processes.
    *   **Action Handling**: Tracks `signal_actions` for each process. Processes can register custom handlers via `sigaction` or `signal`, or use defaults (Ignore, Terminate, Stop).
    *   **Signal Masking**: Implements `sigprocmask` to allow processes to block specific signals. Crucially, `SIGKILL` and `SIGSTOP` are hardcoded as unblockable to ensure kernel control.
    *   **Dispatch Mechanism**: The `handle_signals` function is called every time a process returns from a syscall or interrupt to user-space. If a signal is pending, it:
        1. Saves the current registers.
        2. Sets up a "Signal Frame" on the user's stack.
        3. Redirects execution to the registered handler.
        4. Provides a `sigreturn` path to restore the original context after the handler finishes.
    *   **Process Control**: Implements `kill` for sending signals to PIDs, PGIDs (Process Groups), or broadcasting to all non-privileged processes.

### **315. `src/kernel/posix_ipc.cpp`**
*   **Detailed Full Summary**:
    **POSIX IPC Extensions (Semaphores, Message Queues, Shared Memory)**.
    *   **Semaphores**:
        - **Unnamed**: Fast in-memory counters used for thread synchronization via `sem_wait/post`.
        - **Named**: Persistent semaphores identified by a path (e.g., `/my_sem`) allowed unrelated processes to synchronize.
    *   **Message Queues**: Implements `mq_open/send/receive`. Messages consist of data and a priority. The queue automatically sorts incoming messages by priority so higher-priority tasks are received first. It supports up to 64 messages of 256 bytes each.
    *   **Named Shared Memory**: Implements `shm_open` and `shm_unlink`. It creates a named memory object that multiple processes can map into their address space, serving as the fastest form of IPC for high-volume data transfer.
    *   **Safety**: Uses spinlocks (`cli/sti`) during counter modifications to ensure atomicity in the multi-tasking kernel environment.

### **316. `src/kernel/select.cpp`**
*   **Detailed Full Summary**:
    **I/O Multiplexing (select/poll/pselect)**.
    *   **Role**: Allows a single thread to monitor multiple file descriptors (sockets, pipes, TTYs) simultaneously, blocking until at least one is "ready" for I/O.
    *   **Poll Implementation**: The `poll` function iterates through a list of `pollfd` structures, checking `vfs_node` states. If no descriptor is ready, it puts the calling process to sleep for a specified `timeout_ms`.
    *   **Select Compatibility**: Implements the legacy `select` API using bitsets (`fd_set`). It handles the conversion of sets to a simplified kernel poll-check loop efficiently.
    *   **Timeout Handling**: Uses the system `tick` counter to calculate precise wait durations, ensuring syscalls return `0` (timeout) or `>0` (ready count) accurately.

### **317. `src/kernel/termios.cpp`**
*   **Detailed Full Summary**:
    **Terminal Attribute Implementation (termios)**.
    *   **Standard Interface**: Implements the POSIX `termios.h` interface for controlling TTY devices.
    *   **Configuration Flags**:
        - **Input (`iflag`)**: Maps CR to NL, enables flow control (XON/XOFF).
        - **Output (`oflag`)**: Handles tab expansion and NL-to-CRNL translation.
        - **Local (`lflag`)**: Controls "Canonical Mode" (line-buffered input), "Echoing" (showing typed characters), and "Signals" (Ctrl+C generation).
    *   **Special Characters**: Configures the `c_cc` array to recognize control keys like `VINTR` (0x03), `VERASE` (Backsp), and `VEOF` (Ctrl+D).
    *   **Baud Rate State**: Although virtual, it tracks baud rate settings (`B9600`, etc.) to satisfy application requirements for serial communication.

### **318. `src/kernel/dirent.cpp`**
*   **Detailed Full Summary**:
    **POSIX Directory Entry Abstraction**.
    *   **Directory Streams**: Implements the `DIR` handle and associated functions (`opendir`, `readdir`, `closedir`).
    *   **VFS Bridging**: Wraps raw kernel `vfs_node->readdir` calls into a POSIX-compliant interface. This allows user programs to iterate through file systems without knowing the underlying format (FAT16, RAMFS, etc.).
    *   **Reentrancy**: Provides `readdir_r` for safe multi-threaded directory traversal.
    *   **System Calls**: Implements `sys_getdents` (get directory entries), which is the low-level Linux-compatible syscall used by `ls` and file managers to efficiently fetch batches of directory entries in a single kernel transition.

### **319. `src/kernel/FileSystemTruth.cpp`**
*   **Detailed Full Summary**:
    **Logical Model of File System Correctness**.
    *   **"Truth" Implementation**: This is a standalone, simplified in-memory model of a filesystem. It uses basic structures like `FSNode` with hard-coded limits (32 children per directory) to represent the "ideal" logic of a hierarchical file system.
    *   **Goal**: Serves as a reference implementation for "Stage 5" of the kernel development. It provides `fs_resolve`, `fs_mkdir`, and `fs_create_file` implementations that are used to verify the behavior of the more complex, hardware-backed VFS and FAT16 drivers.
    *   **Self-Verification**: Includes `stage5_self_test` which performs a sequence of operations (create, resolve, remove) and uses `CONTRACT_ASSERT` to ensure every step behaves exactly as the formal model dictates.

### **320. `src/kernel/DeviceModel.cpp`**
*   **Detailed Full Summary**:
    **Logical Device & Driver Abstraction Model**.
    *   **Classification**: Defines a clean taxonomy for system hardware using `DeviceType` (Keyboard, Mouse, Disk, Display).
    *   **Registry**: Implements a `register_device` / `unregister_device` pattern. This model decouples the "idea" of a device from its specific hardware implementation (I/O ports, MMIO), allowing the kernel to reason about device presence abstractly.
    *   **Stage 6 Testing**: Used in the "Stage 6" bootself-test to verify that the device management logic (adding/removing drivers) is robust and correctly handles edge cases like table overflows.

### **321. `src/kernel/ExecutionModel.cpp`**
*   **Detailed Full Summary**:
    **Formal Model of Process Life-cycle & Scheduling**.
    *   **State Machine**: Implements a strict state machine for processes: `Created -> Runnable -> Running -> Blocked -> Dead`.
    *   **Scheduler Logic**: Provides a pure cooperative scheduler implementation (`scheduler_tick`). It demonstrates the core logic of picking a "Runnable" process and transitioning it to "Running".
    *   **Invariants**: Ensures that only a "Running" process can yield or block, and that the scheduler never picks a process that isn't "Runnable". This serves as the blueprint for the real `init_multitasking` implementation in `process.cpp`.

### **322. `src/kernel/HighLevelModel.cpp`**
*   **Detailed Full Summary**:
    **Integrated POSIX & Package Manager Model**.
    *   **System Integration**: This the most advanced model ("Stage 10"), combining the File System Truth model with Task and Thread models.
    *   **POSIX Emulation**: Implements `posix_open`, `posix_close`, and `posix_write` wrappers around the FS Truth model, providing a high-level API simulation.
    *   **Package Manager Skeleton**: Defines a basic `Package` registry with `install` and `uninstall` logic, showing how the kernel intends to manage application life-cycles in the future.
    *   **Multi-Threading**: Extends the execution model to support multiple `Thread` objects per `Task`, showing the relationship between PIDs and TIDs.

### **323. `src/kernel/MemoryModel.cpp`**
*   **Detailed Full Summary**:
    **Rigorous Memory Allocation Model**.
    *   **Ownership Tracking**: Explicitly tracks memory ownership using `MemoryOwner::Kernel` and `MemoryOwner::Process`. Every allocation is recorded in a `MemoryBlock` table.
    *   **Safety Checks**: Its `kfree_model` implementation is extremely strict—it triggers a `kernel_panic` if a process tries to free kernel-owned memory or if an invalid pointer is passed, preventing silent memory corruption during the "Stage 4" development phase.
    *   **Simulation**: Uses a fixed 1MB `g_kernel_heap` array to simulate memory behavior regardless of physical RAM availability.

### **324. `src/kernel/TaskModel.cpp`**
*   **Detailed Full Summary**:
    **Multitasking & IPC Logical Verification**.
    *   **Task Management**: Provides a simplified task table for the "Stage 9" development milestone.
    *   **IPC Modeling**: Implements a fixed-size `IPCQueue` (16 messages) for each task. It models the logic of `ipc_send_model` by copying "data pointers" between task queues, demonstrating the fundamental principles of message passing without needing full Virtual Memory isolation.

### **325. `src/kernel/Kernel.cpp`**
*   **Detailed Full Summary**:
    **Retro-OS Higher-Half Kernel Core & Orchestrator**.
    *   **Entry Point**: The `main()` function is the primary C++ entry after the assembly bootloader hands over control. It orchestrates the entire system startup sequence.
    *   **Initialization Sequence**:
        1.  **Early Hardware**: GDT, ISRs, Serial logging.
        2.  **Memory**: PMM (8MB bitmap), Paging (Higher-half mapping at 0xC0000000), Slab allocator.
        3.  **Advanced Hardware**: IRQs, Keyboard, Mouse, HPET, TSC.
        4.  **Filesystem**: FAT16, DevFS, and the final Phase 4 VFS.
        5.  **Networking**: PCI scanning for E1000 and starting the async Network Thread.
        6.  **Graphics**: BGA Linear Framebuffer (LFB) mapping and graphics subsystem initialization.
    *   **User-Space Transition**: spawns the `gui_main` kernel thread and the first user process `INIT.ELF`, finally entering an idle loop (`hlt`).
    *   **Bridges**: Contains various `extern "C"` bridges (like `sys_get_mouse`, `sys_open`) that provide the final hardware-backed implementations for the models described above.

### **326. `src/kernel/main.cpp`**
*   **Detailed Full Summary**:
    **Early C++ Bootstrap Test**.
    *   **Role**: A minimal entry point used during early development to verify that C++ features (Constructors, Destructors, `new` operator) are working correctly in the kernel environment.
    *   **Logic**: Creates a `TestClass` object on the heap, calls a method, and logs the results to the serial port. It serves as a "Smoke Test" for the C++ runtime support (`cpp_support.cpp`).

### **327. `src/kernel/process.cpp`**
*   **Detailed Full Summary**:
    **Retro-OS Process Management & Preemptive Scheduler Core**.
    *   **Architecture**: Implements the fundamental multitasking environment. It manages a circular linked-list `ready_queue` (Round Robin) and tracks the `current_process`.
    *   **Scheduling Strategy**:
        - **Preemption**: Uses a hardware timer interrupt to decrement `time_remaining`. When zero, it calls `schedule()` to switch tasks.
        - **Priority**: Implements a multilevel priority system where the scheduler searches for the "best" process (lowest numerical priority value) in the ready queue.
        - **States**: Manages complex lifecycle transitions: `RUNNING`, `READY`, `WAITING` (blocking on I/O), `SLEEPING` (timed wait), and `ZOMBIE` (waiting for parent `waitpid`).
    *   **Process Creation**:
        - **`create_kernel_thread`**: Spawns light-weight threads within the kernel address space.
        - **`create_user_process` (INIT)**: The primary bootstrap for user applications. It creates a new page directory, loads the ELF executable via `load_elf`, and sets up the ring-3 user stack.
        - **`fork_process`**: Implements standard Unix `fork`. It performs a deep clone of the page directory (`pd_clone`), duplicates the file descriptor table (incrementing ref counts), and sets up the child's kernel stack to return `0` via the `fork_child_return` trampoline.
    *   **Context Switching**: The `switch_task` assembly wrapper (called from `schedule`) saves current GPRs onto the kernel stack, switches `cr3` (Page Directory), and restores the GPRs of the target process, effectively jumping between execution contexts.
    *   **Advanced Features**: Supports "ASLR" (Address Space Layout Randomization) for the user stack and implements `pledges`/`unveils` security metadata (OpenBSD-style).

### **328. `src/kernel/syscall.cpp`**
*   **Detailed Full Summary**:
    **Kernel System Call Dispatcher & Implementation Hub**.
    *   **Mechanism**: The central entry point for all user-space requests. It handles the `int 0x80` interrupt, extracting the syscall number from `eax` and arguments from `ebx`, `ecx`, `edx`, etc.
    *   **Security Foundation**:
        - **Pointer Validation**: Every user-supplied pointer is vetted via `validate_user_pointer` to ensure it resides in the user's mapped range (above 0x20000000) and doesn't wrap around.
        - **Pledge/Unveil Enforcement**: Retro-OS implements a capability-style security model. Syscalls check bits in `current_process->pledges` (e.g., `PLEDGE_INET`, `PLEDGE_RPATH`) before execution. The `check_unveil` function verifies that file operations are within allowed path prefixes.
    *   **Major Syscall Implementations**:
        - **VFS Hub**: Maps `sys_open`, `sys_read`, `sys_write`, `sys_close`, `sys_seek` to the underlying VFS layer.
        - **Memory Management**: Implements `sys_sbrk` (traditional heap expansion) and `sys_mmap` (page-aligned memory mapping with PMM/VM).
        - **Networking**: Bridging user-space to the `socket` API, including specialized calls for `sys_http_get` and `sys_dns_resolve`.
        - **Graphics**: Provides `sys_get_framebuffer` and `sys_fb_swap`, allowing userspace GUI applications to interact with the kernel-managed BGA framebuffer.
        - **Process Control**: Implements `sys_execve`, `sys_waitpid`, `sys_kill`, and `sys_sigaction`.
    *   **Diagnostics**: Includes `sys_meminfo` and `sys_uname` for standard system monitoring tools.
    *   **GUI Terminal Bridge**: Specifically hooks `sys_print_serial` to also output strings to `gui_terminal_output`, ensuring console logs are visible in the graphical desktop.
 Kingston

### **329. `src/kernel/gdt.cpp`**
*   **Detailed Full Summary**:
    **Global Descriptor Table (GDT) & TSS Configuration**.
    *   **Architecture**: Establishes the memory segmentation model for the i386 processor. It defines 6 primary segments:
        - **Null Segment**: Required index 0.
        - **Kernel Code/Data**: Ring 0 segments encompassing the full 4GB address space.
        - **User Code/Data**: Ring 3 segments allowing applications to run with restricted privileges.
        - **Task State Segment (TSS)**: Index 5, used for hardware-assisted context switching support.
    *   **TSS Implementation**: Specifically sets up the `esp0` field in the TSS (`set_kernel_stack`). This is critical for multitasking; it tells the CPU where the kernel stack is located when transitioning from user mode (Ring 3) to kernel mode (Ring 0) during a syscall or interrupt.
    *   **Flush Logic**: Uses `gdt_flush` and `tss_flush` (assembly routines) to reload the segment registers and the Task Register (`LTR`) with the new table entries.

### **330. `src/kernel/idt.cpp`**
*   **Detailed Full Summary**:
    **Interrupt Descriptor Table (IDT) Foundation**.
    *   **Role**: Defines the "Gate" structures that map interrupt numbers (0-255) to their respective handler functions in `isr.cpp`.
    *   **Privilege Control**: Distinguishes between kernel gates (`set_idt_gate`, DPL 0) and user gates (`set_idt_gate_user`, DPL 3).
    *   **Syscall Support**: Specifically uses `set_idt_gate_user` for the `int 0x80` (128) vector, allowing user-space applications to trigger the syscall handler without causing a General Protection Fault.

### **331. `src/kernel/irq.cpp`**
*   **Detailed Full Summary**:
    **Interrupt Request (IRQ) Routing & Management**.
    *   **Dual Mode Design**: Supports both legacy **PIC (8259A)** and modern **APIC (Advanced Programmable Interrupt Controller)**.
    *   **APIC Integration**: Uses ACPI to detect the presence of the APIC. If found, it initializes the `LAPIC` (Local) and `IOAPIC` for high-performance interrupt delivery in a potentially multi-core environment.
    *   **PIC Fallback**: Includes the classic `irq_remap` logic to move IRQs 0-15 to vectors 32-47, preventing collisions with CPU internal exceptions (0-31).
    *   **Signal Hook**: The `irq_handler` is the last point of execution before returning to user mode. It calls `handle_signals(regs)` to ensure any signals generated during the interrupt (like a timer alarm) are processed immediately.

### **332. `src/kernel/isr.cpp`**
*   **Detailed Full Summary**:
    **Interrupt Service Routine (ISR) Dispatcher**.
    *   **Exception Handling**: Contains the master list of x86 exceptions (Page Fault, General Protection Fault, etc.) and a dedicated `isr_handler` to log them to the serial port with register dumps (EIP, CS, EFLAGS) for debugging.
    *   **Registration**: Provides `register_interrupt_handler`, allowing drivers (like Keyboard or Disk) to hook their specific logic into the interrupt stream.
    *   **Fault Recovery**: Specifically handles Page Faults (Vector 14) and logs the offending address from the `CR2` register. It includes logic to attempt signal delivery to user processes if they cause a crash, rather than panicking the entire kernel.

### **333. `src/kernel/apic.cpp`**
*   **Detailed Full Summary**:
    **Advanced Interrupt Controller Driver**.
    *   **Local APIC (LAPIC)**: Manages per-CPU interrupts and timer events. Implements `lapic_init` and `lapic_eoi`. It enables the APIC by setting the "Spurious Interrupt Vector" register bit 8.
    *   **I/O APIC**: Manages hardware interrupt redirection. It parses the ACPI MADT table to find ISO (Interrupt Source Overrides), such as the common mapping where IRQ 0 (Timer) is actually wired to GSI 2.
    *   **Redirection Table**: Implements `ioapic_set_irq` to program the 64-bit redirection entries. It supports level-triggered and active-low interrupt polarity as specified by ACPI flags.
    *   **Memory Mapping**: The `apic_map_hardware` function ensures the fixed MMIO regions for APIC communication (0xFEE00000 and 0xFEC00000) are correctly mapped in the kernel's page tables before use.

### **334. `src/kernel/pmm.cpp`**
*   **Detailed Full Summary**:
    **Physical Memory Manager (PMM)**.
    *   **Management Technique**: Uses a high-efficiency **Bitmap** to track individual 4KB physical frames. One bit represents one 4KB block (0 for free, 1 for used).
    *   **Scalability**: Dynamically calculates bitmap size based on detected RAM (initially set to 512MB in `Kernel.cpp`).
    *   **Allocation Algorithms**:
        - **`pmm_alloc_block`**: Scans the bitmap for the first set of 32 bits that isn't `0xFFFFFFFF`, then performs a bit-scan to find the specific free bit.
        - **`pmm_alloc_contiguous_blocks`**: Implements a First-Fit search for a sequence of free bits, essential for hardware drivers requiring larger buffers (like network ring buffers).
    *   **Region Control**: Supports `pmm_mark_region_used` to reserve low-memory regions (0-1MB), kernel code, and the PMM bitmap itself during early boot.

### **335. `src/kernel/paging.cpp`**
*   **Detailed Full Summary**:
    **Higher-Half Paging & Demand Paging Engine**.
    *   **Virtual Memory Layout**:
        - **Kernel Space (0xC0000000+)**: Maps the first 512MB of physical RAM to 0xC0000000 (Higher-half). Uses 2-level paging (Page Directory and Page Tables).
        - **Identity Mapping (0x0-0x20000000)**: Maintained temporarily for legacy hardware access and transitioning into protected mode.
    *   **Demand Paging Implementation**:
        - **`handle_demand_paging`**: The core logic triggered by transparent Page Faults.
        - **Optimization**: Instead of pre-allocating memory for process stacks or heaps, it waits for the process to actually access the memory. The handler then catches the fault, allocates a frame via PMM, maps it, and resumes execution—saving significant physical RAM for large processes.
        - **Segments**: Specifically handles demand mapping for Shared Memory (`SHM`), User Heaps, and User Stacks (around `0xB0000000`).
    *   **Protection**: Implements kernel-only regions for `_text` (code) and `_rodata` to catch accidental writes to the kernel binary.

### **336. `src/kernel/heap.cpp`**
*   **Detailed Full Summary**:
    **Hybrid Kernel Heap Allocator**.
    *   **Dual-Layer Architecture**:
        1. **Slab Layer**: Optimized for small allocations (up to 2048 bytes). It uses fixed-size caches to prevent fragmentation.
        2. **Buddy Layer**: Used for larger or page-aligned requests. Implements the Buddy Allocation algorithm to manage 4KB+ blocks.
    *   **Fault Tolerance**: Uses a custom header system with a "Magic Number" (`0xCAFEBABE`) and hidden "Back-Pointers" to detect double-frees and buffer overflows immediately.
    *   **Alignment**: Supports `kmalloc_real` with an `align` flag, which is crucial for allocating buffers that must start at a 4KB page boundary for DMA (Direct Memory Access).
    *   **VFS/Libc Bridge**: Provides the standard `malloc` and `free` symbols for kernel-space logic, making third-party C/C++ library integration (like MbedTLS) seamless.

### **337. `src/kernel/user.cpp`**
*   **Detailed Full Summary**:
    **User & Group Management Implementation**.
    *   **Architecture**: Implements a lightweight identity system within the kernel. It provides the standard POSIX `passwd` and `group` database functionality without requiring external `/etc/passwd` files during early boot.
    *   **Database**:
        - **Static Store**: Uses pre-allocated arrays (`passwd_db`, `group_db`) to store user information (UID, GID, Home Dir, Shell).
        - **Default Entities**: Automatically initializes `root` (UID 0) and `nobody` (UID 65534) upon first access.
    *   **POSIX API**: Implements `getpwnam`, `getpwuid`, `getgrnam`, etc., searching the internal database.
    *   **Multi-User Security**:
        - **UID Transitions**: Implements strict `setuid`, `seteuid`, and `setresuid` logic. Non-root processes are prevented from assuming a UID other than their original real or saved UID.
        - **Supplementary Groups**: Supports up to 16 supplementary groups per process via `getgroups` and `setgroups`.

### **338. `src/kernel/wait_queue.cpp`**
*   **Detailed Full Summary**:
    **Sleep/Wakeup Primitives (Wait Queues)**.
    *   **Synchronization**: Provides the fundamental mechanism for blocking processes. It allows a process to voluntarily stop execution until a specific condition (like data arriving on a socket) is met.
    *   **Logic**:
        - **`sleep_on`**: Critical section. Disables interrupts, adds the `current_process` to a linked list, sets its state to `PROCESS_WAITING`, and calls the scheduler to yield the CPU.
        - **`wake_up` / `wake_up_all`**: Removes one or all entries from the queue and sets their state back to `PROCESS_READY`, allowing them to be scheduled again.
    *   **Safety**: Uses hardware CLI/STI (Clear/Set Interrupts) to ensure the queue management is atomic, preventing race conditions where a process might sleep forever if the wakeup signal arrives during the sleep transition.

### **339. `src/kernel/e1000.cpp`**
*   **Detailed Full Summary**:
    **Intel E1000 Gigabit Ethernet Driver**.
    *   **Hardware Interface**: A high-performance driver designed for QEmu's `e1000` emulation. It uses **MMIO** (Memory Mapped I/O) for register access and **DMA** (Direct Memory Access) for high-speed packet transfer.
    *   **DMA Design**:
        - **Descriptors Rings**: Sets up 32-entry RX and TX "Descriptor Rings". These rings are 16-byte aligned as required by the Intel specification.
        - **Buffer Ownership**: Implements the hardware/software handoff logic using `RDH` (Head) and `RDT` (Tail) registers.
    *   **Initialization Sequence**:
        - **Bus Mastering**: Enables the PCI Bus Master bit to allow the card to independently access physical RAM.
        - **Reset**: Performs a hardware reset via the `E1000_CTRL` register.
        - **Filter Logic**: Configures the Broadcast Accept Mode (`BAM`) and Multicast Promiscuous Enable (`MPE`) to ensure the OS receives required protocol packets (ARP/DHCP).
    *   **Polling Engine**: Currently implements a high-performance polling receiver (`e1000_receive`) used by the kernel's network thread to minimize interrupt overhead in the current single-threaded polling model.

### **340. `src/boot/boot.asm`**
*   **Detailed Full Summary**:
    **FAT16-Compatible First-Stage Bootloader**.
    *   **Dual Identity**: Functions as both a standard x86 Master Boot Record (MBR) and a valid FAT16 BIOS Parameter Block (BPB). This allows the boot disk to be mounted and read by modern operating systems (labeled "MYOS").
    *   **Bootstrap Process**:
        1.  **Environment Setup**: Initializes segments (`ds`, `es`, `ss`) and sets the early stack at `0x7C00`.
        2.  **A20 Enable**: Unlocks the A20 gate via the PS/2 controller (port `0x92`) to allow access to even-numbered megabytes of RAM.
        3.  **Kernel Loading**: Implements a robust LBA-style loader within the `load_kernel` routine. It reads 1300 sectors (~650KB) from the disk starting at sector 2 and places them at physical address `0x8000`.
    *   **Progress Indicators**: Features a dot-based progress bar (`.` printed every 16 sectors) to provide visual feedback during the BIOS disk read process.

### **341. `src/boot/disk.asm`**
*   **Detailed Full Summary**:
    **Low-Level Disk I/O Primitives**.
    *   **BIOS Interrupts**: Wraps the `INT 0x13, AH=02h` BIOS call. It handles the mapping of Cylinder-Head-Sector (CHS) parameters passed from the main loader.
    *   **Error Handling**: Includes a fallback `.error` routine that prints "Disk Error!" and halts the CPU if the BIOS fails to read from the media (common in floppy or misconfigured IDE emulations).

### **342. `src/boot/gdt.asm`**
*   **Detailed Full Summary**:
    **Early Boot GDT Definitions**.
    *   **Temporary Segments**: Defines the initial Global Descriptor Table used only during the transition from Real Mode (16-bit) to Protected Mode (32-bit).
    *   **Flat Memory Model**: Sets up two 4GB segments (Code and Data) with base `0x0` and limit `0xFFFFF` (with 4KB granularity). This allows the kernel code to access all available physical memory immediately after the switch.

### **343. `src/boot/switch_pm.asm`**
*   **Detailed Full Summary**:
    **Protected Mode Transition Logic**.
    *   **Switch Sequence**:
        1.  **Disable Interrupts**: `cli` to prevent BIOS interrupts from crashing the CPU during segment changes.
        2.  **Load GDT**: `lgdt` to point the CPU to the early GDT defined in `gdt.asm`.
        3.  **Bit Flip**: Sets the PE (Protection Enable) bit in `CR0`.
        4.  **Far Jump**: Performs a `jmp CODE_SEG:init_pm`. This is mandatory to clear the CPU's instruction prefetch queue and load the new Code Segment register.
    *   **Post-Transition Stack**: Sets up a large temporary stack at `0x1E000000` (480MB mark) to ensure it doesn't collide with the kernel binary or early BSS regions.

### **344. `src/boot/kernel_entry.asm`**
*   **Detailed Full Summary**:
    **Higher-Half Kernel Initializer & Paging Bootstrap**.
    *   **Role**: This is the first code executed that "knows" about the kernel's final 0xC0000000 mapping.
    *   **Early Paging Setup**:
        - **Recursive Mapping**: Creates a `BootPageDirectory` and `BootPageTables` to map the first 128MB of physical RAM twice: once at `0x0` (Identity) and once at `0xC0000000`. This trick allows the EIP (instruction pointer) to continue executing smoothly after paging is enabled.
        - **Control Register Trigger**: Loads `CR3` with the directory address and sets the paging bit in `CR0`.
    *   **The Final Jump**: Uses `lea eax, [higher_half]` followed by a jump to transition into the 0xC0000000 address space permanently.
    *   **BSS Guard**: Allocates a 64KB kernel stack (`stack_top`) and finally calls the C++ `main` function.

### **345. `src/lib/string.cpp`**
*   **Detailed Full Summary**:
    **Optimized String & Memory Utility Library**.
    *   **Architecture**: Provides the base implementation of the standard C library string functions (`memcpy`, `memset`, `strcmp`, etc.) customized for the 32-bit x86 architecture.
    *   **Performance Optimizations**:
        - **Aligned `memcpy`**: Specifically detects if both destination and source are 32-bit aligned. If so, it performs copy operations using 4-byte `uint32_t` tokens instead of single byte reads/writes, significantly speeding up large memory moves (like framebuffer copies).
        - **Overlap-Aware `memmove`**: Correctlly handles overlapping memory regions by deciding whether to copy forwards or backwards, preventing data corruption during shift operations.
    *   **Formatting Utilities**: Includes a localized `itoa` implementation that supports multiple bases (decimal, hex, binary), allowing the kernel to log memory addresses and status codes easily.

### **346. `src/lib/Assertions.cpp`**
*   **Detailed Full Summary**:
    **Kernel-Wide Assertion & Panic Handler**.
    *   **Mechanism**: Implements the `crash` function (used by the `ASSERT` macro). When a critical invariant is violated, it intercepts execution and redirects output to the serial console.
    *   **Diagnostic Output**: Logs the specific failure message, the source file name, and the exact function name where the crash occurred. This provides developers with immediate context for kernel-level debugging.
    *   **Secure Halt**: Once a crash is initiated, it disables all interrupts (`cli`) and enters an infinite `hlt` loop, safely stopping the system to prevent any further memory corruption or disk I/O.

### **347. `apps/sh.cpp`**
*   **Detailed Full Summary**:
    **Retro-OS Shell (Command Interpreter)**.
    *   **Architecture**: A Unix-like shell that acts as the primary user interface for the system's character-based console.
    *   **Core Logic**:
        - **Built-in Commands**: Implements high-speed execution for `cd` (changing working directory), `pwd`, `clear`, `help`, and `exit`.
        - **Process Execution**: For any command not recognized as a built-in, it performs a `fork()` and `execve()`. It automatically searches for executables in `/apps/`, `/bin/`, and the root directory, appending `.elf` if missing.
        - **Job Control**: Uses `wait()` to block the shell execution until the child process completes, ensuring task serialization.
    *   **ANSI Support**: Outputs ANSI escape codes (like `\x1b[1;36m`) for colored output, compatible with the system's terminal emulators.

### **348. `apps/ls.cpp`**
*   **Detailed Full Summary**:
    **Directory Listing Utility**.
    *   **VFS Integration**: Uses the `syscall_readdir` interface to iterate through files in a directory. It defaults to the current working directory (`getcwd`) if no path is provided.
    *   **Visual Enhancements**: Implements simple terminal coloring—directories are rendered in Cyan, while regular files use the default text color. It explicitly filters out `.` and `..` hidden entries to keep output clean.

### **349. `apps/cat.cpp`**
*   **Detailed Full Summary**:
    **File Concatenation & Output Utility**.
    *   **Streaming Multi-File Logic**: Highly robust implementation that can consume multiple file paths as arguments. It opens each file sequentially, reads data in 1KB chunks to minimize memory pressure, and pipes the output directly to file descriptor 1 (`stdout`).
    *   **Error Reporting**: Gracefully handles non-existent files by printing an error message and continuing to the next argument in the list.

### **350. `apps/terminal.cpp`**
*   **Detailed Full Summary**:
    **GUI Terminal Emulator Application**.
    *   **Complex Subsystem**: This is a standalone graphical application that provides a windowed terminal environment within the Retro-OS desktop.
    *   **Internal Components**:
        - **Font Engine**: Contains an embedded 8x8 monochrome bitmap font (subset of 128 characters).
        - **PTY Bridge**: Uses the `sys_pty_create` syscall to generate a pseudoterminal pair. It forks the system shell (`/apps/sh.elf`) and connects the shell's standard I/O to the slave end of the PTY.
        - **ANSI Parser**: Implements a localized state machine to interpret ANSI escape sequences (e.g., cyan coloring) and update the terminal's internal `current_fg` color state.
        - **Rendering Logic**: Performs manual pixel blitting using the `IPCClient::fill_rect` method to draw characters and a blinking cursor.
        - **Input Handling**: Translates GUI key events into character streams which are written to the master end of the PTY for the shell to consume.

### **351. `apps/init.cpp`**
*   **Detailed Full Summary**:
    **Minimal System Init Process**.
    *   **Role**: The first user-space process spawned during kernel boot. Its primary purpose in the current architecture is to keep the system active and provide a base execution context.
    *   **Logic**: Simply logs "Minimal OS Init Started" to the system console and enters an infinite loop, sleeping for 100ms intervals to prevent CPU starvation.

### **352. `apps/notepad.cpp`**
*   **Detailed Full Summary**:
    **Graphical Text Editor Application**.
    *   **Architecture**: A robust, object-oriented GUI application that demonstrates the system's ability to handle complex user input and file I/O simultaneously.
    *   **Text Processing Engine**: Implements a `TextBuffer` class that manages an array of dynamically allocated heap strings. It supports:
        - **Line Manipulation**: Inserting and removing lines at arbitrary indices.
        - **Horizontal Scrolling**: Handled by tracking `cursor_col`.
        - **Vertical Scrolling**: Implemented via `scroll_y`, following the cursor as it moves between lines.
    *   **GUI System Integration**:
        - **Menus**: Provides interactive dropdown menus (File, Edit, Help) with mouse-collision detection.
        - **Keyboard Hooks**: Supports standard typing and specialized shortcuts like **Ctrl+S** (mapped to key code 19) for quick saving.
        - **Visual Feedback**: Includes a blinking red cursor and a top-aligned toolbar with status messages (e.g., "Saved!", "New Doc Created").

### **353. `apps/minimal_os_api.cpp`**
*   **Detailed Full Summary**:
    **User-Space OS Abstraction Layer (SDK)**.
    *   **Role**: Acts as the "Standard Library" for Retro-OS applications, providing a clean C/C++ interface to underlying kernel syscalls and IPC protocols.
    *   **Graphics Wrappers**: Simplifies window management by providing `gfx_rect`, `gfx_clear`, and `gfx_text`. It internally manages a global `IPCClient` instance, automatically connecting and creating a window if one doesn't exist (`ensure_ipc`).
    *   **Input Polling**: Provides `mouse_poll` and `key_poll` functions which encapsulate the message-passing logic of the compositor, making input handling as easy as calling a single function for the developer.
    *   **Object-Oriented FS**: Implements `os_fs_readdir` which returns a modern `std::vector<FileEntry>`, hiding the complexities of the `dirent` struct and syscall indexing.

### **354. `apps/ping.cpp`**
*   **Detailed Full Summary**:
    **Network Diagnostic Utility**.
    *   **Logic**: Uses the `sys_net_ping` syscall to send ICMP Echo Request packets to a target IP address. It demonstrates how userspace applications can interact with the kernel's network stack without needing to craft raw packets manually.

### **355. `apps/tcptest.cpp`**
*   **Detailed Full Summary**:
    **TCP Protocol Verification Tool**.
    *   **Mechanism**: Calls the `sys_tcp_test` kernel primitive. It verifies the reliability of the TCP state machine (handshake, data transfer, and Fin/Ack sequence) against a mock or real loopback interface, ensuring the networking stack remains stable after kernel updates.

### **356. `src/kernel/interrupt.asm`**
*   **Detailed Full Summary**:
    **Low-Level Interrupt Stubs & Context Management**.
    *   **Architecture**: Provides the essential assembly linkage between raw CPU interrupts and high-level C++ handlers.
    *   **Common Stubs**:
        - **`isr_common_stub`**: Handles CPU exceptions. It saves the full state of all General Purpose Registers (`pusha`), saves the current segment registers, and switches the CPU to the kernel data segment (`0x10`) before calling the C++ `isr_handler`.
        - **`irq_common_stub`**: Similar to ISR, but specifically for hardware interrupts. It ensures the CPU returns to the correct execution mode after the hardware event is serviced.
    *   **Automation**: Uses NASM macros (`ISR_NOERRCODE`, `ISR_ERRCODE`, `IRQ`) to generate boilerplate code for all 256 possible interrupt vectors, ensuring consistent stack frames across the kernel.

### **357. `src/kernel/process_asm.asm`**
*   **Detailed Full Summary**:
    **Task Switching & Fork Return Routines**.
    *   **`switch_task`**: The heart of the multitasking engine. It performs the "Magic" of swapping execution contexts.
        - **State Storage**: Pushes Callee-saved registers (`EBX`, `ESI`, `EDI`, `EBP`) and `EFLAGS` onto the current task's stack.
        - **Stack Pivot**: Dynamically updates the `ESP` register to point to the new task's stack.
        - **Address Space Switch**: Checks if the target task has a different Page Directory (`CR3`) and performs the hardware context switch if necessary.
        - **Restoration**: Pops the registers from the *new* stack and returns into the target task's code.
    *   **`fork_child_return`**: A specialized entry point for newly created child processes. It bypasses the standard `switch_task` logic to force-load a `registers_t` frame and return exactly `0` to the user-space process, fulfilling the POSIX `fork()` contract.

### **358. `src/kernel/gdt_asm.asm`**
*   **Detailed Full Summary**:
    **Segmentation Flush & TSS Activation**.
    *   **`gdt_flush`**: Forces the CPU to update its internal segment caches. It uses the `lgdt` instruction to load the new table pointer and performs a "Far Jump" (`jmp 0x08:.flush`) to reload the Code Segment (`CS`) register, which cannot be modified using standard `mov` instructions.
    *   **`tss_flush`**: Activates the Task State Segment by loading the `LTR` (Load Task Register) with selector `0x28`. This enabling bit allows the CPU to find the kernel stack during Ring-3-to-Ring-0 transitions.

### **359. `inject_wallpaper.py`**
*   **Detailed Full Summary**:
    **Custom FAT16 Disk Image Manipulation Utility**.
    *   **Purpose**: A localized Python script that bypasses the need for semi-privileged `mtools` or loopback mounting. It manually "Injects" specialized files (ELFs, Bitmaps, and Data blobs) directly into the `os.img` binary.
    *   **FAT16 Implementation**:
        - **Structures**: Re-implements the FAT16 disk geometry (Sector Size 512, 4096 Reserved Sectors, 2 FAT tables).
        - **Cluster Management**: Calculates the exact byte offsets for the FAT Table (`FAT_OFFSET`), Root Directory (`ROOT_OFFSET`), and Data Region (`DATA_OFFSET`).
    *   **Logic Flow**:
        1.  **Bitstream Formatting**: Uses the Python `struct` module to pack data into C-compatible byte arrays (11-byte 8.3 filenames, directory entry attributes, etc.).
        2.  **FAT Chain Linker**: Automatically spans files across multiple clusters and updates the File Allocation Table with correct "Next Cluster" pointers, terminating with `0xFFFF`.
        3.  **Bootstrap Persistence**: It initializes a dummy `TRUTH.DAT` file (300KB) to provide a reserved space on the physical disk for the kernel's self-testing persistence model.
    *   **Deployment**: Automatically called by `build.sh` as the final step of the build pipeline, ensuring every ELF application compiled is ready for execution upon system boot.
