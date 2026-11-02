// Minimal stand-in for MSVC's <ppltasks.h>, only what VSTGUI's win32taskexecutor.cpp uses:
// concurrency::task<void> runs a callable asynchronously; then() runs the next one after it.
// Built on std::promise (not std::async) so that dropping a task never blocks.
#pragma once
#include <future>
#include <memory>
#include <thread>
#include <type_traits>
#include <utility>

namespace concurrency {
template <class T> class task;
template <> class task<void> {
public:
  task() = default;
  template <class F> explicit task(F&& f) { start(std::shared_future<void>(), std::forward<F>(f)); }
  template <class F> task then(F&& f) const {
    task t;
    t.start(fut, std::forward<F>(f));
    return t;
  }

private:
  std::shared_future<void> fut;
  template <class F> void start(std::shared_future<void> prev, F&& f) {
    auto p = std::make_shared<std::promise<void>>();
    fut = p->get_future().share();
    std::thread([p, prev, g = std::decay_t<F>(std::forward<F>(f))]() mutable {
      if (prev.valid()) prev.wait();
      g();
      p->set_value();
    }).detach();
  }
};
} // namespace concurrency
