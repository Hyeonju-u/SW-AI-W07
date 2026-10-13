/* This file is derived from source code for the Nachos
   instructional operating system.  The Nachos copyright notice
   is reproduced in full below. */

/* Copyright (c) 1992-1996 The Regents of the University of California.
   All rights reserved.

   Permission to use, copy, modify, and distribute this software
   and its documentation for any purpose, without fee, and
   without written agreement is hereby granted, provided that the
   above copyright notice and the following two paragraphs appear
   in all copies of this software.

   IN NO EVENT SHALL THE UNIVERSITY OF CALIFORNIA BE LIABLE TO
   ANY PARTY FOR DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR
   CONSEQUENTIAL DAMAGES ARISING OUT OF THE USE OF THIS SOFTWARE
   AND ITS DOCUMENTATION, EVEN IF THE UNIVERSITY OF CALIFORNIA
   HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

   THE UNIVERSITY OF CALIFORNIA SPECIFICALLY DISCLAIMS ANY
   WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
   WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
   PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS ON AN "AS IS"
   BASIS, AND THE UNIVERSITY OF CALIFORNIA HAS NO OBLIGATION TO
   PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR
   MODIFICATIONS.
   */

#include "threads/synch.h"
#include <stdio.h>
#include <string.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

/* Initializes semaphore SEMA to VALUE.  A semaphore is a
   nonnegative integer along with two atomic operators for
   manipulating it:

   - down or "P": wait for the value to become positive, then
   decrement it.

   - up or "V": increment the value (and wake up one waiting
   thread, if any). */

// list_entry(list_front(&cond->waiters), struct semaphore_elem, elem);

void sema_init(struct semaphore *sema, unsigned value)
{
	ASSERT(sema != NULL);

	sema->value = value;
	list_init(&sema->waiters);
}

/* Down or "P" operation on a semaphore.  Waits for SEMA's value
   to become positive and then atomically decrements it.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but if it sleeps then the next scheduled
   thread will probably turn interrupts back on. This is
   sema_down function. */
void sema_down(struct semaphore *sema)
{ // 웨이터에 우선 순위 순으로 넣기
	enum intr_level old_level;

	ASSERT(sema != NULL);
	ASSERT(!intr_context());

	old_level = intr_disable(); // 인터럽트 비활성화로 끼어드는거 막기
	while (sema->value == 0)    // 남은 개수가 없는 동안
	{
		// list_push_back(&sema->waiters, &thread_current()->elem);
		list_insert_ordered(&sema->waiters, &thread_current()->elem, compare_pr, NULL);
		// 대기 줄에 서기 우선순위로 정렬 추가

		thread_block(); // 스레드 잠듦
	} // 누가 깨우면 다시 while 문 반복
	sema->value--; // 값에서 하나 가져감
	intr_set_level(old_level);
}

/* Down or "P" operation on a semaphore, but only if the
   semaphore is not already 0.  Returns true if the semaphore is
   decremented, false otherwise.

   This function may be called from an interrupt handler. */
bool sema_try_down(struct semaphore *sema)
{
	enum intr_level old_level;
	bool success;

	ASSERT(sema != NULL);

	old_level = intr_disable();
	if (sema->value > 0)
	{
		sema->value--;
		success = true;
	}
	else
		success = false;
	intr_set_level(old_level);

	return success;
}

/* Up or "V" operation on a semaphore.  Increments SEMA's value
   and wakes up one thread of those waiting for SEMA, if any.

   This function may be called from an interrupt handler. */
void sema_up(struct semaphore *sema)
{ // 공유자원의 사용을 마친 스레드가 호출함 대기중인 스레드 리스트가 비어있지않다면 스레드를 하나 꺼내서 실행 가능 상태로 깨워줌
	enum intr_level old_level;

	ASSERT(sema != NULL);

	old_level = intr_disable();
	if (!list_empty(&sema->waiters))
	{
		list_sort(&sema->waiters, compare_pr, NULL);					 // 웨이터에 다시정렬
		thread_unblock(list_entry(list_pop_front(&sema->waiters), struct thread, elem)); // 맨 앞 한스레드 깨움
	}
	sema->value++;
	// 양보검사 레디리스트 맨 앞이 나보다 높으면 양보

	intr_set_level(old_level);
	priority_yield(); // 깨운애가 더 높으면 비켜줌
}

static void sema_test_helper(void *sema_);

