/* pthread_permit.c
Declares and defines the proposed C1X semaphore object
(C) 2011-2014 Niall Douglas http://www.nedproductions.biz/


Boost Software License - Version 1.0 - August 17th, 2003

Permission is hereby granted, free of charge, to any person or organization
obtaining a copy of the software and accompanying documentation covered by
this license (the "Software") to use, reproduce, display, distribute,
execute, and transmit the Software, and to prepare derivative works of the
Software, and to permit third-parties to whom the Software is furnished to
do so, all subject to the following:

The copyright notices in the Software and this entire statement, including
the above license grant, this restriction and the following disclaimer,
must be included in all copies of the Software, in whole or in part, and
all derivative works of the Software, unless such copies or derivative
works are solely in the form of machine-executable object code generated by
a source language processor.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE, TITLE AND NON-INFRINGEMENT. IN NO EVENT
SHALL THE COPYRIGHT HOLDERS OR ANYONE DISTRIBUTING THE SOFTWARE BE LIABLE
FOR ANY DAMAGES OR OTHER LIABILITY, WHETHER IN CONTRACT, TORT OR OTHERWISE,
ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
DEALINGS IN THE SOFTWARE.
*/

//! The maximum number of pthread_permit_select which can occur simultaneously
#define MAX_PTHREAD_PERMIT_SELECTS 64
//! The magic to use for consuming permits
#define PERMIT_CONSUMING_PERMIT_MAGIC (*(const unsigned *)"CPER")
//! The magic to use for non-consuming permits
#define PERMIT_NONCONSUMING_PERMIT_MAGIC (*(const unsigned *)"NCPR")

#include "pthread_permit.h"
#include <string.h>

#ifdef __cplusplus
PTHREAD_PERMIT_CXX_NAMESPACE_BEGIN
#endif

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#define read _read
#define write _write
#define close _close
#define pipe(fds) _pipe((fds), 4096, _O_BINARY)
  struct pollfd { int fd; short events, revents; };
#define POLLIN 1
#define POLLOUT 2
  // Nasty poll() emulation for Windows
  inline int poll(struct pollfd *fds, size_t nfds, int timeout)
  {
    size_t n, successes=0;
    for(n=0; n<nfds; n++)
    {
      fds[n].revents=0;
      if(fds[n].events&POLLIN)
      {
        // MSVCRT doesn't ask for SYNCHRONIZE permissions in pipe() irritatingly
        //if(WAIT_OBJECT_0==WaitForSingleObject((HANDLE) _get_osfhandle(fds[n].fd), 0)) fds[n].revents|=POLLIN;
        DWORD bytestogo=0;
        PeekNamedPipe((HANDLE) _get_osfhandle(fds[n].fd), NULL, 0, NULL, &bytestogo, NULL);
        if(bytestogo) { fds[n].revents|=POLLIN; successes++; }
      }
      if(fds[n].events&POLLOUT)
      {
        fds[n].revents|=POLLOUT;
        successes++;
      }
    }
    return successes;
  }
#else
#include <unistd.h>
#include <poll.h>
#endif

typedef struct pthread_permit_select_s
{
  atomic_uint magic;                  /* Used to ensure this structure is valid */
  cnd_t cond;                         /* Wakes anything waiting for a permit */
} pthread_permit_select_t;
static pthread_permit_select_t pthread_permit_selects[MAX_PTHREAD_PERMIT_SELECTS];
typedef struct pthread_permit_s pthread_permit_t;
typedef struct pthread_permit_hook_s pthread_permit_hook_t;
typedef struct pthread_permit_hook_s
{
  int (*func)(pthread_permit_hook_type_t type, pthread_permit_t *permit, pthread_permit_hook_t *hookdata);
  void *data;
  pthread_permit_hook_t *next;
} pthread_permit_hook_t;
static char pthread_permitc_hook_t_size_check[sizeof(pthread_permitc_hook_t)==sizeof(pthread_permit_hook_t)];
static char pthread_permitnc_hook_t_size_check[sizeof(pthread_permitnc_hook_t)==sizeof(pthread_permit_hook_t)];
typedef struct pthread_permit_s
{ /* NOTE: KEEP THIS HEADER THE SAME AS pthread_permit1_t to allow its grant() to optionally work here */
  atomic_uint magic;                  /* Used to ensure this structure is valid */
  atomic_uint permit;                 /* =0 no permit, =1 yes permit */
  atomic_uint waiters, waited;        /* Keeps track of when a thread waits and wakes */
  atomic_uint granters, granted;      /* Keeps track of when granters are running */
  cnd_t cond;                         /* Wakes anything waiting for a permit */
  mtx_t internal_mtx;                 /* Used for waits */

  /* Extensions from pthread_permit1_t type */
  unsigned replacePermit;             /* What to replace the permit with when consumed */
  atomic_uint lockWake;               /* Used to exclude new wakers if and only if waiters don't consume */
  pthread_permit_hook_t *PTHREAD_PERMIT_RESTRICT hooks[PTHREAD_PERMIT_HOOK_TYPE_LAST];
  pthread_permit_select_t *volatile PTHREAD_PERMIT_RESTRICT selects[MAX_PTHREAD_PERMIT_SELECTS]; /* select permit parent */
} pthread_permit_t;
static char pthread_permitc_t_size_check[sizeof(pthread_permitc_t)==sizeof(pthread_permit_t)];
static char pthread_permitnc_t_size_check[sizeof(pthread_permitnc_t)==sizeof(pthread_permit_t)];
#define PTHREAD_PERMIT_WAITERS_DONT_CONSUME 1
static void pthread_permit_hide_check_warnings()
{
  (void) pthread_permitc_hook_t_size_check[0];
  (void) pthread_permitnc_hook_t_size_check[0];
  (void) pthread_permitc_t_size_check[0];
  (void) pthread_permitnc_t_size_check[0];
}

