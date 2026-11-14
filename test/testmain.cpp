#include <actor/runtime.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <future>
#include <memory>
#include <mutex>
#include <numeric>
#include <string>
#include <thread>
#include <vector>

namespace {

using namespace libapp::actor;

std::mutex g_log_mu;

inline void log_line(const char* text) {
    std::lock_guard<std::mutex> lock(g_log_mu);
    std::printf("%s\n", text);
    std::fflush(stdout);
}

template <typename... Args>
void log_line(const char* format, Args... args) {
    std::lock_guard<std::mutex> lock(g_log_mu);
    std::printf(format, args...);
    std::printf("\n");
    std::fflush(stdout);
}

enum : MsgType {
    msg_job = 1,
    msg_probe_request = 2,
    msg_probe_report = 3,
    msg_sum_request = 4,
    msg_sum_reply = 5,
    msg_bootstrap = 6,
    msg_coordinator_report = 7,
};

struct ProbeJob {
    int id;
    int sleep_ms;
};

struct ProbeReport {
    std::size_t processed;
    std::size_t violations;
    int max_active;
    std::size_t thread_count;
};

struct SumRequest {
    std::vector<int> values;
};

struct SumReply {
    long long sum;
    ActorId actor;
};

struct CoordinatorReport {
    std::size_t probes;
    std::size_t jobs;
    std::size_t violations;
    int max_active;
};

std::atomic<int> g_probe_alive{0};

/// 统计同一 actor 同时运行了多少个自己的 handler（必须始终为 1），
/// 并记录它跑过哪些 worker 线程（可能不止一个，说明发生了迁移）。
class ProbeActor final : public Actor {
public:
    explicit ProbeActor(Runtime& runtime) : Actor(runtime) {
        g_probe_alive.fetch_add(1, std::memory_order_relaxed);
    }

    ~ProbeActor() override {
        g_probe_alive.fetch_sub(1, std::memory_order_relaxed);
    }

private:
    void on_message(const Message& message) override {
        const int previous = _active.fetch_add(1, std::memory_order_acq_rel);
        ActiveGuard guard{_active};

        if (previous != 0) {
            _violations.fetch_add(1, std::memory_order_relaxed);
            log_line("!!! actor %llu ran concurrently (active=%d)",
                     static_cast<unsigned long long>(id()), previous + 1);
        }

        const int current = previous + 1;
        int observed = _max_active.load(std::memory_order_relaxed);
        while (current > observed &&
               !_max_active.compare_exchange_weak(observed, current, std::memory_order_relaxed)) {
        }
        record_thread(std::this_thread::get_id());

        if (message.type == msg_job) {
            _processed.fetch_add(1, std::memory_order_relaxed);
            const auto* job = message.payload_as<ProbeJob>();
            if (job != nullptr && job->sleep_ms > 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(job->sleep_ms));
            }
        } else if (message.type == msg_probe_request) {
            const std::size_t processed = _processed.load(std::memory_order_relaxed);
            const std::size_t violations = _violations.load(std::memory_order_relaxed);
            const int max_active = _max_active.load(std::memory_order_relaxed);
            reply(message, msg_probe_report,
                  make_payload<ProbeReport>(processed, violations, max_active, thread_count()));
        }
    }

    void record_thread(std::thread::id thread_id) {
        std::lock_guard<std::mutex> lock(_thread_mu);
        if (std::find(_threads.begin(), _threads.end(), thread_id) == _threads.end()) {
            _threads.push_back(thread_id);
        }
    }

    std::size_t thread_count() {
        std::lock_guard<std::mutex> lock(_thread_mu);
        return _threads.size();
    }

    struct ActiveGuard {
        std::atomic<int>& counter;
        ~ActiveGuard() { counter.fetch_sub(1, std::memory_order_acq_rel); }
    };

    std::atomic<int> _active{0};
    std::atomic<int> _max_active{0};
    std::atomic<std::size_t> _processed{0};
    std::atomic<std::size_t> _violations{0};

    std::mutex _thread_mu;
    std::vector<std::thread::id> _threads;
};

class CalculatorActor final : public Actor {
public:
    explicit CalculatorActor(Runtime& runtime) : Actor(runtime) {}

private:
    void on_message(const Message& message) override {
        if (message.type != msg_sum_request) {
            return;
        }
        const auto* request = message.payload_as<SumRequest>();
        long long sum = 0;
        if (request != nullptr) {
            sum = std::accumulate(request->values.begin(), request->values.end(), 0LL);
        }
        reply(message, msg_sum_reply, make_payload<SumReply>(sum, id()));
    }
};

