#include "monsoon.h"

const std::string LOG_HEAD = "[TASK] ";

void test_fiber_1() {
  std::cout << LOG_HEAD << "tid = " << monsoon::GetThreadId()
            << ",test_fiber_1 begin" << std::endl;

  std::cout << LOG_HEAD << "tid = " << monsoon::GetThreadId()
            << ",test_fiber_1 finish" << std::endl;
}

void test_fiber_2() {
  std::cout << LOG_HEAD << "tid = " << monsoon::GetThreadId()
            << ",test_fiber_2 begin" << std::endl;

  sleep(3);
  std::cout << LOG_HEAD << "tid = " << monsoon::GetThreadId()
            << ",test_fiber_2 finish" << std::endl;
}

void test_fiber_3() {
  std::cout << LOG_HEAD << "tid = " << monsoon::GetThreadId()
            << ",test_fiber_3 begin" << std::endl;

  std::cout << LOG_HEAD << "tid = " << monsoon::GetThreadId()
            << ",test_fiber_3 finish" << std::endl;
}

void test_fiber_4() {
  std::cout << LOG_HEAD << "tid = " << monsoon::GetThreadId()
            << ",test_fiber_4 begin" << std::endl;

  std::cout << LOG_HEAD << "tid = " << monsoon::GetThreadId()
            << ",test_fiber_4 finish" << std::endl;
}

void test_user_caller_1() {
  std::cout << "main begin" << std::endl;

  monsoon::Scheduler sc;

  sc.scheduler(test_fiber_1);
  sc.scheduler(test_fiber_2);

  monsoon::Fiber::ptr fiber(new monsoon::Fiber(&test_fiber_3));
  sc.scheduler(fiber);

  sc.start();

  sc.stop();

  std::cout << "main end" << std::endl;
}

void test_user_caller_2() {
  std::cout << "main begin" << std::endl;

  monsoon::Scheduler sc(3, true);

  sc.scheduler(test_fiber_1);
  sc.scheduler(test_fiber_2);

  monsoon::Fiber::ptr fiber(new monsoon::Fiber(&test_fiber_3));
  sc.scheduler(fiber);

  sc.start();

  sc.scheduler(test_fiber_4);

  sleep(5);

  sc.stop();

  std::cout << "main end" << std::endl;
}

int main() { test_user_caller_2(); }