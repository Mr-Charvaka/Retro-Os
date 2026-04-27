; =================================================================
; ring2_gate.asm — Call Gate Entry Point (Ring 3 → Ring 2)
; =================================================================
; This is the raw entry point that the call gate jumps to.
; The CPU has already:
;   1. Switched to Ring 2 stack (from TSS esp2/ss2)
;   2. Pushed Ring 3's SS:ESP
;   3. Copied 2 parameters from Ring 3 stack
;   4. Pushed Ring 3's CS:EIP (return address)
;
; Ring 2 stack at entry:
;   [ESP+20]  Ring 3 SS          (4 bytes)
;   [ESP+16]  Ring 3 ESP         (4 bytes)
;   [ESP+12]  param 1: request_type (4 bytes, copied by CPU)
;   [ESP+8]   param 2: data_ptr    (4 bytes, copied by CPU)
;   [ESP+4]   Ring 3 CS           (4 bytes, return address)
;   [ESP+0]   Ring 3 EIP          (4 bytes, return address)
;
; After RETF $8:
;   CPU pops EIP and CS (return to Ring 3)
;   CPU pops 8 bytes (the 2 parameters we declared)
;   CPU pops ESP and SS (restore Ring 3 stack)
;   CPL changes back to 3
; =================================================================

[BITS 32]

global brain_request_handler_entry
extern brain_request_handler_c

brain_request_handler_entry:
    ; Save callee-saved registers (C ABI)
    push ebp
    mov  ebp, esp
    push ebx
    push esi
    push edi

    ; Extract parameters from the call gate stack frame.
    ; After our push ebp + mov ebp,esp:
    ;   [ebp+0]   = saved EBP
    ;   [ebp+4]   = Ring 3 EIP  (return address)
    ;   [ebp+8]   = Ring 3 CS   (return address)
    ;   [ebp+12]  = param 1: request_type
    ;   [ebp+16]  = param 2: data_ptr
    ;   [ebp+20]  = Ring 3 ESP
    ;   [ebp+24]  = Ring 3 SS

    ; Push parameters for C function call (right to left)
    push dword [ebp+16]    ; data_ptr (param 2)
    push dword [ebp+12]    ; request_type (param 1)

    ; Call the C handler
    call brain_request_handler_c

    ; Return value is in EAX — leave it there for Ring 3
    ; Clean up C call parameters
    add esp, 8

    ; Restore callee-saved registers
    pop edi
    pop esi
    pop ebx
    pop ebp

    ; Far return, cleaning 8 bytes of gate parameters
    ; RETF $8 does:
    ;   Pop EIP, Pop CS (return to Ring 3)
    ;   Add 8 to ESP (remove 2 copied params from Ring 2 stack)
    ;   Pop ESP, Pop SS (restore Ring 3 stack)
    retf 8
