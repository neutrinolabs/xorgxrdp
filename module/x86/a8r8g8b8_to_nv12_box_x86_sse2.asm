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
;x86 SSE2
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

%define LV2         [esp +  0] ; second line V, 8 bytes
%define LU2         [esp +  8] ; second line U, 8 bytes
%define LV1         [esp + 16] ; first line V, 8 bytes
%define LU1         [esp + 24] ; first line U, 8 bytes
%define LD8_UV      [esp + 32] ; d8_uv

%define LS8         [esp + 56] ; s8
%define LSRC_STRIDE [esp + 60] ; src_stride
%define LD8_Y       [esp + 64] ; d8_y
%define LDST_STRIDE [esp + 68] ; dst_stride
%define LWIDTH      [esp + 72] ; width
%define LHEIGHT     [esp + 76] ; height

;int
;a8r8g8b8_to_nv12_box_x86_sse2(char *s8, int src_stride,
;                              char *d8, int dst_stride,
;                              int width, int height);
PROC a8r8g8b8_to_nv12_box_x86_sse2
    push ebx
    push esi
    push edi
    push ebp

    sub esp, 36                ; local vars, 36 bytes

    mov eax, LWIDTH
    imul eax, LHEIGHT
    mov ebx, LD8_Y
    add ebx, eax
    mov LD8_UV, ebx

    pxor xmm7, xmm7

    mov ebx, LHEIGHT           ; ebx = height
    shr ebx, 1                 ; doing 2 lines at a time

row_loop1:
    mov esi, LS8               ; s8
    mov edi, LD8_Y             ; d8_y
    mov edx, LD8_UV            ; d8_uv

    mov ecx, LWIDTH            ; ecx = width
    shr ecx, 3                 ; doing 8 pixels at a time

loop1:
    ; first line
    movdqu xmm0, [esi]         ; 4 pixels, 16 bytes
    movdqa xmm1, xmm0          ; blue
    pand xmm1, [cd255]         ; blue
    movdqa xmm2, xmm0          ; green
    psrld xmm2, 8              ; green
    pand xmm2, [cd255]         ; green
    movdqa xmm3, xmm0          ; red
    psrld xmm3, 16             ; red
    pand xmm3, [cd255]         ; red

    movdqu xmm0, [esi + 16]    ; 4 pixels, 16 bytes
    movdqa xmm4, xmm0          ; blue
    pand xmm4, [cd255]         ; blue
    movdqa xmm5, xmm0          ; green
    psrld xmm5, 8              ; green
    pand xmm5, [cd255]         ; green
    movdqa xmm6, xmm0          ; red
    psrld xmm6, 16             ; red
    pand xmm6, [cd255]         ; red

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
    pmulhw xmm4, [cw401]
    pmulhw xmm5, [cw2064]
    pmulhw xmm6, [cw1053]
    paddw xmm4, xmm5
    paddw xmm4, xmm6
    paddw xmm4, [cw16]
    packuswb xmm4, xmm7
    movq [edi], xmm4           ; out 8 bytes yyyyyyyy

;  _U = ( ((1798 * ((_B) << 4)) >> 16) - (( 606 * ((_R) << 4)) >> 16) -  ((1192 * ((_G) << 4)) >> 16)) + 128;
    movdqa xmm4, xmm1          ; blue
    movdqa xmm5, xmm2          ; green
    movdqa xmm6, xmm3          ; red
    pmulhw xmm4, [cw1798]
    pmulhw xmm5, [cw1192]
    pmulhw xmm6, [cw606]
    psubw xmm4, xmm5
    psubw xmm4, xmm6
    paddw xmm4, [cw128]
    packuswb xmm4, xmm7
    movq LU1, xmm4             ; save for later

