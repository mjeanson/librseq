/* SPDX-License-Identifier: MIT */
/* SPDX-FileCopyrightText: 2016-2022 Mathieu Desnoyers <mathieu.desnoyers@efficios.com> */

/*
 * rseq.h
 */

#ifndef RSEQ_H
#define RSEQ_H

#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>
#include <signal.h>
#include <sched.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sched.h>
#include <stddef.h>
#include <rseq/rseq-abi.h>
#include <rseq/compiler.h>

/*
 * Empty code injection macros, override when testing.
 * It is important to consider that the ASM injection macros need to be
 * fully reentrant (e.g. do not modify the stack).
 */
#ifndef RSEQ_INJECT_ASM
#define RSEQ_INJECT_ASM(n)
#endif

#ifndef RSEQ_INJECT_C
#define RSEQ_INJECT_C(n)
#endif

#ifndef RSEQ_INJECT_INPUT
#define RSEQ_INJECT_INPUT
#endif

#ifndef RSEQ_INJECT_CLOBBER
#define RSEQ_INJECT_CLOBBER
#endif

#ifndef RSEQ_INJECT_FAILED
#define RSEQ_INJECT_FAILED
#endif


/*
 * User code can define RSEQ_GET_ABI_OVERRIDE to override the
 * rseq_get_abi() implementation, for instance to use glibc's symbols
 * directly.
 */
#ifndef RSEQ_GET_ABI_OVERRIDE

# include <rseq/rseq-thread-pointer.h>

# ifdef __cplusplus
extern "C" {
# endif

/* Offset from the thread pointer to the rseq area.  */
extern ptrdiff_t rseq_offset;
/* Size of the registered rseq area.  0 if the registration was
   unsuccessful.  */
extern unsigned int rseq_size;
/* Flags used during rseq registration.  */
extern unsigned int rseq_flags;

static inline struct rseq_abi *rseq_get_abi(void)
{
	return (struct rseq_abi *) ((uintptr_t) rseq_thread_pointer() + rseq_offset);
}

# ifdef __cplusplus
}
# endif

#endif /* RSEQ_GET_ABI_OVERRIDE */

#define rseq_likely(x)		__builtin_expect(!!(x), 1)
#define rseq_unlikely(x)	__builtin_expect(!!(x), 0)
#define rseq_barrier()		__asm__ __volatile__("" : : : "memory")

#define RSEQ_ACCESS_ONCE(x)	(*(__volatile__  __typeof__(x) *)&(x))
#define RSEQ_WRITE_ONCE(x, v)	__extension__ ({ RSEQ_ACCESS_ONCE(x) = (v); })
#define RSEQ_READ_ONCE(x)	RSEQ_ACCESS_ONCE(x)

#define __rseq_str_1(x)	#x
#define __rseq_str(x)		__rseq_str_1(x)

#define rseq_log(fmt, ...)						       \
	fprintf(stderr, fmt "(in %s() at " __FILE__ ":" __rseq_str(__LINE__)"\n", \
		## __VA_ARGS__, __func__)

#define rseq_bug(fmt, ...)		\
	do {				\
		rseq_log(fmt, ## __VA_ARGS__); \
		abort();		\
	} while (0)

#if defined(__x86_64__) || defined(__i386__)
#include <rseq/rseq-x86.h>
#elif defined(__ARMEL__) || defined(__ARMEB__)
#include <rseq/rseq-arm.h>
#elif defined (__AARCH64EL__)
#include <rseq/rseq-arm64.h>
#elif defined(__PPC__)
#include <rseq/rseq-ppc.h>
#elif defined(__mips__)
#include <rseq/rseq-mips.h>
#elif defined(__s390__)
#include <rseq/rseq-s390.h>
#elif defined(__riscv)
#include <rseq/rseq-riscv.h>
#else
#error unsupported target
#endif

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Register rseq for the current thread. This needs to be called once
 * by any thread which uses restartable sequences, before they start
 * using restartable sequences, to ensure restartable sequences
 * succeed. A restartable sequence executed from a non-registered
 * thread will always fail.
 */
int rseq_register_current_thread(void);

/*
 * Unregister rseq for current thread.
 */
int rseq_unregister_current_thread(void);

/*
 * Restartable sequence fallback for reading the current CPU number.
 */
int32_t rseq_fallback_current_cpu(void);

enum rseq_available_query {
	RSEQ_AVAILABLE_QUERY_KERNEL = 0,
	RSEQ_AVAILABLE_QUERY_LIBC = 1,
};

/*
 * Returns true if rseq is supported.
 */
bool rseq_available(unsigned int query);

/*
 * Values returned can be either the current CPU number, -1 (rseq is
 * uninitialized), or -2 (rseq initialization has failed).
 */
static inline int32_t rseq_current_cpu_raw(void)
{
	return RSEQ_READ_ONCE(rseq_get_abi()->cpu_id);
}

/*
 * Returns a possible CPU number, which is typically the current CPU.
 * The returned CPU number can be used to prepare for an rseq critical
 * section, which will confirm whether the cpu number is indeed the
 * current one, and whether rseq is initialized.
 *
 * The CPU number returned by rseq_cpu_start should always be validated
 * by passing it to a rseq asm sequence, or by comparing it to the
 * return value of rseq_current_cpu_raw() if the rseq asm sequence
 * does not need to be invoked.
 */
static inline uint32_t rseq_cpu_start(void)
{
	return RSEQ_READ_ONCE(rseq_get_abi()->cpu_id_start);
}

static inline uint32_t rseq_current_cpu(void)
{
	int32_t cpu;

	cpu = rseq_current_cpu_raw();
	if (rseq_unlikely(cpu < 0))
		cpu = rseq_fallback_current_cpu();
	return cpu;
}

static inline void rseq_clear_rseq_cs(void)
{
	RSEQ_WRITE_ONCE(rseq_get_abi()->rseq_cs.arch.ptr, 0);
}

/*
 * rseq_prepare_unload() should be invoked by each thread executing a rseq
 * critical section at least once between their last critical section and
 * library unload of the library defining the rseq critical section
 * (struct rseq_ab_cs). This also applies to use of rseq in code generated by
 * JIT: rseq_prepare_unload() should be invoked at least once by each
 * thread executing a rseq critical section before reclaim of the memory
 * holding the struct rseq_abi_cs.
 */
static inline void rseq_prepare_unload(void)
{
	rseq_clear_rseq_cs();
}

#ifdef __cplusplus
}
#endif

#endif  /* RSEQ_H_ */
