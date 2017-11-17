;
;Copyright 2017 Pavel Roskin
;Copyright 2017 mirabilos
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
;Common nasm code
;

; Detect ELF formats
%ifidn __OUTPUT_FORMAT__,elf
%define is_elf 1
%endif

%ifidn __OUTPUT_FORMAT__,elf32
%define is_elf 1
%endif

%ifidn __OUTPUT_FORMAT__,elf64
%define is_elf 1
%endif

; Detect Mach-O formats
%ifidn __OUTPUT_FORMAT__,macho
%define is_macho 1
%endif

%ifidn __OUTPUT_FORMAT__,macho32
%define is_macho 1
%endif

%ifidn __OUTPUT_FORMAT__,macho64
%define is_macho 1
%endif

; Mark stack non-executable
%ifdef is_elf
section .note.GNU-stack noalloc noexec nowrite progbits
%endif

; Global function header
%macro PROC 1
    align 16
%ifdef is_elf
    global %1:function
    %1:
%else
    global _%1
    _%1:
%endif
%endmacro

; Macros for relative access to local data
%undef lsym

%ifdef ASM_ARCH_AMD64
; amd64; don't define or call RETRIEVE_RODATA
%define lsym(name) rel name
%endif

%ifdef ASM_ARCH_I386
%ifdef PIC
; i386 PIC

%macro END_OF_FILE 0
%ifdef I386_PIC_NEEDED
section .text
..@get_caller_address:
	mov ebx, [esp]
	ret
%endif
%ifdef is_macho
; see below
    align 16
%endif
%endmacro

%macro RETRIEVE_RODATA 0
%define I386_PIC_NEEDED 1
	call ..@get_caller_address
%%the_caller_address:
	sub ebx, %%the_caller_address - ..@rodata_begin
%endmacro

%define lsym(name) ebx + name - ..@rodata_begin
%else
; i386 non-PIC; default case for lsym and RETRIEVE_RODATA
%endif
%endif

%ifndef lsym
%macro RETRIEVE_RODATA 0
%endmacro
%define lsym(name) name
%endif

%macro PREPARE_RODATA 0
section .text
    align 16
..@rodata_begin:
%endmacro

%ifnmacro END_OF_FILE 0
%macro END_OF_FILE 0
%ifdef is_macho
; cf. https://github.com/libjpeg-turbo/libjpeg-turbo/blob/master/simd/jccolext-mmx.asm#L474-L476
    align 16
%endif
%endmacro
%endif

section .text
