/* pthread_permit.h
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

#ifndef PTHREAD_PERMIT_H
#define PTHREAD_PERMIT_H

/*! \file
\brief Defines and declares the API for POSIX threads permit objects
*/

//! Adjust this to set what C++ namespace this implementation is defined into if being compiled as C++. Defaults to extern "C"
#ifndef PTHREAD_PERMIT_CXX_NAMESPACE
#define PTHREAD_PERMIT_CXX_NAMESPACE extern "C"
#endif

#ifndef DOXYGEN_PREPROCESSOR
#include "c11_compat.h"
#include <assert.h>
#endif // DOXYGEN_PREPROCESSOR

#if !defined(PTHREAD_PERMIT_APIEXPORT) && defined(_USRDLL)
#ifdef _WIN32
#define PTHREAD_PERMIT_APIEXPORT extern __declspec(dllexport)
#elif defined(__GNUC__) || defined (__clang__)
#define PTHREAD_PERMIT_APIEXPORT extern __attribute__ ((visibility("default")))
#endif
#endif
//! Adjust this to set how extern API is exported. NOTE: defining _USRDLL does the right thing on GCC, Clang and MSVC.
#ifndef PTHREAD_PERMIT_APIEXPORT
#define PTHREAD_PERMIT_APIEXPORT extern
#endif
//! Adjust this to change extern API prefix and postfix. By default set to "pthread_" and "_np" respectively.
#ifndef PTHREAD_PERMIT_MANGLEAPI
#ifndef DOXYGEN_PREPROCESSOR
#define PTHREAD_PERMIT_MANGLEAPI(api) pthread_##api##_np
#else
#define PTHREAD_PERMIT_MANGLEAPI(api) pthread_##api
#endif
#endif
//! Adjust this to change non-portable extern API prefix (e.g. Windows-only APIs). By default set to "pthread_".
#ifndef PTHREAD_PERMIT_MANGLEAPINP
#define PTHREAD_PERMIT_MANGLEAPINP(api) pthread_##api##_np
#endif

#ifndef PTHREAD_PERMIT_API
#define PTHREAD_PERMIT_API(ret, api, params) PTHREAD_PERMIT_APIEXPORT ret PTHREAD_PERMIT_MANGLEAPI(api) params
#endif
#ifndef PTHREAD_PERMIT_API_DEFINE
#define PTHREAD_PERMIT_API_DEFINE(ret, api, params) ret PTHREAD_PERMIT_MANGLEAPI(api) params
#endif
#ifndef PTHREAD_PERMIT_APINP
#define PTHREAD_PERMIT_APINP(ret, api, params) PTHREAD_PERMIT_APIEXPORT ret PTHREAD_PERMIT_MANGLEAPINP(api) params
#endif
#ifndef PTHREAD_PERMIT_API_DEFINENP
#define PTHREAD_PERMIT_API_DEFINENP(ret, api, params) ret PTHREAD_PERMIT_MANGLEAPINP(api) params
#endif

