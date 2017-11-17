;
;Copyright 2014 Jay Sorg
;
;Permission to use, copy, modify, distribute, and sell this software and its
;documentation for any purpose is hereby granted without fee, provided that
;the above copyright notice appear in all copies and that both that
;copyright notice and this permission notice appear in supporting
;documentation.
;
;The above copyright notice and this permission notice shall be included in
;all copies or substantial portions of the Software.
;
;THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
;IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
;FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
;OPEN GROUP BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN
;AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
;CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
;
;ARGB to ABGR
;amd64 SSE2
;

%include "common.asm"

PREPARE_RODATA
c1 times 4 dd 0xFF00FF00
c2 times 4 dd 0x00FF0000
c3 times 4 dd 0x000000FF

;The first six integer or pointer arguments are passed in registers
; RDI, RSI, RDX, RCX, R8, and R9

; s8 and d8 do not need to be aligned but they should match
; in the lsb nibble, ie. s8 & 0xf == d8 & 0xf
; if not, it won't make use of the simd
;int
;a8r8g8b8_to_a8b8g8r8_box_amd64_sse2(const char *s8, int src_stride,
;                                    char *d8, int dst_stride,
;                                    int width, int height);
PROC a8r8g8b8_to_a8b8g8r8_box_amd64_sse2
    push rbx
    push rbp

    movdqa xmm4, [lsym(c1)]
    movdqa xmm5, [lsym(c2)]
    movdqa xmm6, [lsym(c3)]

    ; local vars
    ; long src_stride
    ; long dst_stride
    ; long width
    ; long height
    ; const char* src
    ; char* dst
    sub rsp, 48         ; local vars, 48 bytes

    mov [rsp + 0], rsi   ; src_stride
    mov [rsp + 8], rcx   ; dst_stride
    mov [rsp + 16], r8   ; width
    mov [rsp + 24], r9   ; height
    mov [rsp + 32], rdi  ; src
    mov [rsp + 40], rdx  ; dst

    mov rsi, rdi         ; src
    mov rdi, rdx         ; dst

loop_y:
    mov rcx, [rsp + 16]  ; width

loop_xpre:
    mov rax, rsi         ; look for aligned
    and rax, 0x0F        ; we can jump to next
    mov rbx, rax
    mov rax, rdi
    and rax, 0x0F
    or rax, rbx
    cmp rax, 0
    je done_loop_xpre
    cmp rcx, 1
    jl done_loop_x       ; all done with this row
    mov eax, [rsi]
    lea rsi, [rsi + 4]
    mov edx, eax         ; a and g
    and edx, 0xFF00FF00
    mov ebx, eax         ; r
    and ebx, 0x00FF0000
    shr ebx, 16
    or edx, ebx
    mov ebx, eax         ; b
    and ebx, 0x000000FF
    shl ebx, 16
    or edx, ebx
    mov [rdi], edx
    lea rdi, [rdi + 4]
    dec rcx
    jmp loop_xpre
done_loop_xpre:

; A R G B A R G B A R G B A R G B to
; A B G R A B G R A B G R A B G R

loop_x8:
    cmp rcx, 8
    jl done_loop_x8

    movdqa xmm0, [rsi]
    lea rsi, [rsi + 16]
    movdqa xmm3, xmm0    ; a and g
    pand xmm3, xmm4
    movdqa xmm1, xmm0    ; r
    pand xmm1, xmm5
    psrld xmm1, 16
    por xmm3, xmm1
    movdqa xmm1, xmm0    ; b
    pand xmm1, xmm6
    pslld xmm1, 16
    por xmm3, xmm1
    movdqa [rdi], xmm3
    lea rdi, [rdi + 16]
    sub rcx, 4

    movdqa xmm0, [rsi]
    lea rsi, [rsi + 16]
    movdqa xmm3, xmm0    ; a and g
    pand xmm3, xmm4
    movdqa xmm1, xmm0    ; r
    pand xmm1, xmm5
    psrld xmm1, 16
    por xmm3, xmm1
    movdqa xmm1, xmm0    ; b
    pand xmm1, xmm6
    pslld xmm1, 16
    por xmm3, xmm1
    movdqa [rdi], xmm3
    lea rdi, [rdi + 16]
    sub rcx, 4

    jmp loop_x8
done_loop_x8:

loop_x:
    cmp rcx, 1
    jl done_loop_x
    mov eax, [rsi]
    lea rsi, [rsi + 4]
    mov edx, eax         ; a and g
    and edx, 0xFF00FF00
    mov ebx, eax         ; r
    and ebx, 0x00FF0000
    shr ebx, 16
    or edx, ebx
    mov ebx, eax         ; b
    and ebx, 0x000000FF
    shl ebx, 16
    or edx, ebx
    mov [rdi], edx
    lea rdi, [rdi + 4]
    dec rcx
    jmp loop_x
done_loop_x:

    mov rsi, [rsp + 32] ; src
    add rsi, [rsp + 0]  ; src_stride
    mov [rsp + 32], rsi

    mov rdi, [rsp + 40] ; dst
    add rdi, [rsp + 8]  ; dst_stride
    mov [rsp + 40], rdi

    mov rcx, [rsp + 24] ; height
    dec rcx
    mov [rsp + 24], rcx
    jnz loop_y

    mov eax, 0          ; return value
    add rsp, 48
    pop rbp
    pop rbx
    ret
END_OF_FILE
