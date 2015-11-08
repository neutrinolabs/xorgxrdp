
OBJS = yuv2rgb_speed.o

CFLAGS = -Wall -O2

LDFLAGS =

all: yuv2rgb_speed

yuv2rgb_speed: $(OBJS)
	$(CC) -o yuv2rgb_speed $(OBJS) $(LDFLAGS)

clean:
	rm -f $(OBJS) yuv2rgb_speed