/*! \mainpage The POSIX threads permit objects

(C) 2011-2014 Niall Douglas http://www.nedproductions.biz/

Herein lies a suite of safe, composable, user mode permit objects for POSIX threads. They are used
to asynchronously give a thread of code the \em permission to proceed and as such are
very useful in any situation where one bit of code must asynchronously notify another
bit of code of something.

Unlike other forms of synchronisation, permit objects do not suffer from race conditions.
Unlike naive use of condition variables, permit objects do not suffer from lost or spurious
wakeups - there is \b always a one-to-one relationship between the granting of a permit and
the receiving of that permit. Unlike naive use of semaphores, permit objects do not suffer
from producer-consumer problems. Whilst having similarities to event objects, unlike event
objects permit objects always maintain the concept of \em atomic \em ownership of the permit -
exactly \em one actor may own a permit at any one time. This strong guarantee allows
permits to be much safer and more predictable in many-granter many-waiter scenarios than event
objects. Finally, unlike most, permit objects provide strong guarantees during destruction:
you can destroy a permit object being waited upon in another thread safely.

There are three permit objects:
1. A simple implementation, pthread_permit1_t. This is the simplest and fastest implementation of
a POSIX threads permit. It is typically compiled inline, and it is always consuming, non-hookable
and non-selectable.
2. A pthread_permitc_t denotes a consuming POSIX threads permit. It is hookable and selectable.
Selectable means that pthread_permitX_select() may be called upon an array of permits which
returns after any one of the supplied permits receives a grant.
3. A pthread_permitnc_t denotes a non-consuming POSIX threads permit. It is also hookable and
selectable. Non-consuming permits can also optionally mirror their state onto a kernel file
descriptor, allowing the use of select() and poll().

\section features Features
POSIX threads permit objects are guaranteed to not use dynamic memory except in those
functions which explicitly say so. They can also optionally never sleep the calling thread,
instead spinning until the permit is gained. This permits their use in bootstrap or small
embedded systems, or where low latency is paramount.

This reference implementation is written in C11 and the latest version can be found at
https://github.com/ned14/ISO_POSIX_standards_stuff/tree/master/pthreads%20Notifer%20Object.
It contains a full set of unit tests written using C++ CATCH. The software licence is Boost's
software licence (free to use by anyone).

It contains support for Microsoft Windows 7 and POSIX. It has been tested on Microsoft Visual
Studio 2010, GCC v4.6 and clang v3.2.

The simple permit object costs 48/0/142 CPU cycles for grant/revoke/wait uncontended and 359/4/372
cycles when contended between two threads. These results are for an Intel Core 2 processor.

\section whynecessary Why is it necessary that a permit object be added to POSIX threads?
There are many occasions in threaded programming when a third party library goes off and does
something asynchronous in the background. In the meantime, the foreground thread may do other tasks,
occasionally polling a notification object to see if the background job has completed, or indeed if
it runs out of foreground things to do, it may simply sleep until the completion of the background
job or jobs. Put simply, the foreground threads polls or waits for permission to continue.

The problem is that naive programmers think a wait condition is suitable for this purpose. This is
highly incorrect due to the problem of spurious and lost wakeups inherent to wait conditions. Despite
the documentation for pthread_cond saying this, wait conditions are frequently proposed as the "correct"
solution in many "expert advice" internet sites including stackflow.com among others. The present lack of
standardised, safe asynchronous notification objects in POSIX leads to too many "roll your own"
implementations which are too frequently subtly broken. This leads to unreliability in threaded
programming. Furthermore, there is a problem of interoperability - how can third party libraries
interoperate easily when each rolls its own asynchronous notification object.

A completely safe notification object can be built from atomics and wait conditions - indeed, what
is proposed is entirely built this way. The problem is rather one of standardisation on a safe,
efficient, and well tested implementation.

\section Acknowledgements
The POSIX threads permit objects proposed by this document came from internal deliberations by WG14
during the preparation of the C11 standard - I am highly indebted to those on the committee who gave
so freely of their time and thoughts. My thanks in particular are due to Hans Boehm without whose
detailed feedback this proposal would look completely different. I would also like to thank Anthony
Williams for his commentary and feedback, and Nick Stoughton for his advice to me regarding becoming the
ISO JTC1 SC22 convenor for the Republic of Ireland and on how best to submit a proposal for incorporation
into the POSIX standard. My thanks are also due to John Benito, WG14 convenor, for his seemingly never
tiring efforts on the behalf of C-ish programmers everywhere.
*/

