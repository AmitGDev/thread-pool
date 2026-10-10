/*
    ThreadPool.hpp
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

#ifndef AMITGDEV_THREAD_POOL_HPP_
#define AMITGDEV_THREAD_POOL_HPP_

#include <concepts>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <future>
#include <mutex>
#include <queue>
#include <stop_token>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace amitgdev {

// ThreadPool class manages a pool of threads for task execution
class ThreadPool final {
 public:
  explicit ThreadPool(std::size_t pool_size);
  ~ThreadPool() noexcept = default;

  // Non-copyable, non-moveable
  ThreadPool(const ThreadPool&) = delete;
  auto operator=(const ThreadPool&) -> ThreadPool& = delete;
  ThreadPool(ThreadPool&&) = delete;
  auto operator=(ThreadPool&&) -> ThreadPool& = delete;

  // Submit a callable with arguments. Accepts lambdas, functors, free
  // functions, and pointers to member functions (the object or a pointer to it
  // is the first argument, as with std::invoke).
  template <class Callable, class... Args>
    requires std::invocable<std::decay_t<Callable>, std::decay_t<Args>...>
  [[nodiscard]] auto Submit(Callable&& callable, Args&&... args);

 private:
  void Worker(const std::stop_token& stop_token);

  std::queue<std::move_only_function<void()>> tasks_;  // task queue

  mutable std::mutex tasks_mutex_;
  // condition_variable_any (not condition_variable) because only it can wait
  // on a std::stop_token, which is what wakes idle workers at shutdown.
  std::condition_variable_any cv_;

  // Must stay the LAST member. Members are destroyed in reverse declaration
  // order, so this makes the jthreads request stop and join while the queue,
  // mutex and condition variable they use are still alive.
  std::vector<std::jthread> workers_;
};

// Constructor
//
// - Starts pool_size worker threads.
// - Each worker runs the internal Worker() loop.
// - std::jthread passes each worker its own stop_token because the lambda
//   accepts one.
// - Throwing here would likely mean thread creation failure.
inline ThreadPool::ThreadPool(const std::size_t pool_size) {
  workers_.reserve(pool_size);
  for (std::size_t index = 0; index < pool_size; ++index) {
    workers_.emplace_back(
        [this](const std::stop_token& stop_token) { Worker(stop_token); });
  }
}

// Worker
//
// - Sleeps until a task is queued or stop is requested.
// - The stop-aware wait returns false only when stop was requested AND the
//   queue is empty, so tasks queued before destruction are still drained.
// - Stop is cooperative: it wakes idle workers but never interrupts a running
//   task, so destruction waits for in-flight tasks to finish.
// - Exception isolation per task: a single bad task never kills the thread.
inline void ThreadPool::Worker(const std::stop_token& stop_token) {
  while (true) {
    std::move_only_function<void()> task;

    {
      std::unique_lock lock(tasks_mutex_);
      if (!cv_.wait(lock, stop_token, [this] { return !tasks_.empty(); })) {
        return;
      }

      task = std::move(tasks_.front());
      tasks_.pop();
    }

    task();  // Execute task; exceptions propagate to associated future
  }
}

// Submit(callable, args...)
//
// - Packages the callable + args into a std::packaged_task.
// - Pushes a void() lambda wrapper into the queue.
// - The future allows result retrieval.
// - Member functions need no special overload: std::invoke already treats a
//   pointer to member as "call it on the first argument".
template <class Callable, class... Args>
  requires std::invocable<std::decay_t<Callable>, std::decay_t<Args>...>
auto ThreadPool::Submit(Callable&& callable, Args&&... args) {
  // Decayed types mirror what the task actually does: it invokes its own
  // stored copies as rvalues, so that is the form the constraint must check.
  using ReturnType =
      std::invoke_result_t<std::decay_t<Callable>, std::decay_t<Args>...>;

  // packaged_task links execution to the future and is move-only, which
  // std::move_only_function can hold directly; no shared ownership is needed.
  //
  // The invoker is a named lambda rather than an inline initializer: a lambda
  // with no "()" written directly after "task(" can be mis-parsed as a function
  // declarator by some clang versions (most vexing parse).
  auto invoker = [callable = std::forward<Callable>(callable),
                  ... args = std::forward<Args>(args)] mutable {
    return std::invoke(std::move(callable), std::move(args)...);
  };
  std::packaged_task<ReturnType()> task(std::move(invoker));

  auto result = task.get_future();

  {
    const std::lock_guard lock(tasks_mutex_);
    tasks_.emplace(std::move(task));  // Exceptions are stored in the future.
  }

  cv_.notify_one();
  return result;
}

}  // namespace amitgdev

#endif  // AMITGDEV_THREAD_POOL_HPP_