static int pthread_permit_init(pthread_permit_t *permit, unsigned magic, unsigned flags, _Bool initial)
{
  int ret;
  if(0) pthread_permit_hide_check_warnings(); // Purely to shut up GCC warnings
  memset(permit, 0, sizeof(pthread_permit_t));
  permit->permit=initial;
  if(thrd_success!=(ret=cnd_init(&permit->cond))) return ret;
  if(thrd_success!=(ret=mtx_init(&permit->internal_mtx, mtx_plain)))
  {
    cnd_destroy(&permit->cond);
    return ret;
  }
  permit->replacePermit=(flags&PTHREAD_PERMIT_WAITERS_DONT_CONSUME)!=0;
  atomic_store_explicit(&permit->magic, magic, memory_order_seq_cst);
  return thrd_success;
}

static int pthread_permit_pushhook(pthread_permit_t *permit, pthread_permit_hook_type_t type, pthread_permit_hook_t *hook)
{
  unsigned expected;
  if(type<0 || type>=PTHREAD_PERMIT_HOOK_TYPE_LAST) return thrd_error;
  // Serialise
  while((expected=0, !atomic_compare_exchange_weak_explicit(&permit->lockWake, &expected, 1U, memory_order_relaxed, memory_order_relaxed)))
  {
    //if(1==cpus) thrd_yield();
  }
  hook->next=permit->hooks[type];
  permit->hooks[type]=hook;
  // Unlock
  permit->lockWake=0;
  return thrd_success;
}

static pthread_permit_hook_t *pthread_permit_pophook(pthread_permit_t *permit, pthread_permit_hook_type_t type)
{
  unsigned expected;
  pthread_permit_hook_t *ret;
  if(type<0 || type>=PTHREAD_PERMIT_HOOK_TYPE_LAST) { return (pthread_permit_hook_t *)(size_t)-1; }
  // Serialise
  while((expected=0, !atomic_compare_exchange_weak_explicit(&permit->lockWake, &expected, 1U, memory_order_relaxed, memory_order_relaxed)))
  {
    //if(1==cpus) thrd_yield();
  }
  ret=permit->hooks[type];
  permit->hooks[type]=ret->next;
  // Unlock
  permit->lockWake=0;
  return ret;
}

static void pthread_permit_destroy(pthread_permit_t *permit)
{
  if(permit->hooks[PTHREAD_PERMIT_HOOK_TYPE_DESTROY])
    permit->hooks[PTHREAD_PERMIT_HOOK_TYPE_DESTROY]->func(PTHREAD_PERMIT_HOOK_TYPE_DESTROY, permit, permit->hooks[PTHREAD_PERMIT_HOOK_TYPE_DESTROY]);
  /* Mark this object as invalid for further use */
  atomic_store_explicit(&permit->magic, 0U, memory_order_seq_cst);
  // Is anything granting? Need to wait until those exit
  while(permit->granters!=permit->granted)
    thrd_yield();
#if 0
  // If non-consuming, serialise all other grants
  if(permit->replacePermit)
  {
    unsigned expected;
    while((expected=0, !atomic_compare_exchange_weak_explicit(&permit->lockWake, &expected, 1U, memory_order_relaxed, memory_order_relaxed)))
    {
      //if(1==cpus) thrd_yield();
    }
  }
#endif
  // Is anything waiting? If so repeatedly grant permit and wake until none
  while(permit->waiters!=permit->waited)
  {
    atomic_store_explicit(&permit->permit, 1U, memory_order_seq_cst);
    cnd_signal(&permit->cond);
  }
  cnd_destroy(&permit->cond);
  mtx_destroy(&permit->internal_mtx);
#if 0
  // Unlock
  permit->lockWake=0;
#endif
}

