#include "serialio.h"


// https://dcreager.net/2021/03/a-better-varint/
// https://john-millikin.com/vu128-efficient-variable-length-integers

unsigned vu128enc(unsigned char dst[5], unsigned x)
{
    if(x < 0x80)
    {
        dst[0] = x;
        return 1;
    }
	if(x < 0x10000000)
    {
		if(x < 0x00004000)
        {
			x <<= 2u;
			dst[0] = 0x80 | ((x & 0xffu) >> 2u);
			dst[1] = x >> 8;
			return 2;
		}
		if(x < 0x00200000)
        {
			x <<= 3u;
			dst[0] = 0xC0 | ((x & 0xffu) >> 3u);
			dst[1] = x >> 8;
			dst[2] = x >> 16;
			return 3;
		}
		x <<= 4u;
		dst[0] = 0xE0 | ((x & 0xffu) >> 4u);
		dst[1] = x >> 8u;
		dst[2] = x >> 16u;
		dst[3] = x >> 24u;
		return 4;
	}

    dst[0] = 0xf3;
    dst[1] = (unsigned char)(x);
    dst[2] = (unsigned char)(x >> 8u);
    dst[3] = (unsigned char)(x >> 16u);
    dst[4] = (unsigned char)(x >> 24u);

    return 5;
}


vudec vu128dec(const unsigned char *src)
{
	unsigned x = src[0];
    unsigned n; // = clz(x) + 1
    if(!(x & 0x80))
		n = 1;
    else if(!(x & 0x40))
    {
        x &= 0x3f;
        x |= (src[1] << 6u);
        n = 2;
    }
    else if(!(x & 0x20))
    {
        x &= 0x1f;
        x |= (src[1] << 5u);
        x |= (src[2] << 13u);
        n = 3;
    }
    else if(x < 0xf0)
    {
        x &= 0x0f;
        x |= src[1] << 4u;
        x |= src[2] << 12u;
        x |= src[3] << 20u;
        n = 4;
    }
    else
    {
        n = (x & 0x0f) + 2;
        x = (src[1]) | (src[2] << 8u) | (src[3] << 16u) | (src[4] << 24u);
    }

    vudec d;
    d.adv = n;
    d.val = x;
    return d;
}
