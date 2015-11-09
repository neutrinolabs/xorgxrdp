
#OBJS = yuv2rgb_speed.o a8r8g8b8_to_nv12_box_x86_sse2.o
OBJS = yuv2rgb_speed.o a8r8g8b8_to_nv12_box_amd64_sse2.o

CFLAGS = -Wall -O2

LDFLAGS =

all: yuv2rgb_speed

yuv2rgb_speed: $(OBJS)
	$(CC) -o yuv2rgb_speed $(OBJS) $(LDFLAGS)

clean:
	rm -f $(OBJS) yuv2rgb_speed

a8r8g8b8_to_nv12_box_x86_sse2.o: ../../module/x86/a8r8g8b8_to_nv12_box_x86_sse2.asm
	yasm -f elf32 ../../module/x86/a8r8g8b8_to_nv12_box_x86_sse2.asm

a8r8g8b8_to_nv12_box_amd64_sse2.o: ../../module/amd64/a8r8g8b8_to_nv12_box_amd64_sse2.asm
	yasm -f elf64 ../../module/amd64/a8r8g8b8_to_nv12_box_amd64_sse2.asm
