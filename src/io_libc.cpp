#include "io_libc.h"
#include "serialio.h"
#include <stdio.h>

int sinknop(BufSink *sk)
{
    return 0;
}

void sinkdummy(BufSink *sk)
{
}

void hexclose(BufSink *sk)
{
    puts("");
}

int hexwrite(BufSink *sk, const void *mem, size_t n)
{
    const byte *b = (const byte*)mem;
    size_t rem = sk->priv.sz;
    for(size_t i = 0; i < n; ++i)
    {
        printf(" %02X", b[i]);
        if(!--rem)
        {
            puts("");
            rem = 8;
        }
    }
    sk->priv.sz = rem;
    return 0;
}

void sink_initHexPrint(BufSink* sk)
{
    sk->err = 0;
    sk->priv.sz = 8;
    sk->Flush = sinknop;
    sk->Write = hexwrite;
    sk->Close = hexclose;
}
