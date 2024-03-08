// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2024 Mathieu Desnoyers <mathieu.desnoyers@efficios.com>
/*
 * rseq memory pool test.
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <assert.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <inttypes.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

#include <rseq/mempool.h>

#include "list.h"
#include "tap.h"

struct test_data {
	uintptr_t value;
	struct test_data __rseq_percpu *backref;
	struct list_head node;
};

static void test_mempool_fill(size_t stride)
{
	struct test_data __rseq_percpu *ptr;
	struct test_data *iter, *tmp;
	struct rseq_mempool *mempool;
	struct rseq_mempool_attr *attr;
	uint64_t count = 0;
	LIST_HEAD(list);
	int ret, i;

	attr = rseq_mempool_attr_create();
	ok(attr, "Create pool attribute");
	ret = rseq_mempool_attr_set_robust(attr);
	ok(ret == 0, "Setting mempool robust attribute");

	mempool = rseq_mempool_create("test_data",
			sizeof(struct test_data),
			stride, CPU_SETSIZE, attr);
	ok(mempool, "Create mempool of size %zu", stride);
	rseq_mempool_attr_destroy(attr);

	for (;;) {
		struct test_data *cpuptr;

		ptr = (struct test_data __rseq_percpu *) rseq_mempool_percpu_zmalloc(mempool);
		if (!ptr)
			break;
		/* Link items in cpu 0. */
		cpuptr = rseq_percpu_ptr(ptr, 0, stride);
		cpuptr->backref = ptr;
		/* Randomize items in list. */
		if (count & 1)
			list_add(&cpuptr->node, &list);
		else
			list_add_tail(&cpuptr->node, &list);
		count++;
	}

	ok(count * sizeof(struct test_data) == stride, "Allocated %" PRIu64 " objects in pool", count);

	list_for_each_entry(iter, &list, node) {
		ptr = iter->backref;
		for (i = 0; i < CPU_SETSIZE; i++) {
			struct test_data *cpuptr = rseq_percpu_ptr(ptr, i, stride);

			if (cpuptr->value != 0)
				abort();
			cpuptr->value++;
		}
	}

	ok(1, "Check for pool content corruption");

	list_for_each_entry_safe(iter, tmp, &list, node) {
		ptr = iter->backref;
		rseq_mempool_percpu_free(ptr, stride);
	}
	ret = rseq_mempool_destroy(mempool);
	ok(ret == 0, "Destroy mempool");
}

static void test_robust_double_free(struct rseq_mempool *pool)
{
	struct test_data __rseq_percpu *ptr;

	ptr = (struct test_data __rseq_percpu *) rseq_mempool_percpu_malloc(pool);

	rseq_mempool_percpu_free(ptr);
	rseq_mempool_percpu_free(ptr);
}

static void test_robust_corrupt_after_free(struct rseq_mempool *pool)
{
	struct test_data __rseq_percpu *ptr;
	struct test_data *cpuptr;

	ptr = (struct test_data __rseq_percpu *) rseq_mempool_percpu_malloc(pool);
	cpuptr = (struct test_data *) rseq_percpu_ptr(ptr, 0);

	rseq_mempool_percpu_free(ptr);
	cpuptr->value = (uintptr_t) test_robust_corrupt_after_free;

	rseq_mempool_destroy(pool);
}

static void test_robust_memory_leak(struct rseq_mempool *pool)
{
	(void) rseq_mempool_percpu_malloc(pool);

	rseq_mempool_destroy(pool);
}

static void test_robust_free_list_corruption(struct rseq_mempool *pool)
{
	struct test_data __rseq_percpu *ptr;
	struct test_data *cpuptr;

	ptr = (struct test_data __rseq_percpu *) rseq_mempool_percpu_malloc(pool);
	cpuptr = (struct test_data *) rseq_percpu_ptr(ptr, 0);

	rseq_mempool_percpu_free(ptr);

	cpuptr->value = (uintptr_t) cpuptr;

	(void) rseq_mempool_percpu_malloc(pool);
	(void) rseq_mempool_percpu_malloc(pool);
}

static int run_robust_test(void (*test)(struct rseq_mempool*),
			struct rseq_mempool *pool)
{
	pid_t cpid;
	int status;

	cpid = fork();

	switch (cpid) {
	case -1:
		return 0;
	case 0:
		test(pool);
		_exit(EXIT_FAILURE);
	default:
		waitpid(cpid, &status, 0);
	}

	if (WIFSIGNALED(status) &&
	    (SIGABRT == WTERMSIG(status)))
		return 1;

	return 0;
}

static void run_robust_tests(void)
{
	struct rseq_mempool_attr *attr;
	struct rseq_mempool *pool;

	attr = rseq_mempool_attr_create();

	rseq_mempool_attr_set_robust(attr);

	pool = rseq_mempool_create("mempool-robust",
				sizeof(void*), RSEQ_PERCPU_STRIDE, 1,
				attr);

	rseq_mempool_attr_destroy(attr);

	ok(run_robust_test(test_robust_double_free, pool),
		"robust-double-free");

	ok(run_robust_test(test_robust_corrupt_after_free, pool),
		"robust-corrupt-after-free");

	ok(run_robust_test(test_robust_memory_leak, pool),
		"robust-memory-leak");

	ok(run_robust_test(test_robust_free_list_corruption, pool),
		"robust-free-list-corruption");

	rseq_mempool_destroy(pool);
}

int main(void)
{
	size_t len;

	plan_no_plan();

	/* From 4kB to 4MB */
	for (len = 4096; len < 4096 * 1024; len <<= 1) {
		test_mempool_fill(len);
	}

	run_robust_tests();

	exit(exit_status());
}
