#pragma once


struct BufStream
{
    /* public, read, modify */
    const char *cursor;   /* Cursor in buffer. Set to begin by Refill() when READING.
                       The valid range is within [begin, end). */

    /* When open for READING: public, MUST NOT be changed by user, changed by Refill() */
    const char *begin;    /* start of buffer */
    const char *end;      /* one past the end */

    /* public, read only */
    int err;  /* != 0 is error. Set by Refill(). Sticky -- once set, stays set. */

    /* public, callable, changed by Refill and Close. */
    int (*Refill)(BufStream *s); /* Required. Never NULL. Sets err on failure. Returns 0 on success, an error code otherwise.
                                    There are 3 failure cases that need to be handled properly:
                                    1) After refill, begin == end, return 0: Call it again. Handled by sm_refill()
                                    2) After refill, begin == end, return != 0, sm->err == 0: Spurious error, try later
                                    3) Return != 0, sm->err != 0: Hard error, stream is dead
                                 */
    void (*Close)(BufStream *s);  /* Required. Must also set cursor, begin, end, Refill, Close to NULL. */

    /* --- Private part. Don't touch, ever. ---
       The implementation may use the underlying memory freely;
       it may or may not follow the struct layout suggested here. */
    struct
    {
        void *a, *b, *c, *d;
        size_t sz;
    } priv;
};

struct BufSink
{
    int (*Write)(BufSink *sk, const void *mem, size_t n);
    int (*Flush)(BufSink *sk);
    void (*Close)(BufSink *sk);
    int err;
    struct
    {
        void *a, *b, *c, *d;
        size_t sz;
    } priv;
};

struct vudec
{
    unsigned val;
    unsigned adv;
};


inline static int zigzagdec(unsigned int x)
{
    return (x >> 1) ^ (-(x&1));
}

inline static unsigned int zigzagenc(int x)
{
    return (2*x) ^ (x >>(sizeof(int) * 8 - 1));
}

unsigned vu128enc(unsigned char dst[5], unsigned x);
vudec vu128dec(const unsigned char *src);
