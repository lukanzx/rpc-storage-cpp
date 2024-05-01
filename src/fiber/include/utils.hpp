#ifndef __MONSOON_UTIL_H__
#define __MONSOON_UTIL_H__

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cxxabi.h>
#include <execinfo.h>
#include <iostream>
#include <pthread.h>
#include <sstream>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>
#include <vector>

namespace monsoon {
pid_t GetThreadId();
u_int32_t GetFiberId();

static uint64_t GetElapsedMS() {
  struct timespec ts = {0};
  clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
  return ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static std::string demangle(const char *str) {
  size_t size = 0;
  int status = 0;
  std::string rt;
  rt.resize(256);
  if (1 == sscanf(str, "%*[^(]%*[^_]%255[^)+]", &rt[0])) {

    char *v = abi::__cxa_demangle(&rt[0], nullptr, &size, &status);
    if (v) {
      std::string result(v);
      free(v);
      return result;
    }
  }

  if (1 == sscanf(str, "%255s", &rt[0])) {
    return rt;
  }
  return str;
}

static void Backtrace(std::vector<std::string> &bt, int size, int skip) {

  void **array = (void **)malloc((sizeof(void *) * size));
  size_t s = ::backtrace(array, size);

  char **strings = backtrace_symbols(array, s);
  if (strings == NULL) {
    std::cout << "backtrace_synbols error" << std::endl;
    return;
  }

  for (size_t i = skip; i < s; ++i) {
    bt.push_back(demangle(strings[i]));
  }

  free(strings);
  free(array);
}

static std::string BacktraceToString(int size, int skip,
                                     const std::string &prefix) {
  std::vector<std::string> bt;
  Backtrace(bt, size, skip);
  std::stringstream ss;
  for (size_t i = 0; i < bt.size(); ++i) {
    ss << prefix << bt[i] << std::endl;
  }
  return ss.str();
}

static void CondPanic(bool condition, std::string err) {
  if (!condition) {
    std::cout << "[assert by] (" << __FILE__ << ":" << __LINE__
              << "),err: " << err << std::endl;
    std::cout << "[backtrace]\n" << BacktraceToString(6, 3, "") << std::endl;
    assert(condition);
  }
}
} // namespace monsoon

#endif