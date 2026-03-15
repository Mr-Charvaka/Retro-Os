#include "pe_loader.h"
#include "../drivers/serial.h"
#include "../include/pe.h"
#include "../include/string.h"
#include "../include/vfs.h"
#include "heap.h"
#include "memory.h"
#include "pmm.h"
#include "vm.h"

// --- WinAPI Bridge Functions ---
// These are target addresses for the IAT thunks

extern "C" void winapi_exit_process(uint32_t code) {
  serial_log("WINAPI: ExitProcess called.");
  // Map to native exit
  asm volatile("mov $1, %%eax; mov %0, %%ebx; int $0x80" : : "r"(code));
}

extern "C" void winapi_write_console(void *handle, const char *buffer,
                                     uint32_t len, uint32_t *written,
                                     void *reserved) {
  (void)handle;
  (void)reserved;
  serial_log("WINAPI: WriteConsoleA called.");
  // Map to serial/stdout
  for (uint32_t i = 0; i < len; i++) {
    // Basic write loop or syscall
  }
  if (written)
    *written = len;
  serial_log(buffer);
}

static uint32_t resolve_import(const char *dll, const char *func) {
  if (strcmp(dll, "kernel32.dll") == 0 || strcmp(dll, "KERNEL32.dll") == 0) {
    if (strcmp(func, "ExitProcess") == 0)
      return (uint32_t)winapi_exit_process;
    if (strcmp(func, "WriteConsoleA") == 0)
      return (uint32_t)winapi_write_console;
  }
  return 0;
}

uint32_t load_pe(const char *filename, uint32_t *top_address) {
  serial_log("PE: Loading file via VFS...");
  serial_log(filename);

  vfs_node_t *node = vfs_resolve_path(filename);
  if (node == 0) {
    serial_log("PE ERROR: File not found.");
    return 0;
  }

  uint8_t *buffer = (uint8_t *)kmalloc(node->size);
  vfs_read(node, 0, buffer, node->size);

  IMAGE_DOS_HEADER *dos_hdr = (IMAGE_DOS_HEADER *)buffer;
  if (dos_hdr->magic != MZ_MAGIC) {
    serial_log("PE ERROR: Invalid MZ Magic.");
    kfree(buffer);
    return 0;
  }

  IMAGE_NT_HEADERS32 *nt_hdr = (IMAGE_NT_HEADERS32 *)(buffer + dos_hdr->lfanew);
  if (nt_hdr->Signature != PE_MAGIC) {
    serial_log("PE ERROR: Invalid PE Signature.");
    kfree(buffer);
    return 0;
  }

  serial_log("PE: Valid PE header found.");
  uint32_t entry_point = nt_hdr->OptionalHeader.ImageBase +
                         nt_hdr->OptionalHeader.AddressOfEntryPoint;
  serial_log_hex("PE: Entry Point (RVA): ",
                 nt_hdr->OptionalHeader.AddressOfEntryPoint);
  serial_log_hex("PE: Entry Point (VA): ", entry_point);

  uint32_t max_addr = 0;
  IMAGE_SECTION_HEADER *section =
      (IMAGE_SECTION_HEADER *)((uint8_t *)&nt_hdr->OptionalHeader +
                               nt_hdr->FileHeader.SizeOfOptionalHeader);

  for (int i = 0; i < nt_hdr->FileHeader.NumberOfSections; i++) {
    uint32_t start_addr =
        nt_hdr->OptionalHeader.ImageBase + section[i].VirtualAddress;
    uint32_t mem_size = section[i].VirtualSize;
    uint32_t file_size = section[i].SizeOfRawData;

    serial_log_hex("PE: Loading section: ", (uint32_t)section[i].Name);
    serial_log_hex("PE: Target VA: ", start_addr);

    if (start_addr + mem_size > max_addr) {
      max_addr = start_addr + mem_size;
    }

    // Map pages
    uint32_t start_page = start_addr & 0xFFFFF000;
    uint32_t end_page = (start_addr + mem_size + 0xFFF) & 0xFFFFF000;

    for (uint32_t page = start_page; page < end_page; page += 4096) {
      if (vm_get_phys(page) == 0) {
        void *phys = pmm_alloc_block();
        if (!phys) {
          serial_log("PE ERROR: Memory allocation failed.");
          kfree(buffer);
          return 0;
        }
        vm_map_page((uint32_t)phys, page, 7); // User/Read/Write
      }
    }

    // Copy raw data from file
    if (file_size > 0) {
      memcpy((void *)start_addr, buffer + section[i].PointerToRawData,
             file_size);
    }

    // Zero remaining memory for the section (BSS alike)
    if (mem_size > file_size) {
      memset((void *)(start_addr + file_size), 0, mem_size - file_size);
    }
  }

  // --- IAT Resolution (Crucial for DLLs) ---
  if (nt_hdr->OptionalHeader.DataDirectory[1].Size > 0) {
    uint32_t import_rva =
        nt_hdr->OptionalHeader.DataDirectory[1].VirtualAddress;
    IMAGE_IMPORT_DESCRIPTOR *import_desc =
        (IMAGE_IMPORT_DESCRIPTOR *)(buffer + import_rva); // Simple map for demo

    while (import_desc->Name != 0) {
      const char *dll_name = (const char *)(buffer + import_desc->Name);
      serial_log("PE: Resolving imports for ");
      serial_log(dll_name);

      // Thunk arrays
      uint32_t *thunk = (uint32_t *)(nt_hdr->OptionalHeader.ImageBase +
                                     import_desc->FirstThunk);
      uint32_t *orig_thunk =
          (uint32_t *)(buffer + (import_desc->OriginalFirstThunk
                                     ? import_desc->OriginalFirstThunk
                                     : import_desc->FirstThunk));

      while (*orig_thunk != 0) {
        if (!(*orig_thunk & 0x80000000)) { // Import by name
          const char *func_name = (const char *)(buffer + (*orig_thunk) + 2);
          uint32_t addr = resolve_import(dll_name, func_name);
          if (addr != 0) {
            *thunk = addr; // Replace IAT entry with our bridge
          } else {
            serial_log("PE WARNING: Unresolved import: ");
            serial_log(func_name);
          }
        }
        thunk++;
        orig_thunk++;
      }
      import_desc++;
    }
  }

  if (top_address) {
    *top_address = (max_addr + 0xFFF) & 0xFFFFF000;
  }

  kfree(buffer);
  return entry_point;
}
