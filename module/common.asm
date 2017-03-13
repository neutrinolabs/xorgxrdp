;
;Copyright 2017 Pavel Roskin
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
