#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include "rect.h"

static unsigned long long isqrt_ull(unsigned long long n) {
    unsigned long long r = (unsigned long long)sqrtl((long double)n);
    while (r * r > n) r--;
    while ((r + 1) * (r + 1) <= n) r++;
    return r;
}

void rect_iter_init_at(RectIter *it, unsigned long long n) {
    unsigned long long k = isqrt_ull(n) + 1;
    unsigned long long offset = n - (k - 1) * (k - 1);

    it->k = k;
    it->i = offset / 2 + 1;
    it->flip = (int)(offset % 2);
}
void rect_iter_peek(const RectIter *it, unsigned long long *w, unsigned long long *h) {
    if (it->i == it->k) {
        *w = it->k;
        *h = it->k;
    } else if (it->flip == 0) {
        *w = it->i;
        *h = it->k;
    } else {
        *w = it->k;
        *h = it->i;
    }
}

void rect_iter_continue(RectIter *it) {
    if (it->i == it->k) {
        it->k++;
        it->i = 1;
        it->flip = 0;
    } else if (it->flip == 0) {
        it->flip = 1;
    } else {
        it->flip = 0;
        it->i++;
    }
}

void rect_iter_next(RectIter *it, unsigned long long *w, unsigned long long *h) {
    rect_iter_peek(it, w, h);
    rect_iter_continue(it);
}

void rect_at(unsigned long long n, unsigned long long *w, unsigned long long *h) {
    RectIter it;
    rect_iter_init_at(&it, n);
    rect_iter_peek(&it, w, h);
}