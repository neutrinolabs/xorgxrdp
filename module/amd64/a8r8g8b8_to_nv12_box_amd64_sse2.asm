;
;Copyright 2015 Jay Sorg
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
;ARGB to NV12
;amd64 SSE2
;
; notes
;   address s8 should be aligned on 16 bytes, will be slower if not
;   width should be multile of 8 and > 0
;   height should be even and > 0

SECTION .data

    align 16

    cd255  times 4 dd 255

    cw255  times 8 dw 255
    cw16   times 8 dw 16
    cw128  times 8 dw 128
    cw1053 times 8 dw 1053
    cw2064 times 8 dw 2064
    cw401  times 8 dw 401
    cw606  times 8 dw 606
    cw1192 times 8 dw 1192
    cw1798 times 8 dw 1798
    cw1507 times 8 dw 1507
    cw291  times 8 dw 291

SECTION .text

%macro PROC 1
    align 16
    global %1
    %1:
%endmacro

%define LS8         [rsp +  0] ; s8
%define LD8_Y       [rsp +  8] ; d8_y
%define LD8_UV      [rsp + 16] ; d8_uv
%define LSRC_STRIDE [rsp + 24] ; src_stride
%define LDST_STRIDE [rsp + 32] ; dst_stride
%define LWIDTH      [rsp + 40] ; width
%define LHEIGHT     [rsp + 48] ; height
%define LU1         [rsp + 56] ; first line U, 8 bytes
%define LV1         [rsp + 64] ; first line V, 8 bytes
%define LU2         [rsp + 72] ; second line U, 8 bytes
%define LV2         [rsp + 80] ; second line V, 8 bytes

;The first six integer or pointer arguments are passed in registers
; RDI, RSI, RDX, RCX, R8, and R9

;int
;a8r8g8b8_to_nv12_box_amd64_sse2(char *s8, int src_stride,
;                                char *d8, int dst_stride,
;                                int width, int height);
PROC a8r8g8b8_to_nv12_box_amd64_sse2
    push rbx
    push rbp
    sub rsp, 88                ; local vars, 88 bytes

    mov LS8, rdi               ; s8
    mov LD8_Y, rdx             ; d8_y
    mov rax, r8
    imul rax, r9
    add rax, rdx
    mov LD8_UV, rax            ; d8_uv
    mov LSRC_STRIDE, rsi       ; src_stride
    mov LDST_STRIDE, rcx       ; dst_stride
    mov LWIDTH, r8             ; width
    mov LHEIGHT, r9            ; height

    pxor xmm7, xmm7

    mov rbx, LHEIGHT           ; rbx = height
    shr rbx, 1                 ; doing 2 lines at a time

row_loop1:
    mov rsi, LS8               ; s8
    mov rdi, LD8_Y             ; d8_y
    mov rdx, LD8_UV            ; d8_uv

    mov rcx, LWIDTH            ; rcx = width
    shr rcx, 3                 ; doing 8 pixels at a time

loop1:
    ; first line
    movdqu xmm0, [rsi]         ; 4 pixels, 16 bytes
    movdqa xmm1, xmm0          ; blue
    pand xmm1, [rel cd255]     ; blue
    movdqa xmm2, xmm0          ; green
    psrld xmm2, 8              ; green
    pand xmm2, [rel cd255]     ; green
    movdqa xmm3, xmm0          ; red
    psrld xmm3, 16             ; red
    pand xmm3, [rel cd255]     ; red

    movdqu xmm0, [rsi + 16]    ; 4 pixels, 16 bytes
    movdqa xmm4, xmm0          ; blue
    pand xmm4, [rel cd255]     ; blue
    movdqa xmm5, xmm0          ; green
    psrld xmm5, 8              ; green
    pand xmm5, [rel cd255]     ; green
    movdqa xmm6, xmm0          ; red
    psrld xmm6, 16             ; red
    pand xmm6, [rel cd255]     ; red

    packusdw xmm1, xmm4        ; xmm1 = 8 blues
    packusdw xmm2, xmm5        ; xmm2 = 8 greens
    packusdw xmm3, xmm6        ; xmm3 = 8 reds
    psllw xmm1, 4              ; blue
    psllw xmm2, 4              ; green
    psllw xmm3, 4              ; red

