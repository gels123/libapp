#pragma once

#if !defined(ASIO_STANDALONE)
#define ASIO_STANDALONE
#endif

#include <asio.hpp>

#include "message.hpp"

#include <atomic>
#include <cstddef>
#include <cstdio>
#include <exception>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace libapp::actor {

class Runtime;

/// Actor 的每条消息都在自己的 asio strand 上处理。strand 不会并发调用两个 handler，因此 on_message 内部访问的状态无需加锁。
/// strand 不绑定到某个具体 worker 线程，actor 可能在任意 worker 线程上被处理。两条消息之间，actor 可能被另一个 worker 接手。
class Actor {
public:
    Actor() = default;
    Actor(const Actor&) = delete;
    Actor& operator=(const Actor&) = delete;
    virtual ~Actor() = default;

    ActorId id() const noexcept { return _id; }

    Runtime& runtime() noexcept { return *_runtime; }
    const Runtime& runtime() const noexcept { return *_runtime; }

    /// 异步发送，发完即返回。线程安全，可从任意线程调用。
    bool send(ActorId target, MsgType type, std::shared_ptr<void> payload = {});
    bool send_text(ActorId target, MsgType type, std::string text);

    /// 回复请求，携带请求的 session id。线程安全。
    bool reply(const Message& request, MsgType type, std::shared_ptr<void> payload = {});

    /// 非阻塞请求/回复。收到回复后，callback 会在本 actor 的 strand 上执行，与普通消息串行。
    bool request(ActorId target, MsgType type, std::shared_ptr<void> payload, std::function<void(const Message&)> callback);

    /// 停止本 actor。先排空已入队的消息，然后执行 on_stop，最后移除该 actor。线程安全。
    void stop();

protected:
    explicit Actor(Runtime& runtime) noexcept : _runtime(&runtime) {}

    /// 被spawn接管之后在 actor 的 strand 上调用一次。
    virtual void on_start() {}

    /// 在 actor 的 strand 上调用，绝不会与自身并发。
    virtual void on_message(const Message& message) = 0;

    /// 已入队消息全部排空后，在 actor 的 strand 上调用。
    virtual void on_stop() {}

    /// on_message 抛出异常时调用，默认实现输出到 stderr。
    virtual void on_error(const Message& message, std::string_view error) {
        std::fprintf(stderr,
                     "[actor %llu] exception while handling message type=%u: %.*s\n",
                     static_cast<unsigned long long>(_id),
                     static_cast<unsigned>(message.type),
                     static_cast<int>(error.size()), error.data());
    }

private:
    friend class Runtime;

    Runtime* _runtime;
    ActorId _id{kSystemActor};
};

/// 运行时：持有 worker 线程池和所有 actor。
/// 设计：
///   * 一个共享的 asio::io_context，由 N 个 worker 线程运行；
///   * 每个 actor 一条 asio::strand，作为该 actor 的 mailbox；
///   * send() 把 handler 投递到目标 actor 的 strand，因此消息处理是串行的，同时 actor 可以在 worker 之间迁移；
///   * actor 状态只在 strand handler 中访问，因此无需加锁。
class Runtime {
public:
    using Strand = asio::strand<asio::io_context::executor_type>;

    /// worker_threads == 0 表示使用 std::thread::hardware_concurrency()。
    explicit Runtime(std::size_t thread_count = 0) : _thread_count(thread_count) {}
    Runtime(const Runtime&) = delete;
    Runtime& operator=(const Runtime&) = delete;
    ~Runtime();

    /// 启动 worker 线程。必须在 spawn/send/call 之前调用, 每个 Runtime 实例只能调用一次。
    void start();

    /// 停止接收新任务，排空所有actor的mailbox后，调用 on_stop，然后 join 所有 worker。禁止在 worker 线程中调用。
    void shutdown();

    bool running() const noexcept { return _running.load(std::memory_order_acquire); }
    std::size_t worker_count() const noexcept { return _worker_count; }

    /// 接管 actor 的所有权并分配 id。
    ActorId spawn(std::unique_ptr<Actor> actor);

    template <typename ActorT, typename... Args>
    ActorId spawn(Args&&... args) {
        static_assert(std::is_base_of_v<Actor, ActorT>, "ActorT must derive from libapp::actor::Actor");
        return spawn(std::make_unique<ActorT>(*this, std::forward<Args>(args)...));
    }

    bool send(ActorId target, MsgType type, std::shared_ptr<void> payload = {});
    bool send_text(ActorId target, MsgType type, std::string text);

    /// 阻塞式请求：返回一个由目标 actor 的 reply() 完成的 future。面向线程池之外的调用方；在 actor handler 内调用 .get(), 可能占住一个 worker。
    std::future<Message> call(ActorId target, MsgType type, std::shared_ptr<void> payload = {});

