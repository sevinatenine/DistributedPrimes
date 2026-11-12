#pragma once

typedef struct {
    unsigned long long k;
    unsigned long long i;
    int flip;
} RectIter;

void rect_iter_init_at(RectIter *it, unsigned long long n);
void rect_iter_peek(const RectIter *it, unsigned long long *w, unsigned long long *h);
void rect_iter_continue(RectIter *it);
void rect_iter_next(RectIter *it, unsigned long long *w, unsigned long long *h);
void rect_at(unsigned long long n, unsigned long long *w, unsigned long long *h);