/// 用来验证 shutdown 会排空 mailbox 中已入队的消息。
class DrainActor final : public Actor {
public:
    DrainActor(Runtime& runtime, std::shared_ptr<std::atomic<int>> processed)
        : Actor(runtime), _processed(std::move(processed)) {}

private:
    void on_message(const Message& message) override {
        if (message.type == msg_job) {
            _processed->fetch_add(1, std::memory_order_relaxed);
        }
    }

    std::shared_ptr<std::atomic<int>> _processed;
};

/// 运行期动态创建子 actor，用 request/reply 与它们通信，然后
/// 停止子 actor 和自己。覆盖动态 spawn 与 actor 侧回调。
class CoordinatorActor final : public Actor {
public:
    explicit CoordinatorActor(Runtime& runtime) : Actor(runtime) {}

private:
    void on_message(const Message& message) override {
        if (message.type != msg_bootstrap) {
            return;
        }

        _bootstrap = message;
        constexpr std::size_t kChildren = 2;
        constexpr std::size_t kJobsPerChild = 8;

        for (std::size_t i = 0; i < kChildren; ++i) {
            const ActorId child = runtime().spawn<ProbeActor>();
            _children.push_back(child);
            for (std::size_t j = 0; j < kJobsPerChild; ++j) {
                send(child, msg_job, make_payload<ProbeJob>(static_cast<int>(j), 1));
            }

            request(child, msg_probe_request, {},
                    [this](const Message& reply_message) {
                        const auto* report = reply_message.payload_as<ProbeReport>();
                        if (report != nullptr) {
                            _jobs += report->processed;
                            _violations += report->violations;
                            _max_active = std::max(_max_active, report->max_active);
                        }

                        ++_reports;
                        log_line("coordinator: child report %zu/%zu (processed=%zu, violations=%zu, "
                                 "max_active=%d, threads=%zu)",
                                 _reports, _children.size(),
                                 report != nullptr ? report->processed : 0,
                                 report != nullptr ? report->violations : 0,
                                 report != nullptr ? report->max_active : 0,
                                 report != nullptr ? report->thread_count : 0);

                        if (_reports == _children.size()) {
                            reply(_bootstrap, msg_coordinator_report,
                                  make_payload<CoordinatorReport>(_children.size(), _jobs,
                                                                  _violations, _max_active));
                            for (ActorId id : _children) {
                                runtime().stop_actor(id);
                            }
                            log_line("coordinator: stopping %zu children and itself", _children.size());
                            stop();
                        }
                    });
        }

        log_line("coordinator: spawned %zu probe actors dynamically", _children.size());
    }

    Message _bootstrap;
    std::vector<ActorId> _children;
    std::size_t _reports{0};
    std::size_t _jobs{0};
    std::size_t _violations{0};
    int _max_active{0};
};

}  // 命名空间

