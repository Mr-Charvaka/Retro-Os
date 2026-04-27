[BITS 32]
global switch_task
global fork_child_return
global user_mode_entry_asm

; Fork child return stub - called when a forked child is first scheduled
; The child's stack has a registers_t frame ready for iret
; Stack layout when we get here:
;   [registers_t frame] <- ESP points here after switch_task restores
fork_child_return:
    ; At this point, ESP points to the registers_t structure
    ; We need to restore segments and registers, then iret
    
    ; First, restore DS from the saved value
    pop eax
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    
    ; Restore general purpose registers
    popa
    
    ; Skip error code and interrupt number
    add esp, 8
    
    ; Return to user mode
    iret

; user_mode_entry_asm(uint32_t entry, uint32_t utop)
; This function is called from the kernel to switch to user mode.
; It follows the same calling convention as a normal C function.
user_mode_entry_asm:
    cli
    ; [esp+0] = retaddr
    ; [esp+4] = entry
    ; [esp+8] = stack (utop)
    
    mov eax, [esp + 4]    ; entry
    mov edx, [esp + 8]    ; user stack

    ; Segments
    mov bx, 0x23
    mov ds, bx
    mov es, bx
    mov fs, bx
    mov gs, bx

    ; Build IRET frame
    push 0x23             ; SS
    push edx              ; ESP
    pushfd                ; EFLAGS
    pop ecx
    or ecx, 0x200         ; Enable interrupts (IF)
    push ecx
    push 0x1B             ; CS
    push eax              ; EIP

    ; Zero registers for safety
    xor eax, eax
    xor ebx, ebx
    xor ecx, ecx
    xor edx, edx
    xor esi, esi
    xor edi, edi
    xor ebp, ebp

    iret

; void switch_task(uint32_t *old_esp, uint32_t new_esp, uint32_t new_cr3);
switch_task:
    ; 1. Save state of current task
    ; Stack: [Return Addr], old_esp_ptr, new_esp, new_cr3
    
    ; Push registers that C compiler expects us to save (Callee-saved)
    push ebx
    push esi
    push edi
    push ebp
    
    ; Save Flags
    pushf
    
    ; Save current ESP to *old_esp
    mov eax, [esp + 24] ; Get old_esp_ptr (offset depends on pushes above: 4regs*4 + 1flags*4 + 1ret*4 = 24 bytes up?)
                        ; Wait, let's count carefully.
                        ; On Entry: [Ret] [OldESP_Ptr] [NewESP] [NewCR3] +4, +8, +12, +16 relative to ESP
                        ; Pushed: EBX, ESI, EDI, EBP (4*4=16 bytes)
                        ; Pushed: EFLAGS (4 bytes)
                        ; Total pushed: 20 bytes.
                        ; So [Return Addr] is at ESP+20.
                        ; [OldESP_Ptr] is at ESP+24.
    
    mov edx, [esp + 24] ; Get old_esp_ptr
    mov [edx], esp      ; *old_esp_ptr = current ESP
    
    ; 2. Load state of new task
    ; Important: Load values into registers BEFORE switching ESP
    mov eax, [esp + 28] ; EAX = new_esp
    mov ecx, [esp + 32] ; ECX = new_cr3
    
    mov esp, eax        ; Switch stack!
    
    ; 3. Switch Page Directory (if different)
    mov edx, cr3
    cmp edx, ecx
    je .done_cr3
    mov cr3, ecx
.done_cr3:

    ; 4. Restore registers from NEW stack
    popf
    pop ebp
    pop edi
    pop esi
    pop ebx
    
    ; 5. Jump to new task's EIP
    ret
