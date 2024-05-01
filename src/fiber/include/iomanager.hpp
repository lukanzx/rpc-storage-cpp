#ifndef __SYLAR_IOMANAGER_H__
#define __SYLAR_IOMANAGER_H__

#include "fcntl.h"
#include "scheduler.hpp"
#include "string.h"
#include "sys/epoll.h"
#include "timer.hpp"

namespace monsoon {
enum Event {
  NONE = 0x0,
  READ = 0x1,
  WRITE = 0x4,
};

struct EventContext {
  Scheduler *scheduler = nullptr;
  Fiber::ptr fiber;
  std::function<void()> cb;
};

class FdContext {
  friend class IOManager;

public:
  EventContext &getEveContext(Event event);

  void resetEveContext(EventContext &ctx);

  void triggerEvent(Event event);

private:
  EventContext read;
  EventContext write;
  int fd = 0;
  Event events = NONE;
  Mutex mutex;
};

class IOManager : public Scheduler, public TimerManager {
public:
  typedef std::shared_ptr<IOManager> ptr;

  IOManager(size_t threads = 1, bool use_caller = true,
            const std::string &name = "IOManager");
  ~IOManager();

  int addEvent(int fd, Event event, std::function<void()> cb = nullptr);

  bool delEvent(int fd, Event event);

  bool cancelEvent(int fd, Event event);

  bool cancelAll(int fd);
  static IOManager *GetThis();

protected:
  void tickle() override;

  bool stopping() override;

  void idle() override;

  bool stopping(uint64_t &timeout);

  void OnTimerInsertedAtFront() override;
  void contextResize(size_t size);

private:
  int epfd_ = 0;
  int tickleFds_[2];

  std::atomic<size_t> pendingEventCnt_ = {0};
  RWMutex mutex_;
  std::vector<FdContext *> fdContexts_;
};
} // namespace monsoon

#endif