int main() {
    constexpr std::size_t kWorkers = 4;
    constexpr std::size_t kTopProbes = 3;
    constexpr int kProducers = 3;
    constexpr int kJobsPerProducer = 20;
    constexpr long long kExpectedSum = 5050;  // 1 + 2 + ... + 100

    Runtime runtime(kWorkers);
    runtime.start();
    log_line("runtime started: requested=%zu actual=%zu", kWorkers, runtime.worker_count());

    std::vector<ActorId> probes;
    probes.reserve(kTopProbes);
    for (std::size_t i = 0; i < kTopProbes; ++i) {
        probes.push_back(runtime.spawn<ProbeActor>());
    }
    const ActorId calculator = runtime.spawn<CalculatorActor>();
    log_line("spawned %zu probe actors and calculator %llu", probes.size(),
             static_cast<unsigned long long>(calculator));

    // 三个独立的生产者线程同时向同一批 actor 发消息。每次 send
    // 都是线程安全的；目标 actor 的 strand 保证投递串行。
    std::vector<std::thread> producers;
    producers.reserve(kProducers);
    for (int producer = 0; producer < kProducers; ++producer) {
        producers.emplace_back([&runtime, &probes, producer] {
            for (int job = 0; job < kJobsPerProducer; ++job) {
                const std::size_t index =
                    static_cast<std::size_t>(producer + job) % probes.size();
                runtime.send(probes[index], msg_job, make_payload<ProbeJob>(job, 2));
            }
        });
    }
    for (auto& producer : producers) {
        producer.join();
    }

    std::vector<int> values(100);
    std::iota(values.begin(), values.end(), 1);
    auto sum_future = runtime.call(calculator, msg_sum_request,
                                   make_payload<SumRequest>(values));
    Message sum_message = sum_future.get();
    const auto* sum_reply = sum_message.payload_as<SumReply>();
    const bool calculator_ok = sum_reply != nullptr && sum_reply->sum == kExpectedSum;
    log_line("call calculator -> sum=%lld (expected=%lld), reply session=%llu",
             sum_reply != nullptr ? sum_reply->sum : -1LL, kExpectedSum,
             static_cast<unsigned long long>(sum_message.session));

    const ActorId coordinator = runtime.spawn<CoordinatorActor>();
    auto coordinator_future = runtime.call(coordinator, msg_bootstrap, {});
    Message coordinator_message = coordinator_future.get();
    const auto* coordinator_report = coordinator_message.payload_as<CoordinatorReport>();
    const bool coordinator_ok =
        coordinator_report != nullptr && coordinator_report->probes == 2 &&
        coordinator_report->jobs == 16 && coordinator_report->violations == 0 &&
        coordinator_report->max_active == 1;
    if (coordinator_report != nullptr) {
        log_line("coordinator -> probes=%zu jobs=%zu violations=%zu max_active=%d",
                 coordinator_report->probes, coordinator_report->jobs,
                 coordinator_report->violations, coordinator_report->max_active);
    } else {
        log_line("coordinator returned no report");
    }

    std::vector<std::future<Message>> report_futures;
    report_futures.reserve(probes.size());
    for (ActorId id : probes) {
        report_futures.push_back(runtime.call(id, msg_probe_request));
    }

    std::size_t total_processed = 0;
    std::size_t total_violations = 0;
    std::size_t migrated_actors = 0;
    int top_max_active = 0;
    for (std::size_t i = 0; i < report_futures.size(); ++i) {
        Message report_message = report_futures[i].get();
        const auto* report = report_message.payload_as<ProbeReport>();
        if (report == nullptr) {
            log_line("probe %llu returned no report",
                     static_cast<unsigned long long>(probes[i]));
            continue;
        }
        total_processed += report->processed;
        total_violations += report->violations;
        top_max_active = std::max(top_max_active, report->max_active);
        if (report->thread_count > 1) {
            ++migrated_actors;
        }
        log_line("probe %llu: processed=%zu violations=%zu max_active=%d threads=%zu",
                 static_cast<unsigned long long>(probes[i]), report->processed,
                 report->violations, report->max_active, report->thread_count);
    }

    std::vector<std::shared_future<void>> stop_futures;
    stop_futures.reserve(probes.size());
    for (ActorId id : probes) {
        stop_futures.push_back(runtime.stop_actor(id));
    }
    for (auto& future : stop_futures) {
        if (future.valid()) {
            future.wait();
        }
    }
    log_line("stopped %zu top-level probes, actors still alive=%zu", probes.size(),
             runtime.actor_count());

    // 先排入任务，然后不显式 stop 直接停机：shutdown 必须
    // 在销毁 actor 之前排空 mailbox。
    constexpr int kDrainJobs = 25;
    auto drained = std::make_shared<std::atomic<int>>(0);
    const ActorId drain_actor = runtime.spawn<DrainActor>(drained);
    for (int i = 0; i < kDrainJobs; ++i) {
        runtime.send(drain_actor, msg_job, make_payload<ProbeJob>(i, 0));
    }
    log_line("queued %d jobs to drain actor, then shutting down", kDrainJobs);

    runtime.shutdown();

    log_line("runtime shut down: actors=%zu probe_destructed=%d drained_jobs=%d",
             runtime.actor_count(), g_probe_alive.load(std::memory_order_relaxed),
             drained->load(std::memory_order_relaxed));
    log_line("cross-thread scheduling observed on %zu/%zu top-level probes", migrated_actors,
             probes.size());

    bool ok = true;
    auto check = [&ok](bool condition, const char* name) {
        if (condition) {
            log_line("PASS: %s", name);
        } else {
            log_line("FAIL: %s", name);
            ok = false;
        }
    };

    check(runtime.worker_count() == kWorkers, "requested worker count");
    check(calculator_ok, "external call/reply");
    check(total_processed == static_cast<std::size_t>(kProducers * kJobsPerProducer),
          "all probe jobs processed");
    check(total_violations == 0, "no concurrent execution inside a probe actor");
    check(top_max_active == 1, "probe actor max concurrency is 1");
    check(coordinator_ok, "dynamic spawn + actor-side request/reply");
    check(drained->load(std::memory_order_relaxed) == kDrainJobs,
          "shutdown drains queued messages");
    check(runtime.actor_count() == 0, "all actors removed after shutdown");
    check(g_probe_alive.load(std::memory_order_relaxed) == 0, "all probe actors destroyed");

    log_line(ok ? "RESULT: PASS" : "RESULT: FAIL");
    return ok ? 0 : 1;
}