;  _Y = ( ((1053 * ((_R) << 4)) >> 16) + ((2064 * ((_G) << 4)) >> 16) +  (( 401 * ((_B) << 4)) >> 16)) +  16;
    movdqa xmm4, xmm1          ; blue
    movdqa xmm5, xmm2          ; green
    movdqa xmm6, xmm3          ; red
    pmulhw xmm4, [rel cw401]
    pmulhw xmm5, [rel cw2064]
    pmulhw xmm6, [rel cw1053]
    paddw xmm4, xmm5
    paddw xmm4, xmm6
    paddw xmm4, [rel cw16]
    packuswb xmm4, xmm7
    movq [rdi], xmm4           ; out 8 bytes yyyyyyyy

;  _U = ( ((1798 * ((_B) << 4)) >> 16) - (( 606 * ((_R) << 4)) >> 16) -  ((1192 * ((_G) << 4)) >> 16)) + 128;
    movdqa xmm4, xmm1          ; blue
    movdqa xmm5, xmm2          ; green
    movdqa xmm6, xmm3          ; red
    pmulhw xmm4, [rel cw1798]
    pmulhw xmm5, [rel cw1192]
    pmulhw xmm6, [rel cw606]
    psubw xmm4, xmm5
    psubw xmm4, xmm6
    paddw xmm4, [rel cw128]
    packuswb xmm4, xmm7
    movq LU1, xmm4             ; save for later

;  _V = ( ((1798 * ((_R) << 4)) >> 16) - ((1507 * ((_G) << 4)) >> 16) -  (( 291 * ((_B) << 4)) >> 16)) + 128;
    movdqa xmm6, xmm1          ; blue
    movdqa xmm5, xmm2          ; green
    movdqa xmm4, xmm3          ; red
    pmulhw xmm4, [rel cw1798]
    pmulhw xmm5, [rel cw1507]
    pmulhw xmm6, [rel cw291]
    psubw xmm4, xmm5
    psubw xmm4, xmm6
    paddw xmm4, [rel cw128]
    packuswb xmm4, xmm7
    movq LV1, xmm4             ; save for later

    ; go down to second line
    add rsi, LSRC_STRIDE
    add rdi, LDST_STRIDE

    ; second line
    movdqu xmm0, [rsi]         ; 4 pixels, 16 bytes
    movdqa xmm1, xmm0          ; blue
    pand xmm1, [rel cd255]     ; blue
    movdqa xmm2, xmm0          ; green
    psrld xmm2, 8              ; green
    pand xmm2, [rel cd255]     ; green
    movdqa xmm3, xmm0          ; red
    psrld xmm3, 16             ; red
    pand xmm3, [rel cd255]     ; red

    movdqu xmm0, [rsi + 16]    ; 4 pixels, 16 bytes
    movdqa xmm4, xmm0          ; blue
    pand xmm4, [rel cd255]     ; blue
    movdqa xmm5, xmm0          ; green
    psrld xmm5, 8              ; green
    pand xmm5, [rel cd255]     ; green
    movdqa xmm6, xmm0          ; red
    psrld xmm6, 16             ; red
    pand xmm6, [rel cd255]     ; red

    packusdw xmm1, xmm4        ; xmm1 = 8 blues
    packusdw xmm2, xmm5        ; xmm2 = 8 greens
    packusdw xmm3, xmm6        ; xmm3 = 8 reds
    psllw xmm1, 4              ; blue
    psllw xmm2, 4              ; green
    psllw xmm3, 4              ; red

;  _Y = ( ((1053 * ((_R) << 4)) >> 16) + ((2064 * ((_G) << 4)) >> 16) +  (( 401 * ((_B) << 4)) >> 16)) +  16;
    movdqa xmm4, xmm1          ; blue
    movdqa xmm5, xmm2          ; green
    movdqa xmm6, xmm3          ; red 
    pmulhw xmm4, [rel cw401]
    pmulhw xmm5, [rel cw2064]
    pmulhw xmm6, [rel cw1053]
    paddw xmm4, xmm5
    paddw xmm4, xmm6
    paddw xmm4, [rel cw16]
    packuswb xmm4, xmm7
    movq [rdi], xmm4           ; out 8 bytes yyyyyyyy

