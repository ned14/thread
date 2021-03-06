//===----------------------------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
// Copyright (C) 2011 Vicente J. Botet Escriba
//
//  Distributed under the Boost Software License, Version 1.0. (See accompanying
//  file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

// <boost/thread/permit>

// class permit_c_any;

// permit_c_any(const permit_c_any&) = delete;

#include <boost/thread/permit.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/thread/thread.hpp>
#include <boost/detail/lightweight_test.hpp>

#if defined BOOST_THREAD_USES_CHRONO

class Pred
{
  int& i_;
public:
  explicit Pred(int& i) :
    i_(i)
  {
  }

  bool operator()()
  {
    return i_ != 0;
  }
};

boost::permit_c_any cv;

typedef boost::timed_mutex L0;
typedef boost::unique_lock<L0> L1;

L0 m0;

int test1 = 0;
int test2 = 0;

int runs = 0;

void f()
{
  typedef boost::chrono::system_clock Clock;
  typedef boost::chrono::milliseconds milliseconds;
  L1 lk(m0);
  BOOST_TEST(test2 == 0);
  test1 = 1;
  cv.notify_one();
  Clock::time_point t0 = Clock::now();
  //bool r =
      (void)cv.wait_for(lk, milliseconds(250), Pred(test2));
  Clock::time_point t1 = Clock::now();
  if (runs == 0)
  {
    BOOST_TEST(t1 - t0 < milliseconds(250));
    BOOST_TEST(test2 != 0);
  }
  else
  {
    BOOST_TEST(t1 - t0 - milliseconds(250) < milliseconds(250+5));
    BOOST_TEST(test2 == 0);
  }
  ++runs;
}

int main()
{
  {
    L1 lk(m0);
    boost::thread t(f);
    BOOST_TEST(test1 == 0);
    while (test1 == 0)
      cv.wait(lk);
    BOOST_TEST(test1 != 0);
    test2 = 1;
    lk.unlock();
    cv.notify_one();
    t.join();
  }
  test1 = 0;
  test2 = 0;
  {
    L1 lk(m0);
    boost::thread t(f);
    BOOST_TEST(test1 == 0);
    while (test1 == 0)
      cv.wait(lk);
    BOOST_TEST(test1 != 0);
    lk.unlock();
    t.join();
  }

  return boost::report_errors();
}

#else
#error "Test not applicable: BOOST_THREAD_USES_CHRONO not defined for this platform as not supported"
#endif
