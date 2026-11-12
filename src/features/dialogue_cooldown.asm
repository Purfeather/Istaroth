option casemap:none
EXTERN istaroth_cooldown_model:QWORD
EXTERN istaroth_cooldown_enabled:DWORD
EXTERN istaroth_cooldown_seen:DWORD
EXTERN istaroth_cooldown_bypassed:DWORD
EXTERN istaroth_cooldown_original:QWORD
EXTERN istaroth_cooldown_next:QWORD
EXTERN istaroth_advance_context:QWORD
EXTERN istaroth_advance_tid:DWORD
EXTERN istaroth_outer_seen:DWORD
EXTERN istaroth_outer_bypassed:DWORD
EXTERN istaroth_auto_cooldown_seen:DWORD
EXTERN istaroth_auto_cooldown_bypassed:DWORD
EXTERN istaroth_outer_a_original:QWORD
EXTERN istaroth_outer_a_next:QWORD
EXTERN istaroth_outer_b_original:QWORD
EXTERN istaroth_outer_b_next:QWORD
PUBLIC IstarothOuterA
PUBLIC IstarothOuterB
PUBLIC IstarothOuterFixtureA
PUBLIC IstarothOuterFixtureB
PUBLIC IstarothOuterCompareA
PUBLIC IstarothOuterNextA
PUBLIC IstarothOuterCompareB
PUBLIC IstarothOuterNextB
PUBLIC IstarothCooldownGate
PUBLIC IstarothCooldownFixture
PUBLIC IstarothFixtureJump
PUBLIC IstarothFixtureNext
.code
; Entered at JBE, with the original UCOMISS flags and model in RSI.
IstarothCooldownGate PROC
    pushfq
    push rax
    cmp rsi, QWORD PTR [istaroth_cooldown_model]
    jne original
    test QWORD PTR [rsp+8], 041h
    jz original
    lock inc DWORD PTR [istaroth_cooldown_seen]
    cmp QWORD PTR [istaroth_advance_context], 0
    je check_enabled
    mov eax, DWORD PTR gs:[048h]
    cmp eax, DWORD PTR [istaroth_advance_tid]
    jne check_enabled
    lock inc DWORD PTR [istaroth_auto_cooldown_seen]
check_enabled:
    cmp DWORD PTR [istaroth_cooldown_enabled], 1
    jne original
    lock inc DWORD PTR [istaroth_cooldown_bypassed]
    cmp QWORD PTR [istaroth_advance_context], 0
    je bypass
    mov eax, DWORD PTR gs:[048h]
    cmp eax, DWORD PTR [istaroth_advance_tid]
    jne bypass
    lock inc DWORD PTR [istaroth_auto_cooldown_bypassed]
bypass:
    pop rax
    popfq
    jmp QWORD PTR [istaroth_cooldown_next]
original:
    pop rax
    popfq
    jmp QWORD PTR [istaroth_cooldown_original]
IstarothCooldownGate ENDP

; Only our in-flight UI call can omit these two waits. No deadline fields change.
OUTER_GATE MACRO entry_name, original_pointer, next_pointer, deadline_offset
LOCAL original_branch
entry_name PROC
    ucomiss xmm0, DWORD PTR [rsi+deadline_offset]
    pushfq
    push rax
    cmp rsi, QWORD PTR [istaroth_advance_context]
    jne original_branch
    cmp QWORD PTR [istaroth_advance_context], 0
    je original_branch
    mov eax, DWORD PTR gs:[048h]
    cmp eax, DWORD PTR [istaroth_advance_tid]
    jne original_branch
    test QWORD PTR [rsp+8], 004h
    jnz original_branch
    test QWORD PTR [rsp+8], 041h
    jz original_branch
    lock inc DWORD PTR [istaroth_outer_seen]
    cmp DWORD PTR [istaroth_cooldown_enabled], 1
    jne original_branch
    lock inc DWORD PTR [istaroth_outer_bypassed]
    pop rax
    popfq
    jmp QWORD PTR [next_pointer]
original_branch:
    pop rax
    popfq
    jmp QWORD PTR [original_pointer]
entry_name ENDP
ENDM

OUTER_GATE IstarothOuterA, istaroth_outer_a_original, istaroth_outer_a_next, 030ch
OUTER_GATE IstarothOuterB, istaroth_outer_b_original, istaroth_outer_b_next, 0348h

; Owned fixtures use the same seven-byte comparison and short branch as the game.
OUTER_FIXTURE MACRO entry_name, compare_label, next_label, deadline_offset
LOCAL blocked_branch
entry_name PROC
    push rsi
    mov rsi, r8
    movss DWORD PTR [rsi+deadline_offset], xmm1
compare_label LABEL BYTE
    ucomiss xmm0, DWORD PTR [rsi+deadline_offset]
    jbe SHORT blocked_branch
next_label LABEL BYTE
    pushfq
    pop rdx
    mov eax, 1
    and edx, 045h
    shl edx, 8
    or eax, edx
    pop rsi
    ret
    DB 32 DUP (090h)
blocked_branch:
    pushfq
    pop rdx
    xor eax, eax
    and edx, 045h
    shl edx, 8
    or eax, edx
    pop rsi
    ret
entry_name ENDP
ENDM

OUTER_FIXTURE IstarothOuterFixtureA, IstarothOuterCompareA, IstarothOuterNextA, 030ch
OUTER_FIXTURE IstarothOuterFixtureB, IstarothOuterCompareB, IstarothOuterNextB, 0348h

; Owned fixture: result bit 0 is fallthrough, bits 8..14 contain UCOMISS flags.
IstarothCooldownFixture PROC
    push rsi
    mov rsi, r8
    ucomiss xmm0, xmm1
IstarothFixtureJump LABEL BYTE
    jbe fixture_blocked
IstarothFixtureNext LABEL BYTE
    pushfq
    pop rdx
    mov eax, 1
    and edx, 045h
    shl edx, 8
    or eax, edx
    pop rsi
    ret
    DB 160 DUP (090h)
fixture_blocked:
    pushfq
    pop rdx
    xor eax, eax
    and edx, 045h
    shl edx, 8
    or eax, edx
    pop rsi
    ret
IstarothCooldownFixture ENDP
END