static int pthread_permit_grant(pthread_permitX_t _permit)
{
  pthread_permit_t *permit=(pthread_permit_t *) _permit;
  int ret=thrd_success;
  size_t n;
  // Increment the monotonic count to indicate we have entered a grant
  atomic_fetch_add_explicit(&permit->granters, 1U, memory_order_acquire);
  // Check again if we have been deleted
  if(!permit->magic)
  {
    atomic_fetch_add_explicit(&permit->granted, 1U, memory_order_relaxed);
    return thrd_error;
  }
  // If permits aren't consumed, prevent any new waiters or granters
  if(permit->replacePermit)
  {
    unsigned expected;
    // Only one grant may occur concurrently if permits aren't consumed
    while((expected=0, !atomic_compare_exchange_weak_explicit(&permit->lockWake, &expected, 1U, memory_order_relaxed, memory_order_relaxed)))
    {
      //if(1==cpus) thrd_yield();
    }
    // Have we been destroyed?
    if(!permit->magic)
    {
      permit->lockWake=0;
      atomic_fetch_add_explicit(&permit->granted, 1U, memory_order_relaxed);
      return thrd_error;
    }
  }
  // Grant permit
  atomic_store_explicit(&permit->permit, 1U, memory_order_seq_cst);
  if(permit->hooks[PTHREAD_PERMIT_HOOK_TYPE_GRANT])
    permit->hooks[PTHREAD_PERMIT_HOOK_TYPE_GRANT]->func(PTHREAD_PERMIT_HOOK_TYPE_GRANT, permit, permit->hooks[PTHREAD_PERMIT_HOOK_TYPE_GRANT]);
  // Are there waiters on the permit?
  if(atomic_load_explicit(&permit->waiters, memory_order_relaxed)!=atomic_load_explicit(&permit->waited, memory_order_relaxed))
  { // There are indeed waiters. If waiters don't consume permits, release everything
    if(permit->replacePermit)
    { // Loop waking until nothing is waiting
      do
      {
        if(thrd_success!=(ret=cnd_broadcast(&permit->cond)))
        {
          goto exit;
        }
        // Are there select operations on the permit?
        for(n=0; n<MAX_PTHREAD_PERMIT_SELECTS; n++)
        {
          if(permit->selects[n])
          {
            if(thrd_success!=(ret=cnd_signal(&permit->selects[n]->cond)))
            {
              goto exit;
            }
          }
        }
        //if(1==cpus) thrd_yield();
      } while(atomic_load_explicit(&permit->magic, memory_order_relaxed) && atomic_load_explicit(&permit->waiters, memory_order_relaxed)!=atomic_load_explicit(&permit->waited, memory_order_relaxed));
    }
    else
    { // Loop waking until at least one thread takes the permit
      while(atomic_load_explicit(&permit->magic, memory_order_relaxed) && atomic_load_explicit(&permit->permit, memory_order_relaxed) && atomic_load_explicit(&permit->waiters, memory_order_relaxed)!=atomic_load_explicit(&permit->waited, memory_order_relaxed))
      {
        if(thrd_success!=(ret=cnd_signal(&permit->cond)))
        {
          goto exit;
        }
        // Are there select operations on the permit?
        for(n=0; n<MAX_PTHREAD_PERMIT_SELECTS; n++)
        {
          if(permit->selects[n])
          {
            if(thrd_success!=(ret=cnd_signal(&permit->selects[n]->cond)))
            {
              goto exit;
            }
          }
        }
        //if(1==cpus) thrd_yield();
      }
    }
  }
exit:
  // If permits aren't consumed, granting has completed, so permit new waiters and granters
  if(permit->replacePermit)
    permit->lockWake=0;
  atomic_fetch_add_explicit(&permit->granted, 1U, memory_order_relaxed);
  return ret;
}

static void pthread_permit_revoke(pthread_permit_t *permit)
{
  atomic_store_explicit(&permit->permit, 0U, memory_order_relaxed);
  if(permit->hooks[PTHREAD_PERMIT_HOOK_TYPE_REVOKE])
    permit->hooks[PTHREAD_PERMIT_HOOK_TYPE_REVOKE]->func(PTHREAD_PERMIT_HOOK_TYPE_REVOKE, permit, permit->hooks[PTHREAD_PERMIT_HOOK_TYPE_REVOKE]);
}

