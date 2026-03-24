# 🏗️ Retro-OS System Architecture

![Retro-OS Architecture Visual](Demo/Architecture_Visual.png)

This flowchart illustrates the core architecture of Retro-OS, showing how the various subsystems—from the low-level bootloader to high-level GUI applications—interact with each other.

```mermaid
graph TD
    %% Hardware Layer
    subgraph Hardware ["💻 Hardware Layer"]
        CPU["x86 CPU (i386)"]
        RAM["Physical Memory"]
        Storage["IDE/AHCI Hard Drive"]
        NIC["E1000 Network Card"]
        Graphics["BGA/VBE Graphics Card"]
        Input["Keyboard & Mouse"]
    end

    %% Boot & Low Level
    subgraph Boot ["🏁 Boot & Initialization"]
        MBR["boot.asm (Master Boot Record)"]
        Entry["kernel_entry.asm (Higher-Half Transition)"]
        GDT_IDT["GDT / IDT Setup"]
        Paging["Paging (Identity -> Higher-Half)"]
    end

    %% Kernel Core
    subgraph KernelCore ["🧠 Kernel Core Subsystems"]
        PMM["Physical Memory Manager"]
        VMM["Virtual Memory Manager (Paging)"]
        Slab["Slab / Buddy Allocator"]
        Scheduler["Multitasking & Scheduler"]
        Syscall["Syscall Interface"]
    end

    %% Drivers
    subgraph Drivers ["⚙️ Hardware Drivers"]
        FAT16["FAT16 Driver"]
        Serial["Serial Logging"]
        Timer["PIT / HPET / TSC Calibrator"]
        BGA["BGA Graphics Driver"]
        E1000["E1000 NIC Driver"]
    end

    %% Higher Level Services
    subgraph OS_Services ["🛠️ OS Services & Stacks"]
        VFS["Virtual File System (VFS)"]
        NetStack["NetStack (IP/TCP/UDP)"]
        TLS["mbedTLS (HTTPS/SSL)"]
        IPC["POSIX IPC (Semaphores/Queues)"]
    end

    %% GUI & Browser
    subgraph UI_System ["🖥️ Graphical Subsystem"]
        DoubleBuffer["Double Buffered Renderer"]
        GUI_Mgr["GUI Window Manager"]
        BrowserEngine["HTML5 Parser & CSS Engine"]
    end

    %% User Space
    subgraph UserSpace ["🚀 User Applications"]
        Init["INIT.ELF (System Initializer)"]
        Shell["SH.ELF (CLI Shell)"]
        Apps["Notepad, Word, File Explorer"]
    end

    %% Connections
    MBR --> Entry
    Entry --> PMM
    Entry --> VMM
    Entry --> GDT_IDT
    
    PMM & VMM --> Slab
    Slab --> KernelCore
    
    KernelCore --> Drivers
    Drivers --> Hardware
    
    VFS --> FAT16
    NetStack --> E1000
    TLS --> NetStack
    
    Syscall --> OS_Services
    Syscall --> KernelCore
    
    UserSpace --> Syscall
    
    UI_System --> DoubleBuffer
    DoubleBuffer --> BGA
    BrowserEngine --> TLS
    BrowserEngine --> VFS
    
    GUI_Mgr --> UI_System
    Init --> GUI_Mgr
    Init --> Shell
```

### 🔍 Architectural Highlights:

1.  **Higher-Half Design**: The kernel lives at `0xC0000000`, separating kernel space from user space, which simplifies memory protection and system call handling.
2.  **Modular Drivers**: Hardware interaction is abstracted. For example, the **VFS** allows the OS to interact with files without caring whether they are on a FAT16 drive or a network-mounted resource.
3.  **Modern Web Capability**: Unlike many hobbyist OSes, Retro-OS includes a **TLS (mbedTLS)** layer combined with **ALPN/SNI** support, allowing it to communicate with modern HTTPS servers.
4.  **Double Buffering**: All graphics are rendered to an off-screen buffer (`back_buffer`) and swapped to the hardware (`screen_buffer`) in a single pass to eliminate flicker.
5.  **POSIX Layer**: Provides a bridge for standard C/C++ applications, supporting semaphores, message queues, and standard file operations.
