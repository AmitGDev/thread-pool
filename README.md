# ThreadPool

A small, header-only C++23 thread pool with a single `Submit` entry point that returns a `std::future` for the result.

The repository contains:

| File | Purpose |
|------|---------|
| `src/ThreadPool.hpp` | The thread pool (header-only) |
| `src/main.cpp` | Demo showing the three kinds of callables the pool accepts |

## Features

- Fixed number of worker threads, started in the constructor.
- One `Submit(callable, args...)` call for lambdas, functors, free functions and pointers to member functions.
- Results and exceptions are delivered through the returned `std::future`.
- Move-only callables and arguments are supported.
- Orderly shutdown: the destructor lets workers finish every task already queued, then joins them.
- Non-copyable and non-movable, so a task can never outlive the pool that owns its workers.
- No explicit stop flag: shutdown uses `std::stop_token`, which wakes idle workers directly.

## Requirements

- A C++23 compiler and standard library. The pool itself needs `std::move_only_function` and `std::stop_token` (GCC 12 or newer with `-std=c++23`). The demo also uses `<print>`, which needs GCC 14 or newer (or a recent MSVC).
- A threading runtime (`-pthread` on Linux).

## Build and run the demo

```sh
g++ -std=c++23 -pthread -I src src/main.cpp -o thread-pool-demo
./thread-pool-demo
```

To use the pool in your own project, copy `ThreadPool.hpp` and include it. There is nothing to link.

## Usage

```cpp
#include "ThreadPool.hpp"

ThreadPool pool(4);

// Lambda
auto sum = pool.Submit([](int a, int b) { return a + b; }, 2, 3);

// Free function
auto square = pool.Submit(Square, 7);

// Member function: the object (or a pointer to it) is the first argument.
auto size = pool.Submit(&Widget::Size, &widget);

int total = sum.get() + square.get();  // blocks until each task is done
```

`Submit` is `[[nodiscard]]`: the future is the only way to observe completion, the result, or a thrown exception.

### Argument semantics

Arguments are decayed and stored inside the task, then passed to the callable as rvalues. This matches `std::thread` and `std::async`:

- Lvalues are copied. To share an object with the task, pass `std::ref(obj)` (and keep `obj` alive until the task finishes).
- Rvalues are moved, so `std::unique_ptr` and other move-only types work.
- A function taking `T&` therefore needs `std::ref`, otherwise the constraint on `Submit` rejects the call at compile time rather than failing inside the task.

### Member functions

No dedicated overload exists. `std::invoke` already treats `&Class::Method` as "call this on the first argument", so passing a pointer to member followed by the object works through the same `Submit`. Pass `this` or `&object` to operate on the original; passing the object itself operates on a copy.

### Errors

An exception thrown by a task is captured by `std::packaged_task` and rethrown from `future.get()`. It never reaches the worker thread, so one failing task cannot take a worker down. If you never call `get()`, the exception is silently discarded.

## Lifetime and shutdown

The destructor is defaulted. Destroying the `std::jthread` workers requests stop, which wakes any idle worker, and then joins them. Workers exit only when stop was requested **and** the queue is empty, so every task submitted before destruction begins still runs. Destruction therefore blocks until the queue drains.

Stop is cooperative: it wakes idle workers but never interrupts a running task, so a long-running task delays destruction until it finishes.

Things to keep in mind:

- Do not call `Submit` from another thread while the pool is being destroyed. That is a race on the pool's own lifetime, not something the pool can guard against.
- A pool constructed with `pool_size == 0` has no workers, so submitted tasks are never run and waiting on their futures blocks forever.
- Tasks that block indefinitely will block destruction for the same reason.

## Design notes

- **One queue, one mutex, one condition variable.** The simplest structure that is correct. Workers wait with a predicate (`!tasks_.empty()`) together with a stop token, which handles spurious wakeups and the lost-wakeup race without extra code.
- **`std::move_only_function<void()>` as the queue element.** Tasks are `std::packaged_task` objects, which are move-only, so `std::function` (which requires copyable targets) cannot hold them. A move-only function holds them directly, with no shared ownership or extra allocation.
- **Type erasure to `void()`.** Result types differ per task, but the queue must be homogeneous. The result travels through the future, so the queue element needs no return value.
- **`std::jthread` plus `std::stop_token`.** Shutdown needs to wake workers that are blocked on an empty queue. The stop-aware wait on `std::condition_variable_any` does that, checking stop and the queue under the lock, so a wakeup cannot be lost. `condition_variable_any` is used only because plain `condition_variable` cannot wait on a stop token.
- **`workers_` is the last member.** Members are destroyed in reverse order of declaration. Declaring the threads last means they are joined first, while the queue, mutex and condition variable are still alive, and it also means the threads start only after those members are constructed. Do not reorder it.

## Demo walkthrough

`main.cpp` creates a pool of **4** workers and submits **8** tasks. Task *N* sleeps for *N* seconds, so its duration equals its id. The demo runs the same workload three times, once per callable kind:

1. **Callable object** - a lambda capturing the task index.
2. **Function** - a free function taking the index as an argument.
3. **Member function** - a method on `MyClass`, submitted with `this`.

Each task logs a line when it starts and stops, prefixed with the number of seconds since the test began. Output is written through `std::osyncstream`, so lines from different threads never interleave mid-line.

### What to expect

With 4 workers, tasks 1-4 start immediately and tasks 5-8 start as workers free up. An illustrative timeline (exact ordering of lines within the same second varies between runs, and timestamps can be off by one second because they come from whole-second clock readings):

```
0  task 1 started
0  task 2 started
0  task 3 started
0  task 4 started
1  task 1 stopped
1  task 5 started
2  task 2 stopped
2  task 6 started
3  task 3 stopped
3  task 7 started
4  task 4 stopped
4  task 8 started
6  task 5 stopped
8  task 6 stopped
10 task 7 stopped
12 task 8 stopped
```

Each of the three tests therefore takes about 12 seconds, and the whole demo about 36.

## License

MIT. See the header comment in each source file.
