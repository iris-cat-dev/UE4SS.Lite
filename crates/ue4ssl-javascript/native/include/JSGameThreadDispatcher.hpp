#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <future>
#include <mutex>
#include <vector>

namespace RC::JSScript
{
    class GameThreadDispatcher
    {
    public:
        struct PendingOp
        {
            std::function<void*()> operation;
            std::promise<void*> result_promise;
            bool is_sync{false};
        };

        auto dispatch_sync(std::function<void*()> op) -> void*;
        auto dispatch_async(std::function<void()> op) -> void;
        auto drain_on_game_thread() -> void;
        [[nodiscard]] auto has_pending() const -> bool;

    private:
        std::vector<PendingOp> m_pending_ops;
        std::mutex m_mutex;
    };

} // namespace RC::JSScript
