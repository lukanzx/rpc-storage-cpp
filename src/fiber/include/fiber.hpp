#ifndef __MONSOON_FIBER_H__
#define __MONSOON_FIBER_H__

#include "utils.hpp"
#include <functional>
#include <iostream>
#include <memory>
#include <stdio.h>
#include <ucontext.h>
#include <unistd.h>

namespace monsoon {
class Fiber : public std::enable_shared_from_this<Fiber> {
public:
  typedef std::shared_ptr<Fiber> ptr;

  enum State {
    READY,
    RUNNING,
    TERM,
  };

private:
  Fiber();

public:
  Fiber(std::function<void()> cb, size_t stackSz = 0,
        bool run_in_scheduler = true);
  ~Fiber();

  void reset(std::function<void()> cb);

  void resume();

  void yield();

  uint64_t getId() const { return id_; }

  State getState() const { return state_; }

  static void SetThis(Fiber *f);

  static Fiber::ptr GetThis();

  static uint64_t TotalFiberNum();

  static void MainFunc();

  static uint64_t GetCurFiberID();

private:
  uint64_t id_ = 0;

  uint32_t stackSize_ = 0;

  State state_ = READY;

  ucontext_t ctx_;

  void *stack_ptr = nullptr;

  std::function<void()> cb_;

  bool isRunInScheduler_;
};
} // namespace monsoon

#endif