#include "scheduler.hpp"
#include "fiber.hpp"
#include "hook.hpp"

namespace monsoon {

static thread_local Scheduler *cur_scheduler = nullptr;

static thread_local Fiber *cur_scheduler_fiber = nullptr;

const std::string LOG_HEAD = "[scheduler] ";

Scheduler::Scheduler(size_t threads, bool use_caller, const std::string &name) {
  CondPanic(threads > 0, "threads <= 0");

  isUseCaller_ = use_caller;
  name_ = name;

  if (use_caller) {
    std::cout << LOG_HEAD << "current thread as called thread" << std::endl;

    --threads;

    Fiber::GetThis();
    std::cout << LOG_HEAD << "init caller thread's main fiber success"
              << std::endl;
    CondPanic(GetThis() == nullptr, "GetThis err:cur scheduler is not nullptr");

    cur_scheduler = this;

    rootFiber_.reset(new Fiber(std::bind(&Scheduler::run, this), 0, false));
    std::cout << LOG_HEAD << "init caller thread's caller fiber success"
              << std::endl;

    Thread::SetName(name_);
    cur_scheduler_fiber = rootFiber_.get();
    rootThread_ = GetThreadId();
    threadIds_.push_back(rootThread_);
  } else {
    rootThread_ = -1;
  }
  threadCnt_ = threads;
  std::cout << "-------scheduler init success-------" << std::endl;
}

Scheduler *Scheduler::GetThis() { return cur_scheduler; }
Fiber *Scheduler::GetMainFiber() { return cur_scheduler_fiber; }
void Scheduler::setThis() { cur_scheduler = this; }
Scheduler::~Scheduler() {
  CondPanic(isStopped_, "isstopped is false");
  if (GetThis() == this) {
    cur_scheduler = nullptr;
  }
}

void Scheduler::start() {
  std::cout << LOG_HEAD << "scheduler start" << std::endl;
  Mutex::Lock lock(mutex_);
  if (isStopped_) {
    std::cout << "scheduler has stopped" << std::endl;
    return;
  }
  CondPanic(threadPool_.empty(), "thread pool is not empty");
  threadPool_.resize(threadCnt_);
  for (size_t i = 0; i < threadCnt_; i++) {
    threadPool_[i].reset(new Thread(std::bind(&Scheduler::run, this),
                                    name_ + "_" + std::to_string(i)));
    threadIds_.push_back(threadPool_[i]->getId());
  }
}

void Scheduler::run() {
  std::cout << LOG_HEAD << "begin run" << std::endl;
  set_hook_enable(true);
  setThis();
  if (GetThreadId() != rootThread_) {

    cur_scheduler_fiber = Fiber::GetThis().get();
  }

  Fiber::ptr idleFiber(new Fiber(std::bind(&Scheduler::idle, this)));
  Fiber::ptr cbFiber;

  SchedulerTask task;
  while (true) {
    task.reset();

    bool tickle_me = false;
    {
      Mutex::Lock lock(mutex_);
      auto it = tasks_.begin();
      while (it != tasks_.end()) {

        if (it->thread_ != -1 && it->thread_ != GetThreadId()) {
          ++it;
          tickle_me = true;
          continue;
        }
        CondPanic(it->fiber_ || it->cb_, "task is nullptr");
        if (it->fiber_) {
          CondPanic(it->fiber_->getState() == Fiber::READY,
                    "fiber task state error");
        }

        task = *it;
        tasks_.erase(it++);
        ++activeThreadCnt_;
        break;
      }

      tickle_me |= (it != tasks_.end());
    }
    if (tickle_me) {
      tickle();
    }

    if (task.fiber_) {

      task.fiber_->resume();

      --activeThreadCnt_;
      task.reset();
    } else if (task.cb_) {
      if (cbFiber) {
        cbFiber->reset(task.cb_);
      } else {
        cbFiber.reset(new Fiber(task.cb_));
      }
      task.reset();
      cbFiber->resume();
      --activeThreadCnt_;
      cbFiber.reset();
    } else {

      if (idleFiber->getState() == Fiber::TERM) {
        std::cout << "idle fiber term" << std::endl;
        break;
      }

      ++idleThreadCnt_;
      idleFiber->resume();
      --idleThreadCnt_;
    }
  }
  std::cout << "run exit" << std::endl;
}

void Scheduler::tickle() { std::cout << "tickle" << std::endl; }

bool Scheduler::stopping() {
  Mutex::Lock lock(mutex_);
  return isStopped_ && tasks_.empty() && activeThreadCnt_ == 0;
}

void Scheduler::idle() {
  while (!stopping()) {
    Fiber::GetThis()->yield();
  }
}

void Scheduler::stop() {
  std::cout << LOG_HEAD << "stop" << std::endl;
  if (stopping()) {
    return;
  }
  isStopped_ = true;

  if (isUseCaller_) {
    CondPanic(GetThis() == this, "cur thread is not caller thread");
  } else {
    CondPanic(GetThis() != this, "cur thread is caller thread");
  }

  for (size_t i = 0; i < threadCnt_; i++) {
    tickle();
  }
  if (rootFiber_) {
    tickle();
  }

  if (rootFiber_) {

    rootFiber_->resume();
    std::cout << "root fiber end" << std::endl;
  }

  std::vector<Thread::ptr> threads;
  {
    Mutex::Lock lock(mutex_);
    threads.swap(threadPool_);
  }
  for (auto &i : threads) {
    i->join();
  }
}

} // namespace monsoon
