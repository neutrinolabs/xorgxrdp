;
;Copyright 2024 Jay Sorg
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
;ARGB to YUVALP
;amd64 SSE2
;
; notes
;   address s8 should be aligned on 16 bytes, will be slower if not
;   width must be multiple of 8 and > 0
;   height must be > 0

%include "common.asm"

PREPARE_RODATA
    cd255  times 4 dd 255
    cw128  times 8 dw 128
    cw77   times 8 dw 77
    cw150  times 8 dw 150
    cw29   times 8 dw 29
    cw43   times 8 dw 43
    cw85   times 8 dw 85
    cw107  times 8 dw 107
    cw21   times 8 dw 21

%define LS8            [rsp +   0] ; s8
%define LSRC_STRIDE    [rsp +   8] ; src_stride
%define LD8            [rsp +  16] ; d8
%define LDST_STRIDE    [rsp +  24] ; dst_stride
%define LWIDTH         [rsp +  32] ; width
%define LHEIGHT        [rsp +  40] ; height

;The first six integer or pointer arguments are passed in registers
; RDI, RSI, RDX, RCX, R8, and R9

;int
;a8r8g8b8_to_yuvalp_box_amd64_sse2(const uint8_t *s8, int src_stride,
;                                  uint8_t *d8, int dst_stride,
;                                  int width, int height);
PROC a8r8g8b8_to_yuvalp_box_amd64_sse2
    push rbx
    push rbp
    sub rsp, 48                 ; local vars, 48 bytes

    mov LS8, rdi                ; s8
    mov LSRC_STRIDE, rsi        ; src_stride
    mov LD8, rdx                ; d8
    mov LDST_STRIDE, rcx        ; dst_stride
    mov LWIDTH, r8              ; width
    mov LHEIGHT, r9             ; height

    pxor xmm7, xmm7

    mov ebx, LHEIGHT            ; ebx = height

row_loop1:
    mov rsi, LS8                ; s8
    mov rdi, LD8                ; d8

    mov ecx, LWIDTH             ; ecx = width
    shr ecx, 3                  ; doing 8 pixels at a time

loop1:
    movdqu xmm0, [rsi]          ; 4 pixels, 16 bytes
    movdqa xmm1, xmm0           ; blue
    pand xmm1, [lsym(cd255)]    ; blue
    movdqa xmm2, xmm0           ; green
    psrld xmm2, 8               ; green
    pand xmm2, [lsym(cd255)]    ; green
    movdqa xmm3, xmm0           ; red
    psrld xmm3, 16              ; red
    pand xmm3, [lsym(cd255)]    ; red
    movdqa xmm4, xmm0           ; alpha
    psrld xmm4, 24              ; alpha
    pand xmm4, [lsym(cd255)]    ; alpha

    movdqu xmm0, [rsi + 16]     ; 4 pixels, 16 bytes
    movdqa xmm5, xmm0           ; alpha
    psrld xmm5, 24              ; alpha
    pand xmm5, [lsym(cd255)]    ; alpha
    packssdw xmm4, xmm5         ; xmm4 = 8 alphas
    packuswb xmm4, xmm7
    movq [rdi + 3 * 64 * 64], xmm4  ; out 8 bytes aaaaaaaa
    movdqa xmm4, xmm0           ; blue
    pand xmm4, [lsym(cd255)]    ; blue
    movdqa xmm5, xmm0           ; green
    psrld xmm5, 8               ; green
    pand xmm5, [lsym(cd255)]    ; green
    movdqa xmm6, xmm0           ; red
    psrld xmm6, 16              ; red
    pand xmm6, [lsym(cd255)]    ; red

    packssdw xmm1, xmm4         ; xmm1 = 8 blues
    packssdw xmm2, xmm5         ; xmm2 = 8 greens
    packssdw xmm3, xmm6         ; xmm3 = 8 reds

    ; _Y = (77 * _R + 150 * _G + 29 * _B) >> 8;
    movdqa xmm4, xmm1           ; blue
    movdqa xmm5, xmm2           ; green
    movdqa xmm6, xmm3           ; red
    pmullw xmm4, [lsym(cw29)]
    pmullw xmm5, [lsym(cw150)]
    pmullw xmm6, [lsym(cw77)]
    paddw xmm4, xmm5
    paddw xmm4, xmm6
    psrlw xmm4, 8
    packuswb xmm4, xmm7
    movq [rdi], xmm4            ; out 8 bytes yyyyyyyy

    ; _U = ((-43 * _R - 85 * _G + 128 * _B) >> 8) + 128;
    movdqa xmm4, xmm1           ; blue
    movdqa xmm5, xmm2           ; green
    movdqa xmm6, xmm3           ; red
    pmullw xmm4, [lsym(cw128)]
    pmullw xmm5, [lsym(cw85)]
    pmullw xmm6, [lsym(cw43)]
    psubw xmm4, xmm5
    psubw xmm4, xmm6
    psraw xmm4, 8
    paddw xmm4, [lsym(cw128)]
    packuswb xmm4, xmm7
    movq [rdi + 1 * 64 * 64], xmm4  ; out 8 bytes uuuuuuuu

    ; _V = ((128 * _R - 107 * _G -  21 * _B) >> 8) + 128;
    movdqa xmm6, xmm1           ; blue
    movdqa xmm5, xmm2           ; green
    movdqa xmm4, xmm3           ; red
    pmullw xmm4, [lsym(cw128)]
    pmullw xmm5, [lsym(cw107)]
    pmullw xmm6, [lsym(cw21)]
    psubw xmm4, xmm5
    psubw xmm4, xmm6
    psraw xmm4, 8
    paddw xmm4, [lsym(cw128)]
    packuswb xmm4, xmm7
    movq [rdi + 2 * 64 * 64], xmm4  ; out 8 bytes vvvvvvvv

    ; move right
    lea rsi, [rsi + 32]
    lea rdi, [rdi + 8]

    dec ecx
    jnz loop1

    ; update s8
    mov rax, LS8                ; s8
    add rax, LSRC_STRIDE        ; s8 += src_stride
    mov LS8, rax

    ; update d8
    mov rax, LD8                ; d8
    add rax, LDST_STRIDE        ; d8 += dst_stride
    mov LD8, rax

    dec ebx
    jnz row_loop1

    mov rax, 0                  ; return value
    add rsp, 48                 ; local vars, 48 bytes
    pop rbp
    pop rbx
    ret
END_OF_FILE
