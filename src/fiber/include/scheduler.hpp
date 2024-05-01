#ifndef __MONSOON_SCHEDULER_H__
#define __MONSOON_SCHEDULER_H__

#include "fiber.hpp"
#include "mutex.hpp"
#include "thread.hpp"
#include "utils.hpp"
#include <atomic>
#include <boost/type_index.hpp>
#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <vector>

namespace monsoon {

class SchedulerTask {
public:
  friend class Scheduler;
  SchedulerTask() { thread_ = -1; }
  SchedulerTask(Fiber::ptr f, int t) : fiber_(f), thread_(t) {}
  SchedulerTask(Fiber::ptr *f, int t) {
    fiber_.swap(*f);
    thread_ = t;
  }
  SchedulerTask(std::function<void()> f, int t) {

    cb_ = f;
    thread_ = t;
  }

  void reset() {
    fiber_ = nullptr;
    cb_ = nullptr;
    thread_ = -1;
  }

private:
  Fiber::ptr fiber_;
  std::function<void()> cb_;
  int thread_;
};

class Scheduler {
public:
  typedef std::shared_ptr<Scheduler> ptr;

  Scheduler(size_t threads = 1, bool use_caller = true,
            const std::string &name = "Scheduler");
  virtual ~Scheduler();
  const std::string &getName() const { return name_; }

  static Scheduler *GetThis();

  static Fiber *GetMainFiber();

  template <class TaskType> void scheduler(TaskType task, int thread = -1) {
    bool isNeedTickle = false;
    {
      Mutex::Lock lock(mutex_);
      isNeedTickle = schedulerNoLock(task, thread);
    }

    if (isNeedTickle) {
      tickle();
    }
  }

  void start();

  void stop();

protected:
  virtual void tickle();

  void run();

  virtual void idle();

  virtual bool stopping();

  void setThis();

  bool isHasIdleThreads() { return idleThreadCnt_ > 0; }

private:
  template <class TaskType> bool schedulerNoLock(TaskType t, int thread) {
    bool isNeedTickle = tasks_.empty();
    SchedulerTask task(t, thread);
    if (task.fiber_ || task.cb_) {

      tasks_.push_back(task);
    }

    return isNeedTickle;
  }

  std::string name_;

  Mutex mutex_;

  std::vector<Thread::ptr> threadPool_;

  std::list<SchedulerTask> tasks_;

  std::vector<int> threadIds_;

  size_t threadCnt_ = 0;

  std::atomic<size_t> activeThreadCnt_ = {0};

  std::atomic<size_t> idleThreadCnt_ = {0};

  bool isUseCaller_;

  Fiber::ptr rootFiber_;

  int rootThread_ = 0;
  bool isStopped_ = false;
};
} // namespace monsoon

#endif