/* Self-test for semaphores that makes control "ping-pong"
   between a pair of threads.  Insert calls to printf() to see
   what's going on. */
void sema_self_test(void)
{
	struct semaphore sema[2];
	int i;

	printf("Testing semaphores...");
	sema_init(&sema[0], 0);
	sema_init(&sema[1], 0);
	thread_create("sema-test", PRI_DEFAULT, sema_test_helper, &sema);
	for (i = 0; i < 10; i++)
	{
		sema_up(&sema[0]);
		sema_down(&sema[1]);
	}
	printf("done.\n");
}

/* Thread function used by sema_self_test(). */
static void
sema_test_helper(void *sema_)
{
	struct semaphore *sema = sema_;
	int i;

	for (i = 0; i < 10; i++)
	{
		sema_down(&sema[0]);
		sema_up(&sema[1]);
	}
}

/* Initializes LOCK.  A lock can be held by at most a single
   thread at any given time.  Our locks are not "recursive", that
   is, it is an error for the thread currently holding a lock to
   try to acquire that lock.

   A lock is a specialization of a semaphore with an initial
   value of 1.  The difference between a lock and such a
   semaphore is twofold.  First, a semaphore can have a value
   greater than 1, but a lock can only be owned by a single
   thread at a time.  Second, a semaphore does not have an owner,
   meaning that one thread can "down" the semaphore and then
   another one "up" it, but with a lock the same thread must both
   acquire and release it.  When these restrictions prove
   onerous, it's a good sign that a semaphore should be used,
   instead of a lock. */
void lock_init(struct lock *lock)
{
	ASSERT(lock != NULL);

	lock->holder = NULL;
	sema_init(&lock->semaphore, 1);
}

/* Acquires LOCK, sleeping until it becomes available if
   necessary.  The lock must not already be held by the current
   thread.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep. */
void lock_acquire(struct lock *lock)
{ // 락을 바로 못 얻으면(이미 누가 갖고있으면) 그 락의 주소를 저장해둔다 내가 어떤 락을 기다리는지 기록 하는것 wait_on_lock 현재 우선순위를 저장해두고 기부받은 스레드 들을 리스트에 유지한다(멀티플 도네이션) 락 여러개에서 각각 기부가 들어올 수 있으니 리스트로 관리, 우선순위를 기부, 락 홀더에게 내 높은 우선 순위를 넘겨주는 단계
	ASSERT(lock != NULL);
	ASSERT(!intr_context());
	ASSERT(!lock_held_by_current_thread(lock));
	struct thread *curr = thread_current();

	if (lock->holder != NULL) // holder를 가지고있으면
	{
		curr->wait_on_lock = lock;
		//  lock을 curr->wait_on_lock 리스트에 넣어서 기다림
		list_push_back(&lock->holder->donation, &curr->donation_elem); // holder 명단에 내 donation 요소를 넣음

		if (lock->holder->priority < curr->priority)

			lock->holder->priority = curr->priority;
		// holder의 우선순위 보다 내 우선순위가 크면 내 우선순위를 holder의 우선순위 값으로
	}
	sema_down(&lock->semaphore);
	// 락 받을때까지 재움
	// 공유자원을 요청하는 함수자원이 가용상태이면 값을 감소하고 현재스레드가 계속 실행 자원없으면 현재스레드를 대기에 넣고 스레드 재움

	curr->wait_on_lock = NULL; // 락을 받았으니  기다리던 기록 지움
	lock->holder = curr;	   // 나를 holder로 기록
}

/* Tries to acquires LOCK and returns true if successful or false
   on failure.  The lock must not already be held by the current
   thread.

   This function will not sleep, so it may be called within an
   interrupt handler. */
bool lock_try_acquire(struct lock *lock)
{
	bool success;

	ASSERT(lock != NULL);
	ASSERT(!lock_held_by_current_thread(lock));

	success = sema_try_down(&lock->semaphore);
	if (success)
		lock->holder = thread_current();
	return success;
}

/* Releases LOCK, which must be owned by the current thread.
   This is lock_release function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to release a lock within an interrupt
   handler. */