#ifdef __cplusplus
PTHREAD_PERMIT_CXX_NAMESPACE {
#endif

/*! \defgroup pthread_permitX_t Permit types
\brief The types of pthread_permit

A pthread_permit1_t denotes the simplest and fastest implementation of a POSIX threads permit. It
is typically compiled inline, and it is always consuming, non-hookable and non-selectable.

A pthread_permitc_t denotes a consuming POSIX threads permit, while a pthread_permitnc_t denotes
a non-consuming POSIX threads permit. Both are hookable and selectable. Non-consuming permits are additionally
optionally kernel file descriptor mirroring.

pthread_permitX_t denotes any of pthread_permit1_t, pthread_permitc_t and pthread_permitnc_t. It
is specifically for pthread_permit1_grant(pthread_permitX_t permit), 
pthread_permitc_grant(pthread_permitX_t permit) and pthread_permitnc_grant(pthread_permitX_t permit)
all of which match the permit grant function prototype type definition pthread_permitX_grant_func.
The API of the permit grant has been so unified to allow the following idiom:

\code
int ask_3rd_party_library_to_do_an_asynchronous_job(..., pthread_permitX_t permit, pthread_permitX_grant_func completionroutine);
...
pthread_permitnc_t permit;
pthread_permitnc_init(&permit);
ask_3rd_party_library_to_do_an_asynchronous_job(..., &permit, pthread_permitnc_grant);
...
// Wait for completion
pthread_permitnc_wait(&permit);
\endcode

In other words, client code can supply whichever type of permit it prefers to third party library
code. That third party library simply calls the supplied completion routine, thus granting the permit.

@{
*/
//! The simplest and fastest implementation of a POSIX threads permit. Always consuming, non-hookable and non-selectable
typedef struct pthread_permit1_s pthread_permit1_t;
//! A consuming POSIX threads permit. Hookable and selectable
typedef struct pthread_permitc_s pthread_permitc_t;
//! A non-consuming POSIX threads permit. Hookable and selectable
typedef struct pthread_permitnc_s pthread_permitnc_t;
//! A pointer to any of pthread_permit1_t, pthread_permitc_t and pthread_permitnc_t
typedef void *pthread_permitX_t;
//! A permit grant function prototype for any of the permit grant functions
typedef int (*pthread_permitX_grant_func)(pthread_permitX_t);
//! @}

/*! \defgroup pthread_permit_hook_t Permit hooks
\brief The hooks of pthread_permitc_t and pthread_permitnc_t

Consuming and non-consuming POSIX threads permit objects can have their operations upcalled to
interested parties. This is used to provide useful extensions such as kernel file descriptor
state mirroring. The following hooks are available:

- PTHREAD_PERMIT_HOOK_TYPE_DESTROY: Called just before a permit is destroyed.
- PTHREAD_PERMIT_HOOK_TYPE_GRANT: Called just after a permit is granted, but before waiters are woken.
- PTHREAD_PERMIT_HOOK_TYPE_REVOKE: Called just after a permit is revoked.

pthread_permit_hook_t_np is a structure defined as follows:
\code
typedef struct pthread_permit_hook_s
{
  int (*func)(pthread_permitX_t permit, pthread_permit_hook_t_np *hookdata);
  void *data;
  pthread_permit_hook_t_np *next;
} pthread_permit_hook_t_np;
\endcode

You almost certainly want to allocate this structure statically as no copy is taken. \em func
should have the following form:
\code
int pthread_permit_hook_grant(pthread_permit_hook_type_t type, pthread_permitX_t permit, pthread_permit_hook_t_np *hookdata)
{
  // Use hookdata->data as you see fit
  ...
  // Call previous hooks
  return hookdata->next ? hookdata->next->func(type, permit, hookdata->next) : 0;
}
\endcode

To add a hook, pthread_permitc_pushhook() and pthread_permitnc_pushhook() pushes a hook to the top of the call stack (i.e.
is called first) by setting its \em next member to the previous top hook. pthread_permitc_pophook() and
pthread_permitnc_pophook() delink the top hook and return it.

@{
*/
//! The hook data structure type
typedef struct pthread_permitc_hook_s pthread_permitc_hook_t;
//! The hook data structure type
typedef struct pthread_permitnc_hook_s pthread_permitnc_hook_t;
//! The type of hook
typedef enum pthread_permit_hook_type
{
  PTHREAD_PERMIT_HOOK_TYPE_DESTROY,
  PTHREAD_PERMIT_HOOK_TYPE_GRANT,
  PTHREAD_PERMIT_HOOK_TYPE_REVOKE,
  PTHREAD_PERMIT_HOOK_TYPE_WAIT,    // Not currently used

  PTHREAD_PERMIT_HOOK_TYPE_LAST
} pthread_permit_hook_type_t;
//! The hook data structure
typedef struct pthread_permitc_hook_s
{
  int (*func)(pthread_permit_hook_type_t type, pthread_permitc_t *permit, pthread_permitc_hook_t *hookdata);
  void *data;
  pthread_permitc_hook_t *next;
} pthread_permitc_hook_t;
//! The hook data structure
typedef struct pthread_permitnc_hook_s
{
  int (*func)(pthread_permit_hook_type_t type, pthread_permitnc_t *permit, pthread_permitnc_hook_t *hookdata);
  void *data;
  pthread_permitnc_hook_t *next;
} pthread_permitnc_hook_t;
//! Pushes a hook
PTHREAD_PERMIT_API(int , permitc_pushhook, (pthread_permitc_t *permit, pthread_permit_hook_type_t type, pthread_permitc_hook_t *hook));
//! Pushes a hook
PTHREAD_PERMIT_API(int , permitnc_pushhook, (pthread_permitnc_t *permit, pthread_permit_hook_type_t type, pthread_permitnc_hook_t *hook));
//! Pops a hook
PTHREAD_PERMIT_API(pthread_permitc_hook_t *, permitc_pophook, (pthread_permitc_t *permit, pthread_permit_hook_type_t type));
//! Pops a hook
PTHREAD_PERMIT_API(pthread_permitnc_hook_t *, permitnc_pophook, (pthread_permitnc_t *permit, pthread_permit_hook_type_t type));
//! @}


/*! \defgroup pthread_permitX_init Permit initialisation
\brief Initialises a permit
\returns 0: success;
EBUSY: The implementation has detected an attempt to re-initialise the object referenced by permit, a previously initialised, but not yet destroyed, permit;
EAGAIN: The system lacked the necessary resources (other than memory) to initialise another condition variable;
ENOMEM: Insufficient memory exists to initialise the condition variable.
@{
*/
//! Initialises a pthread_permit1_t
inline int pthread_permit1_init(pthread_permit1_t *permit, _Bool initial);
//! Initialises a pthread_permitc_t
PTHREAD_PERMIT_API(int , permitc_init, (pthread_permitc_t *permit, _Bool initial));
//! Initialises a pthread_permitnc_t
PTHREAD_PERMIT_API(int , permitnc_init, (pthread_permitnc_t *permit, _Bool initial));
//! @}

/*! \defgroup pthread_permitX_destroy Permit destruction
\brief Destroys a permit

It is safe to combine pthread_permitX_destroy in one thread with the following operations on the same
object in other threads:

* pthread_permitX_wait. Waits will immediately exit, possibly returning EINVAL.
* For consuming permits:
  * The single pthread_permitX_grant which woke the thread now performing the destroy. NO OTHER GRANTS ARE SAFE,
if you have multiple threads granting permits then you must synchronise them with permit destruction.
* For non-consuming permits:
  * pthread_permitnc_grant, all of which will immediately exit with EINVAL.
@{
*/
//! Destroys a pthread_permit1_t
inline void pthread_permit1_destroy(pthread_permit1_t *permit);
//! Destroys a pthread_permitc_t
PTHREAD_PERMIT_API(void , permitc_destroy, (pthread_permitc_t *permit));
//! Destroys a pthread_permitnc_t
PTHREAD_PERMIT_API(void , permitnc_destroy, (pthread_permitnc_t *permit));
//! @}

/*! \defgroup pthread_permitX_grant Permit granting
\brief Grants a permit.
\returns 0: success; EINVAL: bad/incorrect permit.

Grants permit to one waiting thread. If there is no waiting thread, gives permit to the next thread to wait.

If the permit is consuming (pthread_permit1_t and pthread_permitc_t), the permit is atomically
transferred to the winning thread.

If the permit is non-consuming (pthread_permitnc_t), the permit is still atomically transferred to
the winning thread, but the permit is atomically regranted. You are furthermore guaranteed that exactly
every waiter at the time of grant will be released before the grant operation returns. Note that
because of this guarantee, only one grant may occur at any one time per permit instance i.e. grant is a
critical section. This implies that if a grant is operating, any new waits hold until the grant
completes.

The parameter of the grant functions is a pthread_permitX_t which denotes any of pthread_permit1_t,
pthread_permitc_t and pthread_permitnc_t.
The API of the permit grant has been so unified to allow the following idiom:

\code
int ask_3rd_party_library_to_do_an_asynchronous_job(..., pthread_permitX_t permit, pthread_permitX_grant_func completionroutine);
...
pthread_permitnc_t permit;
pthread_permitnc_init(&permit);
ask_3rd_party_library_to_do_an_asynchronous_job(..., &permit, pthread_permitnc_grant);
...
// Wait for completion
pthread_permitnc_wait(&permit);
\endcode

In other words, client code can supply whichever type of permit it prefers to third party library
code. That third party library simply calls the supplied completion routine, thus granting the permit.

@{
*/
//! Grants a pthread_permit1_t
inline int pthread_permit1_grant(pthread_permitX_t permit);
//! Grants a pthread_permitc_t
PTHREAD_PERMIT_API(int , permitc_grant, (pthread_permitX_t permit));
//! Grants a pthread_permitnc_t
PTHREAD_PERMIT_API(int , permitnc_grant, (pthread_permitX_t permit));
//! @}

/*! \defgroup pthread_permitX_revoke Permit revoking
\brief Revokes a permit.

Revoke any outstanding permit, causing any subsequent waiters to wait.
@{
*/
//! Revokes a pthread_permit1_t
inline void pthread_permit1_revoke(pthread_permit1_t *permit);
//! Revokes a pthread_permitc_t
PTHREAD_PERMIT_API(void , permitc_revoke, (pthread_permitc_t *permit));
//! Revokes a pthread_permitnc_t
PTHREAD_PERMIT_API(void , permitnc_revoke, (pthread_permitnc_t *permit));
//! @}

/*! \defgroup pthread_permitX_wait Permit waiting
\brief Waits on a permit.
\returns 0: success; EINVAL: bad permit, mutex or timespec; ETIMEDOUT: the time period specified by ts expired.

Waits for permit to become available, atomically unlocking the specified mutex when waiting.
If mtx is NULL, never sleeps instead looping forever waiting for permit. If ts is NULL,
returns immediately instead of waiting.

If the permit is non-consuming, note that grants are serialised with one another and no new waits may begin
while a grant is occurring. This guarantees that non-consuming grants always release all waiters
waiting at the point of grant.
@{
*/
//! Waits on a pthread_permit1_t
inline int pthread_permit1_wait(pthread_permit1_t *permit, pthread_mutex_t *mtx);
//! Waits on a pthread_permitc_t
PTHREAD_PERMIT_API(int , permitc_wait, (pthread_permitc_t *permit, pthread_mutex_t *mtx));
//! Waits on a pthread_permitnc_t
PTHREAD_PERMIT_API(int , permitnc_wait, (pthread_permitnc_t *permit, pthread_mutex_t *mtx));
//! Waits on a pthread_permit1_t for a time
inline int pthread_permit1_timedwait(pthread_permit1_t *permit, pthread_mutex_t *mtx, const struct timespec *ts);
//! Waits on a pthread_permitc_t for a time
PTHREAD_PERMIT_API(int , permitc_timedwait, (pthread_permitc_t *permit, pthread_mutex_t *mtx, const struct timespec *ts));
//! Waits on a pthread_permitnc_t for a time
PTHREAD_PERMIT_API(int , permitnc_timedwait, (pthread_permitnc_t *permit, pthread_mutex_t *mtx, const struct timespec *ts));

/*! \brief Waits on many permits.
\returns 0: success; EINVAL: bad permit, mutex or timespec; ETIMEDOUT: the time period specified by ts expired.

Waits for a time for any permit in the supplied list of permits to become available, 
atomically unlocking the specified mutex when waiting. If mtx is NULL, never sleeps instead
looping forever waiting for a permit. If ts is NULL, waits \b forever (rather than return instantly).

On exit, if no error the permits array has all ungranted permits zeroed. Only the first granted permit is ever returned,
so all other elements will be zero.

On exit, if error then only errored permits are zeroed. In this case many elements can be returned.

Note that the permit array you supply may contain null pointers - if so, these entries are ignored. This
allows a convenient "rinse and repeat" idiom.

The complexity of this call is O(no). If we could use dynamic memory, or had OS support, we could achieve O(1).
*/
PTHREAD_PERMIT_API(int , permit_select, (size_t no, pthread_permitX_t *permits, pthread_mutex_t *mtx, const struct timespec *ts));
//! @}

/*! \defgroup pthread_permitnc_associate Permit kernel object association
\brief Associates a non-consuming permit with a kernel object's state

Sets a file descriptor whose signalled state should match the permit's state i.e. the descriptor
has a single byte written to it to make it signalled when the permit is granted. When the permit
is revoked, the descriptor is repeatedly read from until it is non-signalled. Note that this
mechanism uses the non-portable hook system but it does upcall previously installed hooks. Note
that these functions use malloc().

Note that these calls are not thread safe, so do \b not call them upon a permit being used (i.e. these
are suitable for initialisation and destruction stages only). Note you must call pthread_permit_deassociate() on the
returned value before destroying its associated permit else there will be a memory leak.

For pthread_permit_associate_fd(), the int fds[2] are there to mirror the output from the pipe()
function as that is the most likely usage. fds[0] is repeatedly read from until empty by revocation,
while fds[1] has a byte written to it by granting. One can set both members to the same file descriptor
if desired.

On Windows only, pthread_permit_associate_winhandle_np() is the Windows equivalent of pthread_permit_associate_fd().
For convenience there is also a pthread_permit_associate_winevent_np() which is probably much more useful
on Windows.
@{
*/
//! The type of a permit association handle
typedef struct pthread_permitnc_association_s *pthread_permitnc_association_t;
//! Associates the state of a kernel file descriptor with the state of a pthread_permitnc_t
PTHREAD_PERMIT_API(pthread_permitnc_association_t , permitnc_associate_fd, (pthread_permitnc_t *permit, int fds[2]));
//! Deassociates the state of a kernel file descriptor with the state of a pthread_permitnc_t
PTHREAD_PERMIT_API(void , permitnc_deassociate, (pthread_permitnc_t *permit, pthread_permitnc_association_t assoc));

#if defined(_WIN32) || defined(DOXYGEN_PREPROCESSOR)
//! Associates the state of a Windows kernel file handle with the state of a pthread_permitnc_t
PTHREAD_PERMIT_APINP(pthread_permitnc_association_t , permitnc_associate_winhandle, (pthread_permitnc_t *permit, HANDLE h));
//! Associates the state of a Windows kernel event handle with the state of a pthread_permitnc_t
PTHREAD_PERMIT_APINP(pthread_permitnc_association_t , permitnc_associate_winevent, (pthread_permitnc_t *permit, HANDLE h));
#endif
//! @}



#ifndef DOXYGEN_PREPROCESSOR

typedef struct pthread_permit1_s
{
  atomic_uint magic;                  /* Used to ensure this structure is valid */
  atomic_uint permit;                 /* =0 no permit, =1 yes permit */
  atomic_uint waiters, waited;        /* Keeps track of when a thread waits and wakes */
  cnd_t cond;                         /* Wakes anything waiting for a permit */
} pthread_permit1_t;


int pthread_permit1_init(pthread_permit1_t *permit, _Bool initial)
{
  int ret;
  if(*(const unsigned *)"1PER"==permit->magic) return thrd_busy;
  permit->permit=initial;
  permit->waiters=0;
  permit->waited=0;
  if(thrd_success!=(ret=cnd_init(&permit->cond))) return ret;
  atomic_store_explicit(&permit->magic, *(const unsigned *)"1PER", memory_order_seq_cst);
  return thrd_success;
}

void pthread_permit1_destroy(pthread_permit1_t *permit)
{
  if(*(const unsigned *)"1PER"!=permit->magic) return;
  /* Mark this object as invalid for further use */
  atomic_store_explicit(&permit->magic, 0U, memory_order_seq_cst);
  // Is anything waiting? If so repeatedly grant permit and wake until none
  while(permit->waiters!=permit->waited)
  {
    atomic_store_explicit(&permit->permit, 1U, memory_order_seq_cst);
    cnd_signal(&permit->cond);
  }
  cnd_destroy(&permit->cond);
}

int pthread_permit1_grant(pthread_permitX_t _permit)
{
  pthread_permit1_t *permit=(pthread_permit1_t *) _permit;
  int ret=thrd_success;
  if(*(const unsigned *)"1PER"!=permit->magic) return thrd_error;
  // Grant permit
  atomic_store_explicit(&permit->permit, 1U, memory_order_seq_cst);
  // Are there waiters on the permit?
  if(atomic_load_explicit(&permit->waiters, memory_order_relaxed)!=atomic_load_explicit(&permit->waited, memory_order_relaxed))
  { // There are indeed waiters. Loop waking until at least one thread takes the permit
    while(*(const unsigned *)"1PER"==permit->magic && atomic_load_explicit(&permit->permit, memory_order_relaxed))
    {
      if(thrd_success!=cnd_signal(&permit->cond))
      {
        ret=thrd_error;
        break;
      }
      //if(1==cpus) thrd_yield();
    }
  }
  return ret;
}

void pthread_permit1_revoke(pthread_permit1_t *permit)
{
  if(*(const unsigned *)"1PER"!=permit->magic) return;
  atomic_store_explicit(&permit->permit, 0U, memory_order_relaxed);
}

int pthread_permit1_wait(pthread_permit1_t *permit, pthread_mutex_t *mtx)
{
  int ret=thrd_success;
  unsigned expected;
  if(*(const unsigned *)"1PER"!=permit->magic) return thrd_error;
  // Increment the monotonic count to indicate we have entered a wait
  atomic_fetch_add_explicit(&permit->waiters, 1U, memory_order_acquire);
  // Check again if we have been deleted
  if(*(const unsigned *)"1PER"!=permit->magic)
  {
    atomic_fetch_add_explicit(&permit->waited, 1U, memory_order_relaxed);
    return thrd_error;
  }
  // Fetch me a permit
  while((expected=1, !atomic_compare_exchange_weak_explicit(&permit->permit, &expected, 0U, memory_order_relaxed, memory_order_relaxed)))
  { // Permit is not granted, so wait if we have a mutex
    if(mtx)
    {
      if(thrd_success!=cnd_wait(&permit->cond, mtx)) { ret=thrd_error; break; }
    }
    else thrd_yield();
  }
  // Increment the monotonic count to indicate we have exited a wait
  atomic_fetch_add_explicit(&permit->waited, 1U, memory_order_relaxed);
  return ret;
}

int pthread_permit1_timedwait(pthread_permit1_t *permit, pthread_mutex_t *mtx, const struct timespec *ts)
{
  int ret=thrd_success;
  unsigned expected;
  struct timespec now;
  if(*(const unsigned *)"1PER"!=permit->magic) return thrd_error;
  // Increment the monotonic count to indicate we have entered a wait
  atomic_fetch_add_explicit(&permit->waiters, 1U, memory_order_acquire);
  // Check again if we have been deleted
  if(*(const unsigned *)"1PER"!=permit->magic)
  {
    atomic_fetch_add_explicit(&permit->waited, 1U, memory_order_relaxed);
    return thrd_error;
  }
  // Fetch me a permit
  while((expected=1, !atomic_compare_exchange_weak_explicit(&permit->permit, &expected, 0U, memory_order_relaxed, memory_order_relaxed)))
  { // Permit is not granted, so wait if we have a mutex and a timeout
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
      int cndret=cnd_timedwait(&permit->cond, mtx, ts);
      if(thrd_success!=cndret && thrd_timeout!=cndret) { ret=cndret; break; }
    }
    else thrd_yield();
  }
  // Increment the monotonic count to indicate we have exited a wait
  atomic_fetch_add_explicit(&permit->waited, 1U, memory_order_relaxed);
  return ret;
}

