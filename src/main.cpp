/*
    main.cpp
    Copyright (c) 2024-2026, Amit Gefen

    Permission is hereby granted, free of charge, to any person obtaining a copy
    of this software and associated documentation files (the "Software"), to
    deal in the Software without restriction, including without limitation the
    rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
    sell copies of the Software, and to permit persons to whom the Software is
    furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be included in
    all copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
    FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
    IN THE SOFTWARE.
*/

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <future>
#include <print>
#include <ranges>
#include <thread>
#include <vector>

#include "ThreadPool.hpp"

namespace {

constexpr size_t kDefaultPoolSize = 4;
constexpr uint32_t kDefaultThreadCount = 8;

// Task ids 1..kDefaultThreadCount; also each task's sleep time in seconds.
constexpr auto kTaskIds = std::views::iota(1U, kDefaultThreadCount + 1U);

}  // namespace

// Call: Log() to reset origin.
static void Log(uint32_t index = 0, const char* action = nullptr) {
  static uint64_t origin{0};

  const auto now = std::chrono::system_clock::now();
  const auto epoch = now.time_since_epoch();
  const auto seconds =
      std::chrono::duration_cast<std::chrono::seconds>(epoch).count();

  if (action == nullptr) {  // Reset origin
    origin = seconds;

    return;
  }

  // std::println writes each call as one unit, so lines from different threads
  // never interleave. "{:<2}" keeps the columns aligned past nine seconds.
  std::println("{:<2} task {} {}", seconds - origin, index, action);
}

// **** Callable function: ****

static void TestCallableObject() {
  // Create a ThreadPool with 4 threads
  amitgdev::ThreadPool pool(kDefaultPoolSize);

  // Vector to store futures of tasks
  std::vector<std::future<void>> futures;

  // Submit 8 tasks to the ThreadPool
  for (const uint32_t index : kTaskIds) {
    futures.emplace_back(pool.Submit([index] {
      // Simulate some task execution
      Log(index, "started");
      std::this_thread::sleep_for(std::chrono::seconds(index));  // 1 - 8
      Log(index, "stopped");
    }));
  }

  // Wait for all tasks to complete
  for (const auto& future : futures) {
    future.wait();
  }
}

// **** Function: ****

static void Function(uint32_t index) {
  Log(index, "started");
  std::this_thread::sleep_for(std::chrono::seconds(index));
  Log(index, "stopped");
}

static void TestFunction() {
  amitgdev::ThreadPool pool(kDefaultPoolSize);
  std::vector<std::future<void>> futures;

  for (const uint32_t index : kTaskIds) {
    futures.emplace_back(pool.Submit(Function, index));
  }

  for (const auto& future : futures) {
    future.wait();
  }
}

// **** Member Function: ****

namespace {

class MyClass final {
 public:
  void Test() {
    amitgdev::ThreadPool pool(kDefaultPoolSize);
    std::vector<std::future<void>> futures;

    for (const uint32_t index : kTaskIds) {
      futures.emplace_back(pool.Submit(&MyClass::MemberFunction, this, index));
    }

    for (const auto& future : futures) {
      future.wait();
    }
  }

 private:
  void MemberFunction(uint32_t index) {
    Log(index, "started");
    ++counter_;
    std::this_thread::sleep_for(std::chrono::seconds(index));
    Log(index, "stopped");
  }

  std::atomic_int_least64_t counter_{0};
};

}  // namespace

// **** Main: ****

int main() {
  try {
    std::println(
        "**** note: each task runs for a number of seconds equal to its id "
        "****");

    std::println("\n**** test callable object: ****");
    Log();
    TestCallableObject();

    std::println("\n**** test function: ****");
    Log();
    TestFunction();

    std::println("\n**** test member-function: ****");
    Log();
    MyClass my_class;
    my_class.Test();
    return EXIT_SUCCESS;
  } catch (const std::exception& e) {
    // fputs instead of std::println: the error path must not throw again.
    std::fputs(e.what(), stderr);
    std::fputs("\n", stderr);
    return EXIT_FAILURE;
  }
}