void lock_release(struct lock *lock)
{ // 락을 풀때 그 락때문에 기부해준 스레드를 도네이션 리스트에서 제거를 하고 우선순위를 적절하게 재설정하기
	ASSERT(lock != NULL);
	ASSERT(lock_held_by_current_thread(lock));
	struct thread *curr = thread_current();
	struct list_elem *e = list_begin(&curr->donation);
	// 기부한 명단 처음부터 시작
	while (e != list_end(&curr->donation)) // 명단끝까지 가서 e가 빌때까지
	{
		struct thread *t = list_entry(e, struct thread, donation_elem);
		if (t->wait_on_lock == lock)
			// 기부자가 기다린 락이랑 지금 락이랑 같으면
			e = list_remove(e); // 명단에서 제거
		else
			e = list_next(e); // 아니면 그냥 다음칸으로
	}
	// 우선순위 다시 계산
	curr->priority = curr->init_priority; // 원래 우선순위로 설정
	e = list_begin(&curr->donation);      // e를 명단 처음에 놓기
	while (e != list_end(&curr->donation))
	{
		struct thread *t = list_entry(e, struct thread, donation_elem);
		if (t->priority > curr->priority)
			curr->priority = t->priority; // t 우선순위를 내 우선순위로 설정
		e = list_next(e);		      // 아니면 다음으로 이동
	}

	lock->holder = NULL;
	sema_up(&lock->semaphore);
}

/* Returns true if the current thread holds LOCK, false
   otherwise.  (Note that testing whether some other thread holds
   a lock would be racy.) */
bool lock_held_by_current_thread(const struct lock *lock)
{
	ASSERT(lock != NULL);

	return lock->holder == thread_current();
}

/* One semaphore in a list. */
struct semaphore_elem
{
	struct list_elem elem;	    /* List element. */
	struct semaphore semaphore; /* This semaphore. */
};

bool compare_sema(const struct list_elem *a, const struct list_elem *b, void *aux UNUSED)
//
{
	struct semaphore_elem *sa = list_entry(a, struct semaphore_elem, elem);
	struct semaphore_elem *sb = list_entry(b, struct semaphore_elem, elem);

	struct thread *ta = list_entry(list_front(&sa->semaphore.waiters), struct thread, elem);
	struct thread *tb = list_entry(list_front(&sb->semaphore.waiters), struct thread, elem);

	return ta->priority > tb->priority;
}

/* Initializes condition variable COND.  A condition variable
   allows one piece of code to signal a condition and cooperating
   code to receive the signal and act upon it. */
void cond_init(struct condition *cond)
{
	ASSERT(cond != NULL);

	list_init(&cond->waiters);
}

/* Atomically releases LOCK and waits for COND to be signaled by
   some other piece of code.  After COND is signaled, LOCK is
   reacquired before returning.  LOCK must be held before calling
   this function.

   The monitor implemented by this function is "Mesa" style, not
   "Hoare" style, that is, sending and receiving a signal are not
   an atomic operation.  Thus, typically the caller must recheck
   the condition after the wait completes and, if necessary, wait
   again.

   A given condition variable is associated with only a single
   lock, but one lock may be associated with any number of
   condition variables.  That is, there is a one-to-many mapping
   from locks to condition variables.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep. */
void cond_wait(struct condition *cond, struct lock *lock)
{

	struct semaphore_elem waiter;

	ASSERT(cond != NULL);
	ASSERT(lock != NULL);
	ASSERT(!intr_context());
	ASSERT(lock_held_by_current_thread(lock));

	sema_init(&waiter.semaphore, 0); // vaule 0
	list_push_back(&cond->waiters, &waiter.elem);
	lock_release(lock);
	sema_down(&waiter.semaphore);
	lock_acquire(lock);
}

/* If any threads are waiting on COND (protected by LOCK), then
   this function signals one of them to wake up from its wait.
   LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
void cond_signal(struct condition *cond, struct lock *lock UNUSED)
{
	ASSERT(cond != NULL);
	ASSERT(lock != NULL);
	ASSERT(!intr_context());
	ASSERT(lock_held_by_current_thread(lock));

	if (!list_empty(&cond->waiters))
	{
		list_sort(&cond->waiters, compare_sema, NULL);
		// 웨이터 우선 순위 다시 정렬

		sema_up(&list_entry(list_pop_front(&cond->waiters), struct semaphore_elem, elem)
			     ->semaphore);
	}
}

/* Wakes up all threads, if any, waiting on COND (protected by
   LOCK).  LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
void cond_broadcast(struct condition *cond, struct lock *lock)
{
	ASSERT(cond != NULL);
	ASSERT(lock != NULL);

	while (!list_empty(&cond->waiters))
		cond_signal(cond, lock);
}
