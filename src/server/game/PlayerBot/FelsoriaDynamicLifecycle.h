// Project Felsoria: one generation-tagged database operation per bot session.
#ifndef FELSORIA_DYNAMIC_LIFECYCLE_H
#define FELSORIA_DYNAMIC_LIFECYCLE_H
#include <atomic>
#include <cstdint>
#include <memory>
#include <utility>
namespace FelsoriaDynamicLifecycle
{
    enum class Operation : std::uint64_t { Create = 1, Login = 2 };
    class OperationGate
    {
        std::atomic<std::uint64_t> value{0};
        static std::uint64_t Encoded(std::uint64_t token, Operation operation)
        { return (token << 2) | static_cast<std::uint64_t>(operation); }
    public:
        bool Begin(std::uint64_t token, Operation operation)
        {
            if (!token || token > (UINT64_MAX >> 2)) return false;
            std::uint64_t empty = 0;
            return value.compare_exchange_strong(empty, Encoded(token, operation));
        }
        bool Complete(std::uint64_t token, Operation operation)
        {
            if (!token) return false;
            std::uint64_t expected = Encoded(token, operation);
            return value.compare_exchange_strong(expected, 0);
        }
        std::uint64_t Token(Operation operation) const
        {
            std::uint64_t observed = value.load();
            return (observed & 3) == static_cast<std::uint64_t>(operation) ? observed >> 2 : 0;
        }
        bool Busy() const { return value.load() != 0; }
    };
    using Receipt = std::shared_ptr<std::atomic<int>>; // 0 pending, 1 committed, -1 failed
    using WriteFence = std::shared_ptr<std::atomic<std::uint32_t>>;
    // Captured by the native TransactionTask callback. Its destruction also
    // reports terminal failure, without calling a session/player on a DB thread.
    class CommitReceipt
    {
        Receipt status;
        WriteFence writes;
    public:
        CommitReceipt(Receipt state, WriteFence fence) : status(std::move(state)), writes(std::move(fence))
        { if (writes) writes->fetch_add(1); }
        CommitReceipt(CommitReceipt const&) = delete;
        CommitReceipt& operator=(CommitReceipt const&) = delete;
        ~CommitReceipt()
        {
            if (status) { int pending = 0; status->compare_exchange_strong(pending, -1); }
            if (writes) writes->fetch_sub(1);
        }
        void Committed() { if (status) status->store(1); }
    };
    inline bool Expired(std::uint32_t now, std::uint32_t since, std::uint32_t duration)
    { return std::uint32_t(now - since) >= duration; }
}
#endif
