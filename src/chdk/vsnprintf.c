/*
 * vsnprintf.c - the one libc function the engine needs that CHDK does not
 * export. se_log formats every line through it.
 *
 * This is the 5D payload's own implementation, unchanged, from the Magic
 * Lantern build this project began as. tools/chdktest.c checks it against
 * the system's for every format the engine uses.
 */
#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>

/*
 * Supports exactly what the engine and the menu use: %d %u %x %X %s %c %%,
 * a field width, zero padding, '-' left alignment, and the 'l' length
 * modifier (which is a no-op here -- int and long are both 32-bit on this
 * target). %f is deliberately absent: there is no FPU on an ARM946E-S and
 * the engine is fixed-point throughout, so a float in a format string would
 * be a bug worth failing loudly on rather than printing.
 *
 * Always NUL-terminates when n > 0, and returns the length it *would* have
 * written, like C99 -- se_log relies on neither, but the contract is cheap
 * to honour and surprising to get wrong.
 */
typedef struct {
    char  *buf;
    size_t cap;    /* bytes usable for text, i.e. size - 1 */
    size_t len;    /* total characters that would be written */
} vbuf;

static void vb_putc(vbuf *v, char c)
{
    if (v->len < v->cap) v->buf[v->len] = c;
    v->len++;
}

static void vb_pad(vbuf *v, char c, int n)
{
    while (n-- > 0) vb_putc(v, c);
}

static void vb_str(vbuf *v, const char *s, int width, int left)
{
    int l = 0;
    const char *p = s;
    while (*p) { l++; p++; }
    if (!left) vb_pad(v, ' ', width - l);
    while (*s) vb_putc(v, *s++);
    if (left) vb_pad(v, ' ', width - l);
}

static void vb_num(vbuf *v, uint32_t mag, int neg, int base, int upper,
                   int width, int zero, int left)
{
    char t[12];
    int  i = 0, l, pad;
    const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";

    if (mag == 0) t[i++] = '0';
    while (mag) { t[i++] = digits[mag % (uint32_t)base]; mag /= (uint32_t)base; }

    l   = i + (neg ? 1 : 0);
    pad = width - l;

    /* "%-04d" is left alignment with the zero flag ignored, as in C. */
    if (left) {
        if (neg) vb_putc(v, '-');
        while (i) vb_putc(v, t[--i]);
        vb_pad(v, ' ', pad);
        return;
    }
    if (zero) {
        if (neg) vb_putc(v, '-');
        vb_pad(v, '0', pad);
    } else {
        vb_pad(v, ' ', pad);
        if (neg) vb_putc(v, '-');
    }
    while (i) vb_putc(v, t[--i]);
}

int vsnprintf(char *buf, size_t n, const char *fmt, va_list ap)
{
    vbuf v;
    v.buf = buf;
    v.cap = n ? n - 1 : 0;
    v.len = 0;

    while (*fmt) {
        int width = 0, zero = 0, left = 0;

        if (*fmt != '%') { vb_putc(&v, *fmt++); continue; }
        fmt++;

        for (;;) {
            if (*fmt == '0')      { zero = 1; fmt++; }
            else if (*fmt == '-') { left = 1; fmt++; }
            else break;
        }
        while (*fmt >= '0' && *fmt <= '9') width = width * 10 + (*fmt++ - '0');
        while (*fmt == 'l' || *fmt == 'h' || *fmt == 'z') fmt++;

        switch (*fmt) {
        case 'd': case 'i': {
            int32_t  sv = (int32_t)va_arg(ap, int);
            /* -2147483648 has no positive counterpart, so negate in unsigned */
            uint32_t mg = sv < 0 ? (uint32_t)0 - (uint32_t)sv : (uint32_t)sv;
            vb_num(&v, mg, sv < 0, 10, 0, width, zero, left);
            break;
        }
        case 'u': vb_num(&v, va_arg(ap, unsigned), 0, 10, 0, width, zero, left); break;
        case 'x': vb_num(&v, va_arg(ap, unsigned), 0, 16, 0, width, zero, left); break;
        case 'X': vb_num(&v, va_arg(ap, unsigned), 0, 16, 1, width, zero, left); break;
        case 'c': vb_pad(&v, ' ', left ? 0 : width - 1);
                  vb_putc(&v, (char)va_arg(ap, int));
                  vb_pad(&v, ' ', left ? width - 1 : 0);
                  break;
        case 's': {
            const char *s = va_arg(ap, const char *);
            vb_str(&v, s ? s : "(null)", width, left);
            break;
        }
        case '%': vb_putc(&v, '%'); break;
        case '\0': goto done;
        default:  vb_putc(&v, '%'); vb_putc(&v, *fmt); break;
        }
        fmt++;
    }
done:
    if (n) buf[v.len < v.cap ? v.len : v.cap] = '\0';
    return (int)v.len;
}
