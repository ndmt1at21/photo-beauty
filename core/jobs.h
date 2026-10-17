#pragma once
#include <atomic>
#include <exception>
#include <functional>
#include <memory>

namespace pb {

// Highest first. Workers always take the most urgent queued job.
enum class Priority { Interactive, Visible, Background };

// A job polls its token and stops early once it is set.
using CancelToken = std::shared_ptr<std::atomic<bool>>;
inline CancelToken make_token() { return std::make_shared<std::atomic<bool>>(false); }
inline bool cancelled(const CancelToken& t) { return t && t->load(std::memory_order_relaxed); }

// Thrown by work that noticed its token was set.
struct Cancelled : std::exception {
  const char* what() const noexcept override { return "cancelled"; }
};

// Queues fn on the global pool (one worker per hardware thread). Never blocks.
void submit(Priority, std::function<void()> fn);

// Runs fn(begin, end) over chunks of [0, n) on the pool and returns when all are done.
// The caller works on chunks too, so calling it from inside a job cannot deadlock.
void parallel_for(int n, const std::function<void(int begin, int end)>& fn);

// Runs fn on n threads of its own (fewer if the system gives no more, at least the caller) and returns when
// all are done, rethrowing the first error: work that would hold pool threads for long, as an export's
// photos, each fn taking the next item itself. Background ones run below the UI's threads for the OS; the
// parallel_for() calls inside fn queue at p.
void on_threads(int n, Priority p, const std::function<void()>& fn);
// Puts the calling thread below the UI's for the OS, as on_threads()' Background ones.
void lower_priority();

// Newest wins: at most one job runs at a time. Submitting cancels the running job and
// replaces the queued one, so a fast slider only renders its latest value.
class Latest {
 public:
  explicit Latest(Priority p = Priority::Interactive);
  void submit(std::function<void(const CancelToken&)> fn);

 private:
  struct State;
  std::shared_ptr<State> s_;
};

}  // namespace pb
