[BITS 32]
section .text

; ============================================================
; SERIAL TRACE MACRO
; Sends a single character to COM1 (0x3F8)
; No memory access, only registers.
; ============================================================
%macro SERIAL_TRACE 1
    push eax
    push edx
    mov al, %1
    mov dx, 0x3F8
    out dx, al
    pop edx
    pop eax
%endmacro

global cpuid_edx_feat
cpuid_edx_feat:
    push ebx
    push ecx
    mov eax, 1
    cpuid
    mov eax, edx
    pop ecx
    pop ebx
    ret

global cpuid_ext_edx_feat
cpuid_ext_edx_feat:
    push ebx
    push ecx
    mov eax, 0x80000001
    cpuid
    mov eax, edx
    pop ecx
    pop ebx
    ret

global pae_enable
pae_enable:
    push ebp
    mov ebp, esp
    pushad
    pushfd
    cli

    SERIAL_TRACE 'A' ; Entered pae_enable

    ; Step 1: Capture pdpt_phys argument [ebp+8] before stack transition
    mov edi, [ebp + 8]   ; edi = pdpt_phys

    ; Step 2: Transition Stack to Physical
    sub esp, 0xC0000000
    SERIAL_TRACE 'B' ; Stack Adjusted

    ; Step 3: Jump to Physical Trampoline
    lea eax, [.pae_trampoline_phys]
    sub eax, 0xC0000000
    jmp eax

.pae_trampoline_phys:
    SERIAL_TRACE 'C' ; In Physical Trampoline

    ; 1. Disable paging (CR0.PG = 0)
    mov eax, cr0
    and eax, ~(1 << 31)
    mov cr0, eax
    SERIAL_TRACE 'D' ; Paging Disabled

    ; 2. Enable PAE in CR4 (CR4.PAE = 1)
    mov eax, cr4
    or  eax, (1 << 5)
    ; PGE (Global Page Enable) skip for now to avoid CPU-specific faults
    mov cr4, eax
    SERIAL_TRACE 'E' ; PAE Bit Enabled

    ; 3. Load PDPT address into CR3
    mov eax, edi
    mov cr3, eax
    SERIAL_TRACE 'F' ; PDPT Loaded into CR3

    ; 4. Re-enable paging (CR0.PG = 1 + CR0.WP = 1)
    mov eax, cr0
    or  eax, (1 << 31)
    or  eax, (1 << 16) ; Write Protect
    mov cr0, eax
    SERIAL_TRACE 'G' ; Paging Re-enabled (PAE Mode active)

    ; Step 4: Transition Stack back to Virtual
    add esp, 0xC0000000
    SERIAL_TRACE 'H' ; Stack Virtualized

    ; Step 5: Jump back to Higher Half
    lea eax, [.pae_high_half]
    jmp eax

.pae_high_half:
    SERIAL_TRACE 'I' ; Back in Higher Half

    ; Flush TLB
    mov eax, cr3
    mov cr3, eax
    SERIAL_TRACE 'J' ; TLB Flushed. Done.

    popfd
    popad
    pop ebp
    ret

global pae_invlpg_asm
pae_invlpg_asm:
    mov eax, [esp + 4]
    invlpg [eax]
    ret

global pae_flush_tlb_asm
pae_flush_tlb_asm:
    mov eax, cr3
    mov cr3, eax
    ret
