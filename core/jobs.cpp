#include "jobs.h"

#include <algorithm>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <mutex>
#include <system_error>
#include <thread>
#include <vector>

#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#elif defined(__APPLE__)
#include <pthread.h>
#else
#include <sys/resource.h>
#endif

namespace pb {
namespace {

thread_local Priority tl_priority = Priority::Interactive;  // of the job running on this thread

// ponytail: one mutex for all queues; jobs are coarse (chunks claim work via atomics), so
// contention stays low. Per-priority QoS (macOS QOS_CLASS_*, Windows EcoQoS) comes with the UI.
class Pool {
 public:
  Pool() {
    const unsigned n = std::max(2u, std::thread::hardware_concurrency());
    for (unsigned i = 0; i < n; i++) threads_.emplace_back([this] { run(); });
  }
  ~Pool() {
    {
      std::lock_guard l(m_);
      stop_ = true;
    }
    cv_.notify_all();
    for (auto& t : threads_) t.join();
  }
  void push(Priority p, std::function<void()> fn) {
    {
      std::lock_guard l(m_);
      q_[int(p)].push_back(std::move(fn));
    }
    cv_.notify_one();
  }
  int size() const { return int(threads_.size()); }

 private:
  void run() {
    for (;;) {
      std::function<void()> fn;
      {
        std::unique_lock l(m_);
        int p = 0;
        cv_.wait(l, [&] {
          for (p = 0; p < 3; p++)
            if (!q_[p].empty()) return true;
          return stop_;
        });
        if (stop_) return;
        fn = std::move(q_[p].front());
        q_[p].pop_front();
        tl_priority = Priority(p);
      }
      fn();
    }
  }

  std::mutex m_;
  std::condition_variable cv_;
  std::deque<std::function<void()>> q_[3];
  bool stop_ = false;
  std::vector<std::thread> threads_;
};

Pool& pool() {
  static Pool p;
  return p;
}

void run_guarded(const std::function<void()>& fn) {
  try {
    fn();
  } catch (const Cancelled&) {
  } catch (const std::exception& e) {
    std::fprintf(stderr, "photo-beauty: job failed: %s\n", e.what());
  } catch (...) {
    std::fprintf(stderr, "photo-beauty: job failed\n");
  }
}

}  // namespace

void submit(Priority p, std::function<void()> fn) {
  pool().push(p, [fn = std::move(fn)] { run_guarded(fn); });
}

void parallel_for(int n, const std::function<void(int, int)>& fn) {
  if (n <= 0) return;
  Pool& p = pool();
  const int chunks = std::min(n, p.size() * 4);
  if (chunks == 1) return fn(0, n);

  // Shared with helpers that may start after we return; they then find no chunk left.
  struct State {
    std::atomic<int> next{0}, done{0};
    int n = 0, chunks = 0;
    const std::function<void(int, int)>* fn = nullptr;
    std::mutex m;
    std::condition_variable cv;
    std::exception_ptr error;
  };
  auto st = std::make_shared<State>();
  st->n = n;
  st->chunks = chunks;
  st->fn = &fn;
  auto work = [](State& s) {
    for (int i; (i = s.next.fetch_add(1)) < s.chunks;) {
      try {
        (*s.fn)(int(int64_t(s.n) * i / s.chunks), int(int64_t(s.n) * (i + 1) / s.chunks));
      } catch (...) {
        std::lock_guard l(s.m);
        if (!s.error) s.error = std::current_exception();
      }
      if (s.done.fetch_add(1) + 1 == s.chunks) {
        std::lock_guard l(s.m);
        s.cv.notify_all();
      }
    }
  };
  for (int i = std::min(chunks - 1, p.size()); i > 0; i--) p.push(tl_priority, [st, work] { work(*st); });
  work(*st);
  std::unique_lock l(st->m);
  st->cv.wait(l, [&] { return st->done.load() == chunks; });
  if (st->error) std::rethrow_exception(st->error);
}

void lower_priority() {
#ifdef _WIN32
  SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
#elif defined(__APPLE__)
  pthread_set_qos_class_self_np(QOS_CLASS_UTILITY, 0);
#else
  setpriority(PRIO_PROCESS, 0, 10);  // Linux: this thread's nice value
#endif
}

void on_threads(int n, Priority p, const std::function<void()>& fn) {
  pool();  // made first, else its threads would take the lower priority of the first job to use it
  std::mutex m;
  std::exception_ptr error;
  auto run = [&] {
    try {
      fn();
    } catch (...) {
      std::lock_guard l(m);
      if (!error) error = std::current_exception();
    }
  };
  std::vector<std::thread> threads;
  for (int i = 0; i < n; i++) {
    try {
      threads.emplace_back([&, p] {
        tl_priority = p;
        if (p == Priority::Background) lower_priority();
        run();
      });
    } catch (const std::system_error&) {
      break;
    }
  }
  if (threads.empty()) run();
  for (std::thread& t : threads) t.join();
  if (error) std::rethrow_exception(error);
}

struct Latest::State {
  Priority priority = Priority::Interactive;
  std::mutex m;
  std::function<void(const CancelToken&)> pending;
  CancelToken running;
  bool active = false;  // a drain task is queued or running
};

Latest::Latest(Priority p) : s_(std::make_shared<State>()) { s_->priority = p; }

void Latest::submit(std::function<void(const CancelToken&)> fn) {
  std::lock_guard l(s_->m);
  if (s_->running) s_->running->store(true);
  s_->pending = std::move(fn);
  if (s_->active) return;
  s_->active = true;
  pool().push(s_->priority, [s = s_] {
    for (;;) {
      std::function<void(const CancelToken&)> job;
      CancelToken token;
      {
        std::lock_guard l(s->m);
        if (!s->pending) {
          s->active = false;
          s->running.reset();
          return;
        }
        job = std::move(s->pending);
        s->pending = nullptr;
        token = s->running = make_token();
      }
      run_guarded([&] { job(token); });
    }
  });
}

}  // namespace pb