    /// 请求 actor 停止，返回的 future 在 on_stop 完成后就绪。future 无效表示“不存在该 actor”。
    std::shared_future<void> stop_actor(ActorId id);

    bool is_alive(ActorId id) const;
    std::size_t actor_count() const;

private:
    friend class Actor;

    struct PendingReply {
        ActorId requester{kSystemActor};
        std::shared_ptr<std::promise<Message>> promise;
        std::function<void(const Message&)> callback;
    };

    struct Slot {
        Slot(ActorId id_, Strand strand_) : id(id_), strand(std::move(strand_)) {}

        ActorId id;
        Strand strand;
        std::shared_ptr<Actor> actor;
        std::atomic<bool> stopping{false};
        std::promise<void> stop_promise;
        std::shared_future<void> stopped{stop_promise.get_future().share()};
    };

    bool send_message(ActorId source, ActorId target, MsgType type,
                      std::shared_ptr<void> payload, SessionId session,
                      bool is_reply = false);
    bool request_from(ActorId requester, ActorId target, MsgType type,
                      std::shared_ptr<void> payload,
                      std::function<void(const Message&)> callback);
    bool deliver(Message message);
    bool consume_reply(Message& message);
    void finalize_stop(const std::shared_ptr<Slot>& slot);
    void fail_pending(const std::exception_ptr& error);

    mutable std::mutex _actors_mu;
    std::unordered_map<ActorId, std::shared_ptr<Slot>> _actors;

    mutable std::mutex _pending_mu;
    std::unordered_map<SessionId, PendingReply> _pending;

    asio::io_context _io;
    std::optional<asio::executor_work_guard<asio::io_context::executor_type>> _work_guard;
    std::vector<std::thread> _threads;
    std::size_t _thread_count{0};
    std::size_t _worker_count{0};

    std::atomic<bool> _started{false};
    std::atomic<bool> _running{false};
    std::atomic<ActorId> _next_actor_id{1};
    std::atomic<SessionId> _next_session{1};

    std::mutex _lifecycle_mu;
    bool _shut_down{false};
};

// ---------------------------------------------------------------------------
// Actor 的类外定义（此时 Runtime 已经完整）。
// ---------------------------------------------------------------------------

inline bool Actor::send(ActorId target, MsgType type, std::shared_ptr<void> payload) {
    return _runtime->send_message(_id, target, type, std::move(payload), kNoSession);
}

inline bool Actor::send_text(ActorId target, MsgType type, std::string text) {
    return send(target, type, make_text(std::move(text)));
}

inline bool Actor::reply(const Message& request, MsgType type, std::shared_ptr<void> payload) {
    return _runtime->send_message(_id, request.source, type, std::move(payload), request.session,
                                  true);
}

inline bool Actor::request(ActorId target, MsgType type, std::shared_ptr<void> payload,
                          std::function<void(const Message&)> callback) {
    return _runtime->request_from(_id, target, type, std::move(payload), std::move(callback));
}

inline void Actor::stop() {
    _runtime->stop_actor(_id);
}

// ---------------------------------------------------------------------------
// Runtime 的定义。
// ---------------------------------------------------------------------------

inline Runtime::~Runtime() {
    try {
        shutdown();
    } catch (...) {
        // 析构函数不能抛出异常。
    }
}

inline void Runtime::start() {
    bool expected = false;
    if (!_started.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        return;
    }
    _running.store(true, std::memory_order_release);
    _work_guard.emplace(asio::make_work_guard(_io));

    std::size_t workers = _thread_count;
    if (workers == 0) {
        workers = std::thread::hardware_concurrency();
        if (workers == 0) {
            workers = 1;
        }
    }
    _worker_count = workers;

    _threads.reserve(workers);
    try {
        for (std::size_t i = 0; i < workers; ++i) {
            _threads.emplace_back([this] { _io.run(); });
        }
    } catch (...) {
        _running.store(false, std::memory_order_release);
        if (_work_guard) {
            _work_guard.reset();
        }
        for (auto& thread : _threads) {
            if (thread.joinable()) {
                thread.join();
            }
        }
        _threads.clear();
        _worker_count = 0;
        _started.store(false, std::memory_order_release);
        throw;
    }
}

