// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2024 Mathieu Desnoyers <mathieu.desnoyers@efficios.com>

#ifndef _RSEQ_ALLOC_UTILS_H
#define _RSEQ_ALLOC_UTILS_H

#define __rseq_align_mask(v, mask)	(((v) + (mask)) & ~(mask))
#define rseq_align(v, align)		__rseq_align_mask(v, (__typeof__(v)) (align) - 1)

static inline
unsigned int rseq_fls_u64(uint64_t x)
{
	unsigned int r = 64;

	if (!x)
		return 0;

	if (!(x & 0xFFFFFFFF00000000ULL)) {
		x <<= 32;
		r -= 32;
	}
	if (!(x & 0xFFFF000000000000ULL)) {
		x <<= 16;
		r -= 16;
	}
	if (!(x & 0xFF00000000000000ULL)) {
		x <<= 8;
		r -= 8;
	}
	if (!(x & 0xF000000000000000ULL)) {
		x <<= 4;
		r -= 4;
	}
	if (!(x & 0xC000000000000000ULL)) {
		x <<= 2;
		r -= 2;
	}
	if (!(x & 0x8000000000000000ULL)) {
		x <<= 1;
		r -= 1;
	}
	return r;
}

static inline
unsigned int rseq_fls_u32(uint32_t x)
{
	unsigned int r = 32;

	if (!x)
		return 0;
	if (!(x & 0xFFFF0000U)) {
		x <<= 16;
		r -= 16;
	}
	if (!(x & 0xFF000000U)) {
		x <<= 8;
		r -= 8;
	}
	if (!(x & 0xF0000000U)) {
		x <<= 4;
		r -= 4;
	}
	if (!(x & 0xC0000000U)) {
		x <<= 2;
		r -= 2;
	}
	if (!(x & 0x80000000U)) {
		x <<= 1;
		r -= 1;
	}
	return r;
}

static inline
unsigned int rseq_fls_ulong(unsigned long x)
{
#if RSEQ_BITS_PER_LONG == 32
	return rseq_fls_u32(x);
#else
	return rseq_fls_u64(x);
#endif
}

/*
 * Return the minimum order for which x <= (1UL << order).
 * Return -1 if x is 0.
 */
static inline
int rseq_get_count_order_ulong(unsigned long x)
{
	if (!x)
		return -1;

	return rseq_fls_ulong(x - 1);
}

#define RSEQ_DEFAULT_PAGE_SIZE	4096

static inline
long rseq_get_page_len(void)
{
	long page_len = sysconf(_SC_PAGE_SIZE);

	if (page_len < 0)
		page_len = RSEQ_DEFAULT_PAGE_SIZE;
	return page_len;
}

#endif /* _RSEQ_ALLOC_UTILS_H */