typedef struct pthread_permit_select_s pthread_permit_select_t;
struct pthread_permitc_s
{ /* NOTE: KEEP THIS HEADER THE SAME AS pthread_permit1_t to allow its grant() to optionally work here */
  atomic_uint magic;                  /* Used to ensure this structure is valid */
  atomic_uint permit;                 /* =0 no permit, =1 yes permit */
  atomic_uint waiters, waited;        /* Keeps track of when a thread waits and wakes */
  cnd_t cond;                         /* Wakes anything waiting for a permit */

  /* Extensions from pthread_permit1_t type */
  unsigned replacePermit;             /* What to replace the permit with when consumed */
  atomic_uint lockWake;               /* Used to exclude new wakers if and only if waiters don't consume */
  pthread_permitc_hook_t *PTHREAD_PERMIT_RESTRICT hooks[PTHREAD_PERMIT_HOOK_TYPE_LAST];
  pthread_permit_select_t *volatile PTHREAD_PERMIT_RESTRICT selects[64]; /* select permit parent */
};
struct pthread_permitnc_s
{ /* NOTE: KEEP THIS HEADER THE SAME AS pthread_permit1_t to allow its grant() to optionally work here */
  atomic_uint magic;                  /* Used to ensure this structure is valid */
  atomic_uint permit;                 /* =0 no permit, =1 yes permit */
  atomic_uint waiters, waited;        /* Keeps track of when a thread waits and wakes */
  cnd_t cond;                         /* Wakes anything waiting for a permit */

  /* Extensions from pthread_permit1_t type */
  unsigned replacePermit;             /* What to replace the permit with when consumed */
  atomic_uint lockWake;               /* Used to exclude new wakers if and only if waiters don't consume */
  pthread_permitnc_hook_t *PTHREAD_PERMIT_RESTRICT hooks[PTHREAD_PERMIT_HOOK_TYPE_LAST];
  pthread_permit_select_t *volatile PTHREAD_PERMIT_RESTRICT selects[64]; /* select permit parent */
};

#endif // DOXYGEN_PREPROCESSOR

#ifdef __cplusplus
}
#endif

#endif