inline void Runtime::shutdown() {
    if (!_started.load(std::memory_order_acquire)) {
        return;
    }
    if (_io.get_executor().running_in_this_thread()) {
        throw std::logic_error("Runtime::shutdown() must not be called from a worker thread");
    }

    std::lock_guard lifecycle_lock(_lifecycle_mu);
    if (_shut_down) {
        return;
    }
    _shut_down = true;

    // 从这里开始拒绝新的 send/spawn。已入队的 handler
    // 仍排在每个 actor 的停止标记之前，会被排空。
    _running.store(false, std::memory_order_release);

    std::vector<std::shared_future<void>> waiters;
    {
        std::lock_guard actors_lock(_actors_mu);
        waiters.reserve(_actors.size());
        for (auto& entry : _actors) {
            const auto& slot = entry.second;
            if (!slot->stopping.exchange(true, std::memory_order_acq_rel)) {
                asio::post(slot->strand, [this, slot] { finalize_stop(slot); });
            }
            waiters.push_back(slot->stopped);
        }
    }
    for (auto& waiter : waiters) {
        waiter.wait();
    }

    fail_pending(std::make_exception_ptr(std::runtime_error("actor runtime has shut down")));

    if (_work_guard) {
        _work_guard.reset();
    }
    for (auto& thread : _threads) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    _threads.clear();
}

inline ActorId Runtime::spawn(std::unique_ptr<Actor> actor) {
    if (!actor) {
        throw std::invalid_argument("actor must not be null");
    }
    if (!running()) {
        throw std::runtime_error("actor runtime is not running");
    }

    const ActorId id = _next_actor_id.fetch_add(1, std::memory_order_relaxed);
    auto slot = std::make_shared<Slot>(id, asio::make_strand(_io));
    slot->actor = std::shared_ptr<Actor>(std::move(actor));
    slot->actor->_id = id;

    {
        std::lock_guard actors_lock(_actors_mu);
        if (!running()) {
            throw std::runtime_error("actor runtime is shutting down");
        }
        _actors.emplace(id, slot);
        try {
            asio::post(slot->strand, [slot] {
                try {
                    slot->actor->on_start();
                } catch (const std::exception& error) {
                    std::fprintf(stderr, "[actor %llu] exception in on_start: %s\n",
                                 static_cast<unsigned long long>(slot->id), error.what());
                } catch (...) {
                    std::fprintf(stderr, "[actor %llu] unknown exception in on_start\n",
                                 static_cast<unsigned long long>(slot->id));
                }
            });
        } catch (...) {
            _actors.erase(id);
            throw;
        }
    }
    return id;
}

inline bool Runtime::send(ActorId target, MsgType type, std::shared_ptr<void> payload) {
    return send_message(kSystemActor, target, type, std::move(payload), kNoSession);
}

inline bool Runtime::send_text(ActorId target, MsgType type, std::string text) {
    return send(target, type, make_text(std::move(text)));
}

inline std::future<Message> Runtime::call(ActorId target, MsgType type,
                                          std::shared_ptr<void> payload) {
    if (!running()) {
        throw std::runtime_error("actor runtime is not running");
    }

    auto promise = std::make_shared<std::promise<Message>>();
    auto future = promise->get_future();
    const SessionId session = _next_session.fetch_add(1, std::memory_order_relaxed);

    {
        std::lock_guard pending_lock(_pending_mu);
        _pending.emplace(session, PendingReply{kSystemActor, promise, {}});
    }

    bool delivered = false;
    try {
        delivered = send_message(kSystemActor, target, type, std::move(payload), session);
    } catch (...) {
        std::lock_guard pending_lock(_pending_mu);
        _pending.erase(session);
        throw;
    }

    if (!delivered) {
        {
            std::lock_guard pending_lock(_pending_mu);
            _pending.erase(session);
        }
        promise->set_exception(
            std::make_exception_ptr(std::runtime_error("target actor is not alive")));
    }
    return future;
}

inline std::shared_future<void> Runtime::stop_actor(ActorId id) {
    std::lock_guard actors_lock(_actors_mu);
    auto it = _actors.find(id);
    if (it == _actors.end()) {
        return {};
    }

    auto slot = it->second;
    if (!slot->stopping.exchange(true, std::memory_order_acq_rel)) {
        asio::post(slot->strand, [this, slot] { finalize_stop(slot); });
    }
    return slot->stopped;
}

inline bool Runtime::is_alive(ActorId id) const {
    std::lock_guard actors_lock(_actors_mu);
    auto it = _actors.find(id);
    return it != _actors.end() && !it->second->stopping.load(std::memory_order_acquire);
}

inline std::size_t Runtime::actor_count() const {
    std::lock_guard actors_lock(_actors_mu);
    return _actors.size();
}

inline bool Runtime::send_message(ActorId source, ActorId target, MsgType type,
                                  std::shared_ptr<void> payload, SessionId session,
                                  bool is_reply) {
    return deliver(Message(source, target, type, session, std::move(payload), is_reply));
}

