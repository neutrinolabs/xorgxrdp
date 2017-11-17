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
;YUY2 to RGB32
;amd64 SSE2
;
; RGB to YUV
;   0.299    0.587    0.114
;  -0.14713 -0.28886  0.436
;   0.615   -0.51499 -0.10001
; YUV to RGB
;   1        0        1.13983
;   1       -0.39465 -0.58060
;   1        2.03211  0
; shift left 12
;   4096     0        4669
;   4096    -1616    -2378
;   4096     9324     0

%include "common.asm"

PREPARE_RODATA
c128 times 8 dw 128
c4669 times 8 dw 4669
c1616 times 8 dw 1616
c2378 times 8 dw 2378
c9324 times 8 dw 9324

;The first six integer or pointer arguments are passed in registers
; RDI, RSI, RDX, RCX, R8, and R9

;int
;yuy2_to_rgb32_amd64_sse2(unsigned char *yuvs, int width, int height, int *rgbs)

PROC yuy2_to_rgb32_amd64_sse2
    push rbx
    push rbp

    mov rax, rsi
    imul rax, rdx
    mov rsi, rdi
    mov rdi, rcx

    mov rcx, rax

    movdqa xmm7, [lsym(c128)]

loop1:
    ; hi                                           lo
    ; v3 y7 u3 y6 v2 y5 u2 y4 v1 y3 u1 y2 v0 y1 u0 y0
    movdqu xmm0, [rsi]      ; 8 pixels at a time
    lea rsi, [rsi + 16]

    ; 00 y7 00 y6 00 y5 00 y4 00 y3 00 y2 00 y1 00 y0
    ; 00 u3 00 u3 00 u2 00 u2 00 u1 00 u1 00 u0 00 u0
    ; 00 v3 00 v3 00 v2 00 v2 00 v1 00 v1 00 v0 00 v0

    movdqu xmm1, xmm0
    movdqu xmm2, xmm0

    ; y
    psllw xmm0, 8
    psrlw xmm0, 8

    ; u
    pslld xmm1, 16
    psrld xmm1, 24
    movdqu xmm3, xmm1
    pslld xmm3, 16
    por xmm1, xmm3
    psubw xmm1, xmm7
    psllw xmm1, 4

    ; v
    psrld xmm2, 24
    movdqu xmm3, xmm2
    pslld xmm3, 16
    por xmm2, xmm3
    psubw xmm2, xmm7
    psllw xmm2, 4

    ; r = y + hiword(4669 * (v << 4))
    movdqa xmm4, [lsym(c4669)]
    pmulhw xmm4, xmm1
    movdqa xmm3, xmm0
    paddw xmm3, xmm4

    ; g = y - hiword(1616 * (u << 4)) - hiword(2378 * (v << 4))
    movdqa xmm5, [lsym(c1616)]
    pmulhw xmm5, xmm2
    movdqa xmm6, [lsym(c2378)]
    pmulhw xmm6, xmm1
    movdqa xmm4, xmm0
    psubw xmm4, xmm5
    psubw xmm4, xmm6

    ; b = y + hiword(9324 * (u << 4))
    movdqa xmm6, [lsym(c9324)]
    pmulhw xmm6, xmm2
    movdqa xmm5, xmm0
    paddw xmm5, xmm6

    packuswb xmm3, xmm3  ; b
    packuswb xmm4, xmm4  ; g
    punpcklbw xmm3, xmm4 ; gb

    pxor xmm4, xmm4      ; a
    packuswb xmm5, xmm5  ; r
    punpcklbw xmm5, xmm4 ; ar

    movdqa xmm4, xmm3

    punpcklwd xmm3, xmm5 ; argb
    movdqa [rdi], xmm3   ; 4 pixels
    lea rdi, [rdi + 16]

    punpckhwd xmm4, xmm5 ; argb
    movdqa [rdi], xmm4   ; 4 pixels
    lea rdi, [rdi + 16]

    sub rcx, 8
    cmp rcx, 8
    jge loop1

    mov rax, 0

    pop rbp
    pop rbx
    ret
END_OF_FILE