static int pthread_permit_wait(pthread_permit_t *permit, pthread_mutex_t *mtx)
{
  int ret=thrd_success, unlocked=0;
  unsigned expected;
  // If permits aren't consumed, if a permit is executing then wait here
  // such that the grant can complete in a finite time
  if(permit->replacePermit)
  {
    while(atomic_load_explicit(&permit->lockWake, memory_order_acquire))
    {
      //if(1==cpus) thrd_yield();
    }
  }
  // Increment the monotonic count to indicate we have entered a wait
  atomic_fetch_add_explicit(&permit->waiters, 1U, memory_order_acquire);
  // Check again if we have been deleted
  if(!permit->magic)
  {
    atomic_fetch_add_explicit(&permit->waited, 1U, memory_order_relaxed);
    return thrd_error;    
  }
  // Fetch me a permit, excluding all other threads if replacePermit is zero
  while((expected=1, !atomic_compare_exchange_weak_explicit(&permit->permit, &expected, permit->replacePermit, memory_order_relaxed, memory_order_relaxed)))
  { // Permit is not granted, so wait if we have a mutex
    if(mtx)
    {
      int _ret;
      // If supplied with a mutex, we need to ensure it is unlocked during grants
      if(!unlocked)
      {
        if(thrd_success!=(_ret=mtx_lock(&permit->internal_mtx))) ret=_ret;
        if(thrd_success!=(_ret=mtx_unlock(mtx))) ret=_ret;
        unlocked=1;
      }
      if(thrd_success!=(_ret=cnd_wait(&permit->cond, &permit->internal_mtx))) ret=_ret;
    }
    else thrd_yield();
  }
  if(unlocked)
  {
    mtx_lock(mtx);
    mtx_unlock(&permit->internal_mtx);
  }
  // Increment the monotonic count to indicate we have exited a wait
  atomic_fetch_add_explicit(&permit->waited, 1U, memory_order_relaxed);
  return ret;
}

static int pthread_permit_timedwait(pthread_permit_t *permit, pthread_mutex_t *mtx, const struct timespec *ts)
{
  int ret=thrd_success, unlocked=0;
  unsigned expected;
  struct timespec now;
  // If permits aren't consumed, if a permit is executing then wait here
  if(permit->replacePermit)
  {
    while(atomic_load_explicit(&permit->lockWake, memory_order_acquire))
    {
      //if(1==cpus) thrd_yield();
    }
  }
  // Increment the monotonic count to indicate we have entered a wait
  atomic_fetch_add_explicit(&permit->waiters, 1U, memory_order_acquire);
  // Check again if we have been deleted
  if(!permit->magic)
  {
    atomic_fetch_add_explicit(&permit->waited, 1U, memory_order_relaxed);
    return thrd_error;    
  }
  // Fetch me a permit, excluding all other threads if replacePermit is zero
  while((expected=1, !atomic_compare_exchange_weak_explicit(&permit->permit, &expected, permit->replacePermit, memory_order_relaxed, memory_order_relaxed)))
  { // Permit is not granted, so wait if we have a mutex
    if(!ts) { ret=thrd_timeout; break; }
    else
    {
      long long diff;
      timespec_get(&now, TIME_UTC);
      diff=timespec_diff(ts, &now);
      if(diff<=0) { ret=thrd_timeout; break; }
    }
    if(mtx)
    {
      int _ret;
      // If supplied with a mutex, we need to ensure it is unlocked during grants
      if(!unlocked)
      {
        if(thrd_success!=(_ret=mtx_timedlock(&permit->internal_mtx, ts))) { ret=_ret; break; }
        _ret=mtx_unlock(mtx);
        unlocked=1;
      }
      _ret=cnd_timedwait(&permit->cond, &permit->internal_mtx, ts);
      if(thrd_success!=_ret && thrd_timeout!=_ret) { ret=_ret; break; }
    }
    else thrd_yield();
  }
  if(unlocked)
  {
    mtx_lock(mtx);
    mtx_unlock(&permit->internal_mtx);
  }
  // Increment the monotonic count to indicate we have exited a wait
  atomic_fetch_add_explicit(&permit->waited, 1U, memory_order_relaxed);
  return ret;
}

#ifndef VALGRIND_MAKE_MEM_DEFINED
#define VALGRIND_MAKE_MEM_DEFINED(a, l)
#endif

