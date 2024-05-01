#ifndef __MONSOON_TIMER_H__
#define __MONSSON_TIMER_H__

#include "mutex.hpp"
#include <memory>
#include <set>
#include <vector>

namespace monsoon {
class TimerManager;

class Timer : public std::enable_shared_from_this<Timer> {
  friend class TimerManager;

public:
  typedef std::shared_ptr<Timer> ptr;

  bool cancel();
  bool refresh();
  bool reset(uint64_t ms, bool from_now);

private:
  Timer(uint64_t ms, std::function<void()> cb, bool recuring,
        TimerManager *manager);
  Timer(uint64_t next);

  bool recurring_ = false;

  uint64_t ms_ = 0;

  uint64_t next_ = 0;

  std::function<void()> cb_;

  TimerManager *manager_ = nullptr;

private:
  struct Comparator {
    bool operator()(const Timer::ptr &lhs, const Timer::ptr &rhs) const;
  };
};

class TimerManager {
  friend class Timer;

public:
  TimerManager();
  virtual ~TimerManager();
  Timer::ptr addTimer(uint64_t ms, std::function<void()> cb,
                      bool recuring = false);
  Timer::ptr addConditionTimer(uint64_t ms, std::function<void()> cb,
                               std::weak_ptr<void> weak_cond,
                               bool recurring = false);

  uint64_t getNextTimer();

  void listExpiredCb(std::vector<std::function<void()>> &cbs);

  bool hasTimer();

protected:
  virtual void OnTimerInsertedAtFront() = 0;

  void addTimer(Timer::ptr val, RWMutex::WriteLock &lock);

private:
  bool detectClockRolllover(uint64_t now_ms);

  RWMutex mutex_;

  std::set<Timer::ptr, Timer::Comparator> timers_;

  bool tickled_ = false;

  uint64_t previouseTime_ = 0;
};
} // namespace monsoon

#endif