;  _U = ( ((1798 * ((_B) << 4)) >> 16) - (( 606 * ((_R) << 4)) >> 16) -  ((1192 * ((_G) << 4)) >> 16)) + 128;
    movdqa xmm4, xmm1          ; blue
    movdqa xmm5, xmm2          ; green
    movdqa xmm6, xmm3          ; red
    pmulhw xmm4, [rel cw1798]
    pmulhw xmm5, [rel cw1192]
    pmulhw xmm6, [rel cw606]
    psubw xmm4, xmm5
    psubw xmm4, xmm6
    paddw xmm4, [rel cw128]
    packuswb xmm4, xmm7
    movq LU2, xmm4             ; save for later

;  _V = ( ((1798 * ((_R) << 4)) >> 16) - ((1507 * ((_G) << 4)) >> 16) -  (( 291 * ((_B) << 4)) >> 16)) + 128;
    movdqa xmm6, xmm1          ; blue
    movdqa xmm5, xmm2          ; green
    movdqa xmm4, xmm3          ; red
    pmulhw xmm4, [rel cw1798]
    pmulhw xmm5, [rel cw1507]
    pmulhw xmm6, [rel cw291]
    psubw xmm4, xmm5
    psubw xmm4, xmm6
    paddw xmm4, [rel cw128]
    packuswb xmm4, xmm7
    movq LV2, xmm4             ; save for later

    ; uv add and divide(average)
    movq mm1, LU1              ; u from first line
    movq mm3, mm1
    pand mm1, [rel cw255]
    psrlw mm3, 8
    pand mm3, [rel cw255]
    paddw mm1, mm3             ; add
    movq mm2, LU2              ; u from second line
    movq mm3, mm2
    pand mm2, [rel cw255]
    paddw mm1, mm2             ; add
    psrlw mm3, 8
    pand mm3, [rel cw255]
    paddw mm1, mm3             ; add
    psrlw mm1, 2               ; div 4

    movq mm2, LV1              ; v from first line
    movq mm4, mm2
    pand mm2, [rel cw255]
    psrlw mm4, 8
    pand mm4, [rel cw255]
    paddw mm2, mm4             ; add
    movq mm3, LV2              ; v from second line
    movq mm4, mm3
    pand mm3, [rel cw255]
    paddw mm2, mm3             ; add
    psrlw mm4, 8
    pand mm4, [rel cw255]
    paddw mm2, mm4             ; add
    psrlw mm2, 2               ; div 4

    packuswb mm1, mm1
    packuswb mm2, mm2

    punpcklbw mm1, mm2         ; uv
    movq [rdx], mm1            ; out 8 bytes uvuvuvuv

    ; go up to first line
    sub rsi, LSRC_STRIDE
    sub rdi, LDST_STRIDE

    ; move right
    lea rsi, [rsi + 32]
    lea rdi, [rdi + 8]
    lea rdx, [rdx + 8]

    dec rcx
    jnz loop1

    ; update s8
    mov rax, LS8               ; s8
    add rax, LSRC_STRIDE       ; s8 += src_stride
    add rax, LSRC_STRIDE       ; s8 += src_stride
    mov [rsp + 0], rax

    ; update d8_y
    mov rax, LD8_Y             ; d8_y
    add rax, LDST_STRIDE       ; d8_y += dst_stride
    add rax, LDST_STRIDE       ; d8_y += dst_stride
    mov LD8_Y, rax

    ; update d8_uv
    mov rax, LD8_UV           ; d8_uv
    add rax, LDST_STRIDE      ; d8_uv += dst_stride
    mov LD8_UV, rax

    dec rbx
    jnz row_loop1

    mov rax, 0                 ; return value
    add rsp, 88                ; local vars, 88 bytes
    pop rbp
    pop rbx
    ret
    align 16

