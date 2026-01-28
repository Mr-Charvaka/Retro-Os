<div align="center">

# 👾 Retro Pixel OS
### *A Hobby Operating System with a Pixel-Art Soul*

![Retro Pixel OS](https://img.shields.io/badge/OS-Retro%20Pixel-blueviolet?style=for-the-badge)
![Version](https://img.shields.io/badge/version-0.1.0-orange?style=for-the-badge)
![Architecture](https://img.shields.io/badge/arch-x86-lightgrey?style=for-the-badge)
![License](https://img.shields.io/badge/license-MIT-green?style=for-the-badge)
![Made With](https://img.shields.io/badge/made%20with-C%20%7C%20ASM%20%7C%20C++-red?style=for-the-badge)

🚀 **A retro-inspired operating system built from scratch**
🖥️ **Pixel UI • Custom Kernel • HTTPS Support • Terminal • Apps**
🧠 **Perfect for OS dev learners & low-level hackers**

</div>

---

## 🧠 What is Retro Pixel OS?

**Retro Pixel OS** is a **from-scratch hobby operating system** designed to:
- Teach **low-level OS development** (bare-metal)
- Recreate the **feel of early computing** with a modern twist
- Provide a **Pixel-art GUI** environment
- Support **HTTPS communication** via integrated mbedTLS
- Run inside the **QEMU** emulator

---
⚠️ **Educational Project**: This is a learning OS. It is NOT a Linux distribution. No standard C/C++ libraries are used; every component, from the memory manager to the network stack, is custom-built or manually integrated for the bare-metal environment.
---

## ✨ Key Features

- **Custom Bootloader**: Hand-rolled x86 assembly.
- **Micro-Kernel**: Multi-tasking, memory management, and interrupt handling.
- **Pixel GUI**: A vibrant, retro-styled graphical user interface.
- **HTTPS Stack**: Native HTTPS support via mbedTLS glue logic.
- **App Suite**: Terminal (`sh`), Notepad, File Manager, Calculator, Ping, and more.
- **Custom Libs**: Proprietary implementation of core C/C++ headers and functions.

---

## 🛠 Prerequisites (Windows / Linux)

To build and run Retro Pixel OS, you need a Unix-like environment. On Windows, we recommend **WSL (Ubuntu)**.

### 1. Development Tools
| Tool | Purpose |
|------|---------|
| **GCC/G++** | C/C++ Compiler (32-bit multilib) |
| **NASM** | Assembly Compiler |
| **Make** | Build automation |
| **QEMU** | x86 System Emulator |
| **Python 3** | Scripting for asset injection |
| **Git** | Version Control |

---

## 🚀 Installation & Setup

Follow these steps exactly to get the OS running on your machine.

### Step 1: Prepare the Environment (WSL Users)
If you are on Windows, open PowerShell as Administrator and run:
```powershell
wsl --install
```
*Restart your computer if prompted.* Then open your **Ubuntu** terminal.

### Step 2: Install Dependencies
Copy and paste this command into your terminal:
```bash
sudo apt update && sudo apt install -y build-essential g++-multilib nasm qemu-system-x86 mtools python3 git
```

### Step 3: Clone the Repository
```bash
git clone https://github.com/Mr-Charvaka/Retro-Os.git
cd Retro-Os
```

### Step 4: Build the OS Image
The build script compiles the bootloader, kernel, and apps, then bundles them into `os.img`.
```bash
# Make the script executable
chmod +x build.sh

# Run the build
./build.sh
```
*You should see a "Build Successful: os.img" message.*

### Step 5: Launch the OS
Run the OS using QEMU:
```bash
qemu-system-i386 -drive format=raw,file=os.img -serial stdio -m 512
```

---

## 🎮 Inside the OS

Once the GUI loads, you can use the following terminal commands:

| Command | Description |
|---------|-------------|
| `ls` | List files in the current directory |
| `sh` | Open the shell |
| `calc 5 + 5` | Use the built-in calculator |
| `ping [ip]` | Test network connectivity |
| `notepad` | Basic text editing |
| `df` | View disk frequency/info |
| `init` | Restart system services |

---

## 📂 Project Structure

- `/src/boot`: Bootloader and Kernel entry code (ASM).
- `/src/kernel`: Core kernel logic (Memory, Tasks, Interrupts, Networking).
- `/src/include`: Custom system headers and mbedTLS configurations.
- `/apps`: Source code for user-mode applications.
- `/assets`: Wallpapers and UI resources.

---

## 📜 License & Credits

Distributed under the **MIT License**. Created with ❤️ by Mr-Charvaka.

---

⭐ **Star the repo if you like it — Happy Hacking!** 👾