// Specialise the above with their extern type safe APIs
#define PERMIT_IMPL(permittype) \
PTHREAD_PERMIT_API_DEFINE(int, permittype##_init, (pthread_##permittype##_t *permit, _Bool initial)) \
{ \
  VALGRIND_MAKE_MEM_DEFINED(&permit->magic, sizeof(permit->magic)); \
  if(PERMIT_MAGIC==((pthread_permit_t *) permit)->magic) return thrd_busy; \
  return pthread_permit_init((pthread_permit_t *) permit, PERMIT_MAGIC, PERMIT_FLAGS, initial); \
} \
\
PTHREAD_PERMIT_API_DEFINE(int, permittype##_pushhook, (pthread_##permittype##_t *permit, pthread_permit_hook_type_t type, pthread_##permittype##_hook_t *hook)) \
{ \
  if(PERMIT_MAGIC!=((pthread_permit_t *) permit)->magic) return thrd_error; \
  return pthread_permit_pushhook((pthread_permit_t *) permit, type, (pthread_permit_hook_t *) hook); \
} \
\
PTHREAD_PERMIT_API_DEFINE(pthread_##permittype##_hook_t *, permittype##_pophook, (pthread_##permittype##_t *permit, pthread_permit_hook_type_t type)) \
{ \
  if(PERMIT_MAGIC!=((pthread_permit_t *) permit)->magic) return 0; \
  return (pthread_##permittype##_hook_t *) pthread_permit_pophook((pthread_permit_t *) permit, type); \
} \
\
PTHREAD_PERMIT_API_DEFINE(void , permittype##_destroy, (pthread_##permittype##_t *permit)) \
{ \
  if(PERMIT_MAGIC!=((pthread_permit_t *) permit)->magic) return; \
  pthread_permit_destroy((pthread_permit_t *) permit); \
} \
\
PTHREAD_PERMIT_API_DEFINE(int , permittype##_grant, (pthread_permitX_t permit)) \
{ \
  if(PERMIT_MAGIC!=((pthread_permit_t *) permit)->magic) return thrd_error; \
  return pthread_permit_grant(permit); \
} \
\
PTHREAD_PERMIT_API_DEFINE(void , permittype##_revoke, (pthread_##permittype##_t *permit)) \
{ \
  if(PERMIT_MAGIC!=((pthread_permit_t *) permit)->magic) return; \
  pthread_permit_revoke((pthread_permit_t *) permit); \
} \
\
PTHREAD_PERMIT_API_DEFINE(int , permittype##_wait, (pthread_##permittype##_t *permit, pthread_mutex_t *mtx)) \
{ \
  if(PERMIT_MAGIC!=((pthread_permit_t *) permit)->magic) return thrd_error; \
  return pthread_permit_wait((pthread_permit_t *) permit, mtx); \
} \
\
PTHREAD_PERMIT_API_DEFINE(int , permittype##_timedwait, (pthread_##permittype##_t *permit, pthread_mutex_t *mtx, const struct timespec *ts)) \
{ \
  if(PERMIT_MAGIC!=((pthread_permit_t *) permit)->magic) return thrd_error; \
  return pthread_permit_timedwait((pthread_permit_t *) permit, mtx, ts); \
}

#define PERMIT permitc
#define PERMIT_MAGIC PERMIT_CONSUMING_PERMIT_MAGIC
#define PERMIT_FLAGS 0
PERMIT_IMPL(permitc)
#undef PERMIT_FLAGS
#undef PERMIT_MAGIC

#define PERMIT_MAGIC PERMIT_NONCONSUMING_PERMIT_MAGIC
#define PERMIT_FLAGS PTHREAD_PERMIT_WAITERS_DONT_CONSUME
PERMIT_IMPL(permitnc)
#undef PERMIT_FLAGS
#undef PERMIT_MAGIC

#undef PERMIT_IMPL


static int pthread_permit_select_int(size_t no, pthread_permit_t **PTHREAD_PERMIT_RESTRICT permits, pthread_mutex_t *mtx, const struct timespec *ts)
{
  int ret=thrd_success;
  unsigned expected;
  struct timespec now;
  pthread_permit_select_t *myselect=0;
  size_t n, totalpermits=0, replacePermits=0, selectslot=(size_t)-1, selectedpermit=(size_t)-1;
  // Sanity check permits
  for(n=0; n<no; n++)
  {
    if(permits[n])
    {
      if(PERMIT_CONSUMING_PERMIT_MAGIC!=permits[n]->magic && PERMIT_NONCONSUMING_PERMIT_MAGIC!=permits[n]->magic)
      {
        permits[n]=0;
      }
      if(permits[n]->replacePermit) replacePermits++;
      totalpermits++;
    }
  }
  if(thrd_success!=ret || !totalpermits) return ret;
  // Find a free slot for us to use
  for(n=0; n<MAX_PTHREAD_PERMIT_SELECTS; n++)
  {
    expected=0;
    if(atomic_compare_exchange_weak_explicit(&pthread_permit_selects[n].magic, &expected, *(const unsigned *)"SPER", memory_order_relaxed, memory_order_relaxed))
    {
      selectslot=n;
      break;
    }
  }
  if(MAX_PTHREAD_PERMIT_SELECTS==n) return thrd_nomem;
  myselect=&pthread_permit_selects[selectslot];
  if(thrd_success!=(ret=cnd_init(&myselect->cond))) return ret;

  // Link each of the permits into our select slot
  for(n=0; n<no; n++)
  {
    if(permits[n])
    {
      // If the permit isn't consumed, if the permit is executing then wait here
      if(permits[n]->replacePermit)
      {
        while(atomic_load_explicit(&permits[n]->lockWake, memory_order_acquire))
        {
          //if(1==cpus) thrd_yield();
        }
        replacePermits--;
      }
      // Increment the monotonic count to indicate we have entered a wait
      atomic_fetch_add_explicit(&permits[n]->waiters, 1U, memory_order_acquire);
      // Set the select
      assert(!permits[n]->selects[selectslot]);
      permits[n]->selects[selectslot]=myselect;
    }
  }
  assert(!replacePermits);

  // Loop the permits, trying to grab a permit
  for(;;)
  {
    for(n=0; n<no; n++)
    {
      if(permits[n])
      {
        expected=1;
        if(atomic_compare_exchange_weak_explicit(&permits[n]->permit, &expected, permits[n]->replacePermit, memory_order_relaxed, memory_order_relaxed))
        { // Permit is granted
          selectedpermit=n;
          break;
        }
      }
    }
    if((size_t)-1!=selectedpermit) break;
    // Permit is not granted, so wait if we have a mutex
    if(ts)
    {
      long long diff;
      timespec_get(&now, TIME_UTC);
      diff=timespec_diff(ts, &now);
      if(diff<=0) { ret=thrd_timeout; break; }
    }
    if(mtx)
    {
      int cndret=(ts ? cnd_timedwait(&myselect->cond, mtx, ts) : cnd_wait(&myselect->cond, mtx));
      if(thrd_success!=cndret && thrd_timeout!=cndret) { ret=cndret; break; }
    }
    else thrd_yield();
  }

  // Delink each of the permits from our select slot
  for(n=0; n<no; n++)
  {
    if(permits[n])
    {
      // Unset the select
      assert(permits[n]->selects[selectslot]==myselect);
      permits[n]->selects[selectslot]=0;
      // Increment the monotonic count to indicate we have exited a wait
      atomic_fetch_add_explicit(&permits[n]->waited, 1U, memory_order_relaxed);
      // Zero if not selected
      if(selectedpermit!=n) permits[n]=0;
    }
  }
  // Destroy the select slot's condition variable and reset
  cnd_destroy(&myselect->cond);
  myselect->magic=0;
  return ret;
}
PTHREAD_PERMIT_API_DEFINE(int , permit_select, (size_t no, pthread_permitX_t *permits, pthread_mutex_t *mtx, const struct timespec *ts))
{
  return pthread_permit_select_int(no, (pthread_permit_t **PTHREAD_PERMIT_RESTRICT) permits, mtx, ts);
}

typedef struct pthread_permitnc_association_s
{
  struct pthread_permitnc_hook_s grant, revoke;
} *pthread_permitnc_association_t;
static int pthread_permitnc_associate_fd_hook_grant(pthread_permit_hook_type_t type, pthread_permitnc_t *permit, pthread_permitnc_hook_t *hookdata)
{
  int fd=(int)(size_t)(hookdata->data);
  char buffer=0;
  struct pollfd pfd;
  pfd.fd=fd;
  pfd.events=POLLOUT;
  pfd.revents=0;
  poll(&pfd, 1, 0);
  if(pfd.revents&POLLOUT)
    while(-1==write(fd, &buffer, 1) && EINTR==errno);
  return hookdata->next ? hookdata->next->func(type, permit, hookdata->next) : 0;
}
static int pthread_permitnc_associate_fd_hook_revoke(pthread_permit_hook_type_t type, pthread_permitnc_t *permit, pthread_permitnc_hook_t *hookdata)
{
  int fd=(int)(size_t)(hookdata->data);
  char buffer[256];
  struct pollfd pfd;
  pfd.fd=fd;
  pfd.events=POLLIN;
  pfd.revents=0;
  do
  {
    pfd.revents=0;
    poll(&pfd, 1, 0);
    if(pfd.revents&POLLIN)
      while(-1==read(fd, buffer, 256) && EINTR==errno);
  } while(pfd.revents&POLLIN);
  return hookdata->next ? hookdata->next->func(type, permit, hookdata->next) : 0;
}
static pthread_permitnc_association_t pthread_permit_associate_fd(pthread_permit_t *permit, int fds[2])
{
  pthread_permitnc_association_t ret;
  if(PERMIT_NONCONSUMING_PERMIT_MAGIC!=permit->magic || !permit->replacePermit) return 0;
  ret=(pthread_permitnc_association_t) calloc(1, sizeof(struct pthread_permitnc_association_s));
  if(!ret) return ret;
  ret->grant.func=pthread_permitnc_associate_fd_hook_grant;
  ret->revoke.func=pthread_permitnc_associate_fd_hook_revoke;
  ret->revoke.data=(void *)(size_t) fds[0]; // Revoke reads until empty
  ret->grant.data=(void *)(size_t) fds[1];  // Grant writes a single byte
  if(thrd_success!=pthread_permit_pushhook(permit, PTHREAD_PERMIT_HOOK_TYPE_GRANT, (pthread_permit_hook_t *) &ret->grant))
  {
    free(ret);
    return 0;
  }
  if(thrd_success!=pthread_permit_pushhook(permit, PTHREAD_PERMIT_HOOK_TYPE_REVOKE, (pthread_permit_hook_t *) &ret->revoke))
  {
    pthread_permit_pophook(permit, PTHREAD_PERMIT_HOOK_TYPE_GRANT);
    free(ret);
    return 0;
  }
  if(permit->permit)
  {
    char buffer=0;
    for(;;) if(-1!=write(fds[1], &buffer, 1)) break; else if(EINTR!=errno) return 0;
  }
  return ret;
}
PTHREAD_PERMIT_API_DEFINE(pthread_permitnc_association_t , permitnc_associate_fd, (pthread_permitnc_t *permit, int fds[2]))
{
  return pthread_permit_associate_fd((pthread_permit_t *) permit, fds);
}

static void pthread_permit_deassociate(pthread_permit_t *permit, pthread_permitnc_association_t assoc)
{
  pthread_permitnc_hook_t *PTHREAD_PERMIT_RESTRICT *hookptr;
  if(PERMIT_NONCONSUMING_PERMIT_MAGIC!=permit->magic || !permit->replacePermit) return;
  for(hookptr=(pthread_permitnc_hook_t *PTHREAD_PERMIT_RESTRICT *) &permit->hooks[PTHREAD_PERMIT_HOOK_TYPE_GRANT]; *hookptr; hookptr=&(*hookptr)->next)
  {
    if(*hookptr==&assoc->grant)
    {
      *hookptr=(*hookptr)->next;
      break;
    }
  }
  for(hookptr=(pthread_permitnc_hook_t *PTHREAD_PERMIT_RESTRICT *) &permit->hooks[PTHREAD_PERMIT_HOOK_TYPE_REVOKE]; *hookptr; hookptr=&(*hookptr)->next)
  {
    if(*hookptr==&assoc->revoke)
    {
      *hookptr=(*hookptr)->next;
      break;
    }
  }
  free(assoc);
}
PTHREAD_PERMIT_API_DEFINE(void , permitnc_deassociate, (pthread_permitnc_t *permit, pthread_permitnc_association_t assoc))
{
  pthread_permit_deassociate((pthread_permit_t *) permit, assoc);
}

#ifdef _WIN32
static int pthread_permit_associate_winhandle_hook_grant(pthread_permit_hook_type_t type, pthread_permitnc_t *permit, pthread_permitnc_hook_t *hookdata)
{
  HANDLE h=hookdata->data;
  char buffer=0;
  DWORD written=0;
  if(WAIT_TIMEOUT==WaitForSingleObject(h, 0))
    WriteFile(h, &buffer, 1, &written, NULL);
  return hookdata->next ? hookdata->next->func(type, permit, hookdata->next) : 0;
}
static int pthread_permit_associate_winhandle_hook_revoke(pthread_permit_hook_type_t type, pthread_permitnc_t *permit, pthread_permitnc_hook_t *hookdata)
{
  HANDLE h=hookdata->data;
  char buffer[256];
  DWORD read=0;
  while(WAIT_OBJECT_0==WaitForSingleObject(h, 0))
    ReadFile(h, buffer, 256, &read, NULL);
  return hookdata->next ? hookdata->next->func(type, permit, hookdata->next) : 0;
}
static pthread_permitnc_association_t pthread_permit_associate_winhandle_np(pthread_permit_t *permit, HANDLE h)
{
  pthread_permitnc_association_t ret;
  if(PERMIT_NONCONSUMING_PERMIT_MAGIC!=permit->magic || !permit->replacePermit) return 0;
  ret=(pthread_permitnc_association_t) calloc(1, sizeof(struct pthread_permitnc_association_s));
  if(!ret) return ret;
  ret->grant.func=pthread_permit_associate_winhandle_hook_grant;
  ret->revoke.func=pthread_permit_associate_winhandle_hook_revoke;
  ret->grant.data=ret->revoke.data=h;
  if(thrd_success!=pthread_permit_pushhook( permit, PTHREAD_PERMIT_HOOK_TYPE_GRANT, (pthread_permit_hook_t *) &ret->grant))
  {
    free(ret);
    return 0;
  }
  if(thrd_success!=pthread_permit_pushhook(permit, PTHREAD_PERMIT_HOOK_TYPE_REVOKE, (pthread_permit_hook_t *) &ret->revoke))
  {
    pthread_permit_pophook(permit, PTHREAD_PERMIT_HOOK_TYPE_GRANT);
    free(ret);
    return 0;
  }
  if(permit->permit)
  {
    char buffer=0;
    DWORD written=0;
    if(!WriteFile(h, &buffer, 1, &written, NULL) && ERROR_IO_PENDING!=GetLastError())
      return 0;
  }
  return ret;
}
PTHREAD_PERMIT_API_DEFINENP(pthread_permitnc_association_t , permitnc_associate_winhandle, (pthread_permitnc_t *permit, HANDLE h))
{
  return pthread_permit_associate_winhandle_np((pthread_permit_t *) permit, h);
}

static int pthread_permit_associate_winevent_hook_grant(pthread_permit_hook_type_t type, pthread_permitnc_t *permit, pthread_permitnc_hook_t *hookdata)
{
  HANDLE h=hookdata->data;
  char buffer=0;
  SetEvent(h);
  return hookdata->next ? hookdata->next->func(type, permit, hookdata->next) : 0;
}
static int pthread_permit_associate_winevent_hook_revoke(pthread_permit_hook_type_t type, pthread_permitnc_t *permit, pthread_permitnc_hook_t *hookdata)
{
  HANDLE h=hookdata->data;
  ResetEvent(h);
  return hookdata->next ? hookdata->next->func(type, permit, hookdata->next) : 0;
}
static pthread_permitnc_association_t pthread_permit_associate_winevent_np(pthread_permit_t *permit, HANDLE h)
{
  pthread_permitnc_association_t ret;
  if(PERMIT_NONCONSUMING_PERMIT_MAGIC!=permit->magic || !permit->replacePermit) return 0;
  ret=(pthread_permitnc_association_t) calloc(1, sizeof(struct pthread_permitnc_association_s));
  if(!ret) return ret;
  ret->grant.func=pthread_permit_associate_winevent_hook_grant;
  ret->revoke.func=pthread_permit_associate_winevent_hook_revoke;
  ret->grant.data=ret->revoke.data=h;
  if(thrd_success!=pthread_permit_pushhook(permit, PTHREAD_PERMIT_HOOK_TYPE_GRANT, (pthread_permit_hook_t *) &ret->grant))
  {
    free(ret);
    return 0;
  }
  if(thrd_success!=pthread_permit_pushhook(permit, PTHREAD_PERMIT_HOOK_TYPE_REVOKE, (pthread_permit_hook_t *) &ret->revoke))
  {
    pthread_permit_pophook(permit, PTHREAD_PERMIT_HOOK_TYPE_GRANT);
    free(ret);
    return 0;
  }
  if(permit->permit)
  {
    if(!SetEvent(h)) return 0;
  }
  return ret;
}
PTHREAD_PERMIT_API_DEFINENP(pthread_permitnc_association_t , permitnc_associate_winevent, (pthread_permitnc_t *permit, HANDLE h))
{
  return pthread_permit_associate_winhandle_np((pthread_permit_t *) permit, h);
}
#endif

#ifdef __cplusplus
PTHREAD_PERMIT_CXX_NAMESPACE_END
#endif

