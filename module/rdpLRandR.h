#ifndef _RDPLRANDR_H
#define _RDPLRANDR_H

int
rdpLRRInit(rdpPtr dev);
Bool
rdpLRRScreenSizeSet(rdpPtr dev, int width, int height,
                    int mmWidth, int mmHeight);
Bool
rdpLRRSetRdpOutputs(rdpPtr dev);

#endif
