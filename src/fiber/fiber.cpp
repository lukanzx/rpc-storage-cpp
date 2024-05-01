#include "fiber.hpp"
#include "scheduler.hpp"
#include "utils.hpp"
#include <cassert>
#include <atomic>

namespace monsoon {
const bool DEBUG = true;

static thread_local Fiber *cur_fiber = nullptr;

static thread_local Fiber::ptr cur_thread_fiber = nullptr;

static std::atomic<uint64_t> cur_fiber_id{0};

static std::atomic<uint64_t> fiber_count{0};

static int g_fiber_stack_size = 128 * 1024;

class StackAllocator {
public:
  static void *Alloc(size_t size) { return malloc(size); }
  static void Delete(void *vp, size_t size) { return free(vp); }
};

Fiber::Fiber() {
  SetThis(this);
  state_ = RUNNING;
  CondPanic(getcontext(&ctx_) == 0, "getcontext error");
  ++fiber_count;
  id_ = cur_fiber_id++;
  std::cout << "[fiber] create fiber , id = " << id_ << std::endl;
}

void Fiber::SetThis(Fiber *f) { cur_fiber = f; }

Fiber::ptr Fiber::GetThis() {
  if (cur_fiber) {
    return cur_fiber->shared_from_this();
  }

  Fiber::ptr main_fiber(new Fiber);
  CondPanic(cur_fiber == main_fiber.get(), "cur_fiber need to be main_fiber");
  cur_thread_fiber = main_fiber;
  return cur_fiber->shared_from_this();
}

Fiber::Fiber(std::function<void()> cb, size_t stacksize, bool run_inscheduler)
    : id_(cur_fiber_id++), cb_(cb), isRunInScheduler_(run_inscheduler) {
  ++fiber_count;
  stackSize_ = stacksize > 0 ? stacksize : g_fiber_stack_size;
  stack_ptr = StackAllocator::Alloc(stackSize_);
  CondPanic(getcontext(&ctx_) == 0, "getcontext error");

  ctx_.uc_link = nullptr;
  ctx_.uc_stack.ss_sp = stack_ptr;
  ctx_.uc_stack.ss_size = stackSize_;
  makecontext(&ctx_, &Fiber::MainFunc, 0);
}

void Fiber::resume() {
  CondPanic(state_ != TERM && state_ != RUNNING, "state error");
  SetThis(this);
  state_ = RUNNING;

  if (isRunInScheduler_) {

    CondPanic(0 == swapcontext(&(Scheduler::GetMainFiber()->ctx_), &ctx_),
              "isRunInScheduler_ = true,swapcontext error");
  } else {

    CondPanic(0 == swapcontext(&(cur_thread_fiber->ctx_), &ctx_),
              "isRunInScheduler_ = false,swapcontext error");
  }
}

void Fiber::yield() {
  CondPanic(state_ == TERM || state_ == RUNNING, "state error");
  SetThis(cur_thread_fiber.get());
  if (state_ != TERM) {
    state_ = READY;
  }
  if (isRunInScheduler_) {
    CondPanic(0 == swapcontext(&ctx_, &(Scheduler::GetMainFiber()->ctx_)),
              "isRunInScheduler_ = true,swapcontext error");
  } else {

    CondPanic(0 == swapcontext(&ctx_, &(cur_thread_fiber->ctx_)),
              "swapcontext failed");
  }
}

void Fiber::MainFunc() {
  Fiber::ptr cur = GetThis();
  CondPanic(cur != nullptr, "cur is nullptr");

  cur->cb_();
  cur->cb_ = nullptr;
  cur->state_ = TERM;

  auto raw_ptr = cur.get();
  cur.reset();

  raw_ptr->yield();
}

void Fiber::reset(std::function<void()> cb) {
  CondPanic(stack_ptr, "stack is nullptr");
  CondPanic(state_ == TERM, "state isn't TERM");
  cb_ = cb;
  CondPanic(0 == getcontext(&ctx_), "getcontext failed");
  ctx_.uc_link = nullptr;
  ctx_.uc_stack.ss_sp = stack_ptr;
  ctx_.uc_stack.ss_size = stackSize_;

  makecontext(&ctx_, &Fiber::MainFunc, 0);
  state_ = READY;
}

Fiber::~Fiber() {
  --fiber_count;
  if (stack_ptr) {

    CondPanic(state_ == TERM, "fiber state should be term");
    StackAllocator::Delete(stack_ptr, stackSize_);

  } else {

    CondPanic(!cb_, "main fiber no callback");
    CondPanic(state_ == RUNNING, "main fiber state should be running");

    Fiber *cur = cur_fiber;
    if (cur == this) {
      SetThis(nullptr);
    }
  }
}

} // namespace monsoon