;  _V = ( ((1798 * ((_R) << 4)) >> 16) - ((1507 * ((_G) << 4)) >> 16) -  (( 291 * ((_B) << 4)) >> 16)) + 128;
    movdqa xmm6, xmm1          ; blue
    movdqa xmm5, xmm2          ; green
    movdqa xmm4, xmm3          ; red
    pmulhw xmm4, [cw1798]
    pmulhw xmm5, [cw1507]
    pmulhw xmm6, [cw291]
    psubw xmm4, xmm5
    psubw xmm4, xmm6
    paddw xmm4, [cw128]
    packuswb xmm4, xmm7
    movq LV1, xmm4             ; save for later

    ; go down to second line
    add esi, LSRC_STRIDE
    add edi, LDST_STRIDE

    ; second line
    movdqu xmm0, [esi]         ; 4 pixels, 16 bytes
    movdqa xmm1, xmm0          ; blue
    pand xmm1, [cd255]         ; blue
    movdqa xmm2, xmm0          ; green
    psrld xmm2, 8              ; green
    pand xmm2, [cd255]         ; green
    movdqa xmm3, xmm0          ; red
    psrld xmm3, 16             ; red
    pand xmm3, [cd255]         ; red

    movdqu xmm0, [esi + 16]    ; 4 pixels, 16 bytes
    movdqa xmm4, xmm0          ; blue
    pand xmm4, [cd255]         ; blue
    movdqa xmm5, xmm0          ; green
    psrld xmm5, 8              ; green
    pand xmm5, [cd255]         ; green
    movdqa xmm6, xmm0          ; red
    psrld xmm6, 16             ; red
    pand xmm6, [cd255]         ; red

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
    pmulhw xmm4, [cw401]
    pmulhw xmm5, [cw2064]
    pmulhw xmm6, [cw1053]
    paddw xmm4, xmm5
    paddw xmm4, xmm6
    paddw xmm4, [cw16]
    packuswb xmm4, xmm7
    movq [edi], xmm4           ; out 8 bytes yyyyyyyy

;  _U = ( ((1798 * ((_B) << 4)) >> 16) - (( 606 * ((_R) << 4)) >> 16) -  ((1192 * ((_G) << 4)) >> 16)) + 128;
    movdqa xmm4, xmm1          ; blue
    movdqa xmm5, xmm2          ; green
    movdqa xmm6, xmm3          ; red
    pmulhw xmm4, [cw1798]
    pmulhw xmm5, [cw1192]
    pmulhw xmm6, [cw606]
    psubw xmm4, xmm5
    psubw xmm4, xmm6
    paddw xmm4, [cw128]
    packuswb xmm4, xmm7
    movq LU2, xmm4             ; save for later

;  _V = ( ((1798 * ((_R) << 4)) >> 16) - ((1507 * ((_G) << 4)) >> 16) -  (( 291 * ((_B) << 4)) >> 16)) + 128;
    movdqa xmm6, xmm1          ; blue
    movdqa xmm5, xmm2          ; green
    movdqa xmm4, xmm3          ; red
    pmulhw xmm4, [cw1798]
    pmulhw xmm5, [cw1507]
    pmulhw xmm6, [cw291]
    psubw xmm4, xmm5
    psubw xmm4, xmm6
    paddw xmm4, [cw128]
    packuswb xmm4, xmm7
    movq LV2, xmm4             ; save for later

    ; uv add and divide(average)
    movq mm1, LU1              ; u from first line
    movq mm3, mm1
    pand mm1, [cw255]
    psrlw mm3, 8
    pand mm3, [cw255]
    paddw mm1, mm3             ; add
    movq mm2, LU2              ; u from second line
    movq mm3, mm2
    pand mm2, [cw255]
    paddw mm1, mm2             ; add
    psrlw mm3, 8
    pand mm3, [cw255]
    paddw mm1, mm3             ; add
    psrlw mm1, 2               ; div 4

    movq mm2, LV1              ; v from first line
    movq mm4, mm2
    pand mm2, [cw255]
    psrlw mm4, 8
    pand mm4, [cw255]
    paddw mm2, mm4             ; add
    movq mm3, LV2              ; v from second line
    movq mm4, mm3
    pand mm3, [cw255]
    paddw mm2, mm3             ; add
    psrlw mm4, 8
    pand mm4, [cw255]
    paddw mm2, mm4             ; add
    psrlw mm2, 2               ; div 4

    packuswb mm1, mm1
    packuswb mm2, mm2

    punpcklbw mm1, mm2         ; uv
    movq [edx], mm1            ; out 8 bytes uvuvuvuv

    ; go up to first line
    sub esi, LSRC_STRIDE
    sub edi, LDST_STRIDE

    ; move right
    lea esi, [esi + 32]
    lea edi, [edi + 8]
    lea edx, [edx + 8]

    dec ecx
    jnz loop1

    ; update s8
    mov eax, LS8               ; s8
    add eax, LSRC_STRIDE       ; s8 += src_stride
    add eax, LSRC_STRIDE       ; s8 += src_stride
    mov LS8, eax

    ; update d8_y
    mov eax, LD8_Y             ; d8_y
    add eax, LDST_STRIDE       ; d8_y += dst_stride
    add eax, LDST_STRIDE       ; d8_y += dst_stride
    mov LD8_Y, eax

    ; update d8_uv
    mov eax, LD8_UV            ; d8_uv
    add eax, LDST_STRIDE       ; d8_uv += dst_stride
    mov LD8_UV, eax

    dec ebx
    jnz row_loop1

    mov eax, 0                 ; return value
    add esp, 36                ; local vars, 36 bytes
    pop ebp
    pop edi
    pop esi
    pop ebx
    ret
    align 16

