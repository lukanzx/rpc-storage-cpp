#include "iomanager.hpp"

namespace monsoon {

EventContext &FdContext::getEveContext(Event event) {
  switch (event) {
  case READ:
    return read;
  case WRITE:
    return write;
  default:
    CondPanic(false, "getContext error: unknow event");
  }
  throw std::invalid_argument("getContext invalid event");
}

void FdContext::resetEveContext(EventContext &ctx) {
  ctx.scheduler = nullptr;
  ctx.fiber.reset();
  ctx.cb = nullptr;
}

void FdContext::triggerEvent(Event event) {
  CondPanic(events & event, "event hasn't been registed");
  events = (Event)(events & ~event);
  EventContext &ctx = getEveContext(event);
  if (ctx.cb) {
    ctx.scheduler->scheduler(ctx.cb);
  } else {
    ctx.scheduler->scheduler(ctx.fiber);
  }
  resetEveContext(ctx);
  return;
}

IOManager::IOManager(size_t threads, bool use_caller, const std::string &name)
    : Scheduler(threads, use_caller, name) {
  epfd_ = epoll_create(5000);
  int ret = pipe(tickleFds_);
  CondPanic(ret == 0, "pipe error");

  epoll_event event{};
  memset(&event, 0, sizeof(epoll_event));
  event.events = EPOLLIN | EPOLLET;
  event.data.fd = tickleFds_[0];

  ret = fcntl(tickleFds_[0], F_SETFL, O_NONBLOCK);
  CondPanic(ret == 0, "set fd nonblock error");

  ret = epoll_ctl(epfd_, EPOLL_CTL_ADD, tickleFds_[0], &event);
  CondPanic(ret == 0, "epoll_ctl error");

  contextResize(32);

  start();
}
IOManager::~IOManager() {
  stop();
  close(epfd_);
  close(tickleFds_[0]);
  close(tickleFds_[1]);

  for (size_t i = 0; i < fdContexts_.size(); i++) {
    if (fdContexts_[i]) {
      delete fdContexts_[i];
    }
  }
}

int IOManager::addEvent(int fd, Event event, std::function<void()> cb) {
  FdContext *fd_ctx = nullptr;
  RWMutex::ReadLock lock(mutex_);

  if ((int)fdContexts_.size() > fd) {
    fd_ctx = fdContexts_[fd];
    lock.unlock();
  } else {
    lock.unlock();
    RWMutex::WriteLock lock2(mutex_);
    contextResize(fd * 1.5);
    fd_ctx = fdContexts_[fd];
  }

  Mutex::Lock ctxLock(fd_ctx->mutex);
  CondPanic(!(fd_ctx->events & event), "addevent error, fd = " + fd);

  int op = fd_ctx->events ? EPOLL_CTL_MOD : EPOLL_CTL_ADD;
  epoll_event epevent;
  epevent.events = EPOLLET | fd_ctx->events | event;
  epevent.data.ptr = fd_ctx;

  int ret = epoll_ctl(epfd_, op, fd, &epevent);
  if (ret) {
    std::cout << "addevent: epoll ctl error" << std::endl;
    return -1;
  }

  ++pendingEventCnt_;

  fd_ctx->events = (Event)(fd_ctx->events | event);
  EventContext &event_ctx = fd_ctx->getEveContext(event);
  CondPanic(!event_ctx.scheduler && !event_ctx.fiber && !event_ctx.cb,
            "event_ctx is nullptr");

  event_ctx.scheduler = Scheduler::GetThis();
  if (cb) {

    event_ctx.cb.swap(cb);
  } else {

    event_ctx.fiber = Fiber::GetThis();
    CondPanic(event_ctx.fiber->getState() == Fiber::RUNNING,
              "state=" + event_ctx.fiber->getState());
  }
  std::cout << "add event success,fd = " << fd << std::endl;
  return 0;
}

bool IOManager::delEvent(int fd, Event event) {
  RWMutex::ReadLock lock(mutex_);
  if ((int)fdContexts_.size() <= fd) {

    return false;
  }
  FdContext *fd_ctx = fdContexts_[fd];
  lock.unlock();

  Mutex::Lock ctxLock(fd_ctx->mutex);
  if (!(fd_ctx->events & event)) {
    return false;
  }

  Event new_events = (Event)(fd_ctx->events & ~event);
  int op = new_events ? EPOLL_CTL_MOD : EPOLL_CTL_DEL;
  epoll_event epevent;
  epevent.events = EPOLLET | new_events;
  epevent.data.ptr = fd_ctx;

  int ret = epoll_ctl(epfd_, op, fd, &epevent);
  if (ret) {
    std::cout << "delevent: epoll_ctl error" << std::endl;
    return false;
  }
  --pendingEventCnt_;
  fd_ctx->events = new_events;
  EventContext &event_ctx = fd_ctx->getEveContext(event);
  fd_ctx->resetEveContext(event_ctx);
  return true;
}

bool IOManager::cancelEvent(int fd, Event event) {
  RWMutex::ReadLock lock(mutex_);
  if ((int)fdContexts_.size() <= fd) {

    return false;
  }
  FdContext *fd_ctx = fdContexts_[fd];
  lock.unlock();

  Mutex::Lock ctxLock(fd_ctx->mutex);
  if (!(fd_ctx->events & event)) {
    return false;
  }

  Event new_events = (Event)(fd_ctx->events & ~event);
  int op = new_events ? EPOLL_CTL_MOD : EPOLL_CTL_DEL;
  epoll_event epevent;
  epevent.events = EPOLLET | new_events;
  epevent.data.ptr = fd_ctx;

  int ret = epoll_ctl(epfd_, op, fd, &epevent);
  if (ret) {
    std::cout << "delevent: epoll_ctl error" << std::endl;
    return false;
  }

  fd_ctx->triggerEvent(event);
  --pendingEventCnt_;
  return true;
}

bool IOManager::cancelAll(int fd) {
  RWMutex::ReadLock lock(mutex_);
  if ((int)fdContexts_.size() <= fd) {

    return false;
  }
  FdContext *fd_ctx = fdContexts_[fd];
  lock.unlock();

  Mutex::Lock ctxLock(fd_ctx->mutex);
  if (!fd_ctx->events) {
    return false;
  }

  int op = EPOLL_CTL_DEL;
  epoll_event epevent;
  epevent.events = 0;
  epevent.data.ptr = fd_ctx;

  int ret = epoll_ctl(epfd_, op, fd, &epevent);
  if (ret) {
    std::cout << "delevent: epoll_ctl error" << std::endl;
    return false;
  }

  if (fd_ctx->events & READ) {
    fd_ctx->triggerEvent(READ);
    --pendingEventCnt_;
  }
  if (fd_ctx->events & WRITE) {
    fd_ctx->triggerEvent(WRITE);
    --pendingEventCnt_;
  }
  CondPanic(fd_ctx->events == 0, "fd not totally clear");
  return true;
}
IOManager *IOManager::GetThis() {
  return dynamic_cast<IOManager *>(Scheduler::GetThis());
}

void IOManager::tickle() {
  if (!isHasIdleThreads()) {

    return;
  }

  int rt = write(tickleFds_[1], "T", 1);
  CondPanic(rt == 1, "write pipe error");
}

void IOManager::idle() {

  const uint64_t MAX_EVENTS = 256;
  epoll_event *events = new epoll_event[MAX_EVENTS]();
  std::shared_ptr<epoll_event> shared_events(
      events, [](epoll_event *ptr) { delete[] ptr; });

  while (true) {

    uint64_t next_timeout = 0;
    if (stopping(next_timeout)) {
      std::cout << "name=" << getName() << "idle stopping exit";
      break;
    }

    int ret = 0;
    do {
      static const int MAX_TIMEOUT = 5000;

      if (next_timeout != ~0ull) {
        next_timeout = std::min((int)next_timeout, MAX_TIMEOUT);
      } else {
        next_timeout = MAX_TIMEOUT;
      }

      ret = epoll_wait(epfd_, events, MAX_EVENTS, (int)next_timeout);

      if (ret < 0) {
        if (errno == EINTR) {

          continue;
        }
        std::cout << "epoll_wait [" << epfd_ << "] errno,err: " << errno
                  << std::endl;
        break;
      } else {
        break;
      }
    } while (true);

    std::vector<std::function<void()>> cbs;
    listExpiredCb(cbs);
    if (!cbs.empty()) {
      for (const auto &cb : cbs) {
        scheduler(cb);
      }
      cbs.clear();
    }

    for (int i = 0; i < ret; i++) {
      epoll_event &event = events[i];
      if (event.data.fd == tickleFds_[0]) {

        uint8_t dummy[256];

        while (read(tickleFds_[0], dummy, sizeof(dummy)) > 0)
          ;
        continue;
      }

      FdContext *fd_ctx = (FdContext *)event.data.ptr;
      Mutex::Lock lock(fd_ctx->mutex);

      if (event.events & (EPOLLERR | EPOLLHUP)) {
        std::cout << "error events" << std::endl;
        event.events |= (EPOLLIN | EPOLLOUT) & fd_ctx->events;
      }

      int real_events = NONE;
      if (event.events & EPOLLIN) {
        real_events |= READ;
      }
      if (event.events & EPOLLOUT) {
        real_events |= WRITE;
      }
      if ((fd_ctx->events & real_events) == NONE) {

        continue;
      }

      int left_events = (fd_ctx->events & ~real_events);
      int op = left_events ? EPOLL_CTL_MOD : EPOLL_CTL_DEL;
      event.events = EPOLLET | left_events;

      int ret2 = epoll_ctl(epfd_, op, fd_ctx->fd, &event);
      if (ret2) {
        std::cout << "epoll_wait [" << epfd_ << "] errno,err: " << errno
                  << std::endl;
        continue;
      }

      if (real_events & READ) {
        fd_ctx->triggerEvent(READ);
        --pendingEventCnt_;
      }
      if (real_events & WRITE) {
        fd_ctx->triggerEvent(WRITE);
        --pendingEventCnt_;
      }
    }

    Fiber::ptr cur = Fiber::GetThis();
    auto raw_ptr = cur.get();
    cur.reset();

    raw_ptr->yield();
  }
}

bool IOManager::stopping() {
  uint64_t timeout = 0;
  return stopping(timeout);
}

bool IOManager::stopping(uint64_t &timeout) {

  timeout = getNextTimer();
  return timeout == ~0ull && pendingEventCnt_ == 0 && Scheduler::stopping();
}

void IOManager::contextResize(size_t size) {
  fdContexts_.resize(size);
  for (size_t i = 0; i < fdContexts_.size(); i++) {
    if (!fdContexts_[i]) {
      fdContexts_[i] = new FdContext;
      fdContexts_[i]->fd = i;
    }
  }
}
void IOManager::OnTimerInsertedAtFront() { tickle(); }

} // namespace monsoon