inline bool Runtime::request_from(ActorId requester, ActorId target, MsgType type,
                                  std::shared_ptr<void> payload,
                                  std::function<void(const Message&)> callback) {
    if (!callback) {
        return send_message(requester, target, type, std::move(payload), kNoSession);
    }

    const SessionId session = _next_session.fetch_add(1, std::memory_order_relaxed);
    {
        std::lock_guard pending_lock(_pending_mu);
        _pending.emplace(session, PendingReply{requester, nullptr, std::move(callback)});
    }

    bool delivered = false;
    try {
        delivered = send_message(requester, target, type, std::move(payload), session);
    } catch (...) {
        std::lock_guard pending_lock(_pending_mu);
        _pending.erase(session);
        throw;
    }

    if (!delivered) {
        std::lock_guard pending_lock(_pending_mu);
        _pending.erase(session);
    }
    return delivered;
}

inline bool Runtime::deliver(Message message) {
    // 回复携带请求的 session。把它路由到等待中的
    // future/callback；普通请求同样带 session，必须继续
    // 投递到目标 actor。
    if (message.is_reply && message.session != kNoSession && consume_reply(message)) {
        return true;
    }

    std::shared_ptr<Slot> slot;
    {
        std::lock_guard actors_lock(_actors_mu);
        if (!_running.load(std::memory_order_acquire)) {
            return false;
        }

        auto it = _actors.find(message.target);
        if (it == _actors.end()) {
            return false;
        }
        slot = it->second;
        if (slot->stopping.load(std::memory_order_acquire)) {
            return false;  // 请求停机后，新任务会被拒绝。
        }

        // 持有 _actors_mu 投递，才能与停止标记形成严格顺序：
        // 要么本条消息排在标记之前（随后会被排空），
        // 要么先观察到 stop()，消息被拒绝。
        asio::post(slot->strand, [slot, msg = std::move(message)]() mutable {
            try {
                slot->actor->on_message(msg);
            } catch (const std::exception& error) {
                slot->actor->on_error(msg, error.what());
            } catch (...) {
                slot->actor->on_error(msg, "unknown exception");
            }
        });
    }
    return true;
}

inline bool Runtime::consume_reply(Message& message) {
    PendingReply pending;
    bool found = false;
    {
        std::lock_guard pending_lock(_pending_mu);
        auto it = _pending.find(message.session);
        if (it != _pending.end()) {
            pending = std::move(it->second);
            _pending.erase(it);
            found = true;
        }
    }
    if (!found) {
        return false;
    }

    if (pending.promise) {
        try {
            pending.promise->set_value(std::move(message));
        } catch (...) {
            // 在销毁阶段，promise 可能已经被满足。
        }
        return true;
    }
    if (!pending.callback) {
        return true;
    }

    std::shared_ptr<Slot> slot;
    {
        std::lock_guard actors_lock(_actors_mu);
        auto it = _actors.find(pending.requester);
        if (it != _actors.end() &&
            !it->second->stopping.load(std::memory_order_acquire)) {
            slot = it->second;
        }
    }
    if (!slot) {
        return true;  // 请求方已不存在，丢弃回复。
    }

    auto callback = std::move(pending.callback);
    asio::post(slot->strand,
               [slot, callback = std::move(callback), msg = std::move(message)]() mutable {
                   try {
                       callback(msg);
                   } catch (const std::exception& error) {
                       slot->actor->on_error(msg, error.what());
                   } catch (...) {
                       slot->actor->on_error(msg, "unknown exception");
                   }
               });
    return true;
}

inline void Runtime::finalize_stop(const std::shared_ptr<Slot>& slot) {
    try {
        slot->actor->on_stop();
    } catch (const std::exception& error) {
        std::fprintf(stderr, "[actor %llu] exception in on_stop: %s\n",
                     static_cast<unsigned long long>(slot->id), error.what());
    } catch (...) {
        std::fprintf(stderr, "[actor %llu] unknown exception in on_stop\n",
                     static_cast<unsigned long long>(slot->id));
    }

    {
        std::lock_guard actors_lock(_actors_mu);
        auto it = _actors.find(slot->id);
        if (it != _actors.end() && it->second == slot) {
            _actors.erase(it);
        }
    }

    try {
        slot->stop_promise.set_value();
    } catch (...) {
        // 已经满足。
    }
}

inline void Runtime::fail_pending(const std::exception_ptr& error) {
    std::unordered_map<SessionId, PendingReply> doomed;
    {
        std::lock_guard pending_lock(_pending_mu);
        doomed.swap(_pending);
    }
    for (auto& entry : doomed) {
        if (entry.second.promise) {
            try {
                entry.second.promise->set_exception(error);
            } catch (...) {
                // 已经满足。
            }
        }
    }
}

}  // 命名空间 libapp::actor
