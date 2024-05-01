#ifndef UTIL_H
#define UTIL_H

#include "config.h"
#include <arpa/inet.h>
#include <boost/archive/text_iarchive.hpp>
#include <boost/archive/text_oarchive.hpp>
#include <boost/serialization/access.hpp>
#include <condition_variable>
#include <functional>
#include <iostream>
#include <mutex>
#include <queue>
#include <random>
#include <sstream>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

template <class F> class DeferClass {
public:
  DeferClass(F &&f) : m_func(std::forward<F>(f)) {}
  DeferClass(const F &f) : m_func(f) {}
  ~DeferClass() { m_func(); }

  DeferClass(const DeferClass &e) = delete;
  DeferClass &operator=(const DeferClass &e) = delete;

private:
  F m_func;
};

#define _CONCAT(a, b) a##b
#define _MAKE_DEFER_(line) DeferClass _CONCAT(defer_placeholder, line) = [&]()

#undef DEFER
#define DEFER _MAKE_DEFER_(__LINE__)

void DPrintf(const char *format, ...);

void myAssert(bool condition, std::string message = "Assertion failed!");

template <typename... Args>
std::string format(const char *format_str, Args... args) {
  int size_s = std::snprintf(nullptr, 0, format_str, args...) + 1;
  if (size_s <= 0) {
    throw std::runtime_error("Error during formatting.");
  }
  auto size = static_cast<size_t>(size_s);
  std::vector<char> buf(size);
  std::snprintf(buf.data(), size, format_str, args...);
  return std::string(buf.data(), buf.data() + size - 1);
}

std::chrono::_V2::system_clock::time_point now();

std::chrono::milliseconds getRandomizedElectionTimeout();
void sleepNMilliseconds(int N);

template <typename T> class LockQueue {
public:
  void Push(const T &data) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_queue.push(data);
    m_condvariable.notify_one();
  }

  T Pop() {
    std::unique_lock<std::mutex> lock(m_mutex);
    while (m_queue.empty()) {

      m_condvariable.wait(lock);
    }
    T data = m_queue.front();
    m_queue.pop();
    return data;
  }

  bool timeOutPop(int timeout, T *ResData) {
    std::unique_lock<std::mutex> lock(m_mutex);

    auto now = std::chrono::system_clock::now();
    auto timeout_time = now + std::chrono::milliseconds(timeout);

    while (m_queue.empty()) {

      if (m_condvariable.wait_until(lock, timeout_time) ==
          std::cv_status::timeout) {
        return false;
      } else {
        continue;
      }
    }

    T data = m_queue.front();
    m_queue.pop();
    *ResData = data;
    return true;
  }

private:
  std::queue<T> m_queue;
  std::mutex m_mutex;
  std::condition_variable m_condvariable;
};

class Op {
public:
  std::string Operation;
  std::string Key;
  std::string Value;
  std::string ClientId;
  int RequestId;

public:
  std::string asString() const {
    std::stringstream ss;
    boost::archive::text_oarchive oa(ss);

    oa << *this;

    return ss.str();
  }

  bool parseFromString(std::string str) {
    std::stringstream iss(str);
    boost::archive::text_iarchive ia(iss);

    ia >> *this;
    return true;
  }

public:
  friend std::ostream &operator<<(std::ostream &os, const Op &obj) {
    os << "[MyClass:Operation{" + obj.Operation + "},Key{" + obj.Key +
              "},Value{" + obj.Value + "},ClientId{" + obj.ClientId +
              "},RequestId{" + std::to_string(obj.RequestId) + "}";
    return os;
  }

private:
  friend class boost::serialization::access;
  template <class Archive>
  void serialize(Archive &ar, const unsigned int version) {
    ar & Operation;
    ar & Key;
    ar & Value;
    ar & ClientId;
    ar & RequestId;
  }
};

const std::string OK = "OK";
const std::string ErrNoKey = "ErrNoKey";
const std::string ErrWrongLeader = "ErrWrongLeader";

bool isReleasePort(unsigned short usPort);

bool getReleasePort(short &port);

#endif