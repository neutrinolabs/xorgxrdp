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
    cw66   times 8 dw 66
    cw129  times 8 dw 129
    cw25   times 8 dw 25
    cw38   times 8 dw 38
    cw74   times 8 dw 74
    cw112  times 8 dw 112
    cw94   times 8 dw 94
    cw18   times 8 dw 18
    cw2    times 8 dw 2

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

    packssdw xmm1, xmm4        ; xmm1 = 8 blues
    packssdw xmm2, xmm5        ; xmm2 = 8 greens
    packssdw xmm3, xmm6        ; xmm3 = 8 reds

    ; _Y = (( 66 * _R + 129 * _G +  25 * _B + 128) >> 8) +  16;
    movdqa xmm4, xmm1          ; blue
    movdqa xmm5, xmm2          ; green
    movdqa xmm6, xmm3          ; red
    pmullw xmm4, [rel cw25]
    pmullw xmm5, [rel cw129]
    pmullw xmm6, [rel cw66]
    paddw xmm4, xmm5
    paddw xmm4, xmm6
    paddw xmm4, [rel cw128]
    psrlw xmm4, 8
    paddw xmm4, [rel cw16]
    packuswb xmm4, xmm7
    movq [rdi], xmm4           ; out 8 bytes yyyyyyyy

    ; _U = ((-38 * _R -  74 * _G + 112 * _B + 128) >> 8) + 128;
    movdqa xmm4, xmm1          ; blue
    movdqa xmm5, xmm2          ; green
    movdqa xmm6, xmm3          ; red
    pmullw xmm4, [rel cw112]
    pmullw xmm5, [rel cw74]
    pmullw xmm6, [rel cw38]
    psubw xmm4, xmm5
    psubw xmm4, xmm6
    paddw xmm4, [rel cw128]
    psraw xmm4, 8
    paddw xmm4, [rel cw128]
    packuswb xmm4, xmm7
    movq LU1, xmm4             ; save for later

    ; _V = ((112 * _R -  94 * _G -  18 * _B + 128) >> 8) + 128;
    movdqa xmm6, xmm1          ; blue
    movdqa xmm5, xmm2          ; green
    movdqa xmm4, xmm3          ; red
    pmullw xmm4, [rel cw112]
    pmullw xmm5, [rel cw94]
    pmullw xmm6, [rel cw18]
    psubw xmm4, xmm5
    psubw xmm4, xmm6
    paddw xmm4, [rel cw128]
    psraw xmm4, 8
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

    packssdw xmm1, xmm4        ; xmm1 = 8 blues
    packssdw xmm2, xmm5        ; xmm2 = 8 greens
    packssdw xmm3, xmm6        ; xmm3 = 8 reds

    ; _Y = (( 66 * _R + 129 * _G +  25 * _B + 128) >> 8) +  16;
    movdqa xmm4, xmm1          ; blue
    movdqa xmm5, xmm2          ; green
    movdqa xmm6, xmm3          ; red 
    pmullw xmm4, [rel cw25]
    pmullw xmm5, [rel cw129]
    pmullw xmm6, [rel cw66]
    paddw xmm4, xmm5
    paddw xmm4, xmm6
    paddw xmm4, [rel cw128]
    psrlw xmm4, 8
    paddw xmm4, [rel cw16]
    packuswb xmm4, xmm7
    movq [rdi], xmm4           ; out 8 bytes yyyyyyyy

    ; _U = ((-38 * _R -  74 * _G + 112 * _B + 128) >> 8) + 128;
    movdqa xmm4, xmm1          ; blue
    movdqa xmm5, xmm2          ; green
    movdqa xmm6, xmm3          ; red
    pmullw xmm4, [rel cw112]
    pmullw xmm5, [rel cw74]
    pmullw xmm6, [rel cw38]
    psubw xmm4, xmm5
    psubw xmm4, xmm6
    paddw xmm4, [rel cw128]
    psraw xmm4, 8
    paddw xmm4, [rel cw128]
    packuswb xmm4, xmm7
    movq LU2, xmm4             ; save for later

    ; _V = ((112 * _R -  94 * _G -  18 * _B + 128) >> 8) + 128;
    movdqa xmm6, xmm1          ; blue
    movdqa xmm5, xmm2          ; green
    movdqa xmm4, xmm3          ; red
    pmullw xmm4, [rel cw112]
    pmullw xmm5, [rel cw94]
    pmullw xmm6, [rel cw18]
    psubw xmm4, xmm5
    psubw xmm4, xmm6
    paddw xmm4, [rel cw128]
    psraw xmm4, 8
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
    paddw mm1, [rel cw2]       ; add 2
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
    paddw mm1, [rel cw2]       ; add 2
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
    mov LS8, rax

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

