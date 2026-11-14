#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>

namespace libapp::actor {

using ActorId = std::uint64_t;
using MsgType = std::uint32_t;
using SessionId = std::uint64_t;

inline constexpr ActorId kSystemActor = 0;
inline constexpr SessionId kNoSession = 0;

/// 消息：谁发的、发往哪里、是什么类型，以及一个
/// 不透明 payload。回复复用请求的 session id，这样运行时
/// 可以把回复路由回等待中的 future/callback，而不是
/// 投递给目标 actor。
struct Message {
    ActorId source{kSystemActor};
    ActorId target{kSystemActor};
    MsgType type{0};
    SessionId session{kNoSession};
    std::shared_ptr<void> payload;
    /// 仅当消息由 Actor::reply() 产生时为 true。请求同样携带
    /// session，运行时需要这个标志区分两个方向，才能正确
    /// 路由回复到等待中的 future/callback。
    bool is_reply{false};

    Message() = default;

    Message(ActorId source_, ActorId target_, MsgType type_, SessionId session_,
            std::shared_ptr<void> payload_ = {}, bool is_reply_ = false)
        : source(source_),
          target(target_),
          type(type_),
          session(session_),
          payload(std::move(payload_)),
          is_reply(is_reply_) {}

    bool has_payload() const noexcept { return static_cast<bool>(payload); }

    template <typename T>
    const T* payload_as() const noexcept {
        return static_cast<const T*>(payload.get());
    }

    template <typename T>
    T* payload_as() noexcept {
        return static_cast<T*>(payload.get());
    }
};

template <typename T, typename... Args>
std::shared_ptr<void> make_payload(Args&&... args) {
    static_assert(std::is_object_v<T>, "payload type must be an object type");
    return std::make_shared<T>(std::forward<Args>(args)...);
}

inline std::shared_ptr<void> make_text(std::string text) {
    return std::make_shared<std::string>(std::move(text));
}

inline const std::string* payload_text(const Message& message) noexcept {
    return message.payload_as<std::string>();
}

}  // 命名空间 libapp::actor
