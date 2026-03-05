#pragma once
#include <functional>
#include <unordered_map>
#include <cstdint>

// ─────────────────────────────────────────────────────────────────────────────
// EventBus<EventT>  —  type-safe publish/subscribe event bus.
//
// Usage:
//   auto tok = EventBus<MyEvent>::Subscribe([](const MyEvent& e){ ... });
//   EventBus<MyEvent>::Emit({ .field = value });
//   EventBus<MyEvent>::Unsubscribe(tok);
//
// The bus data lives in a function-local static so it is initialised exactly
// once and is safe to use across translation units without linker issues.
// ─────────────────────────────────────────────────────────────────────────────
template<typename EventT>
class EventBus
{
public:
    using Handler = std::function<void(const EventT&)>;
    using Token   = uint32_t;

    /// Register a handler.  Returns a token needed for Unsubscribe.
    static Token Subscribe(Handler fn)
    {
        auto& d  = GetData();
        Token id = ++d.nextId;
        d.handlers[id] = std::move(fn);
        return id;
    }

    /// Remove a previously registered handler.
    static void Unsubscribe(Token tok)
    {
        GetData().handlers.erase(tok);
    }

    /// Fire the event — all subscribers are called synchronously.
    static void Emit(const EventT& event)
    {
        for (auto& [id, fn] : GetData().handlers)
            fn(event);
    }

private:
    struct BusData
    {
        std::unordered_map<Token, Handler> handlers;
        Token nextId = 0;
    };
    static BusData& GetData()
    {
        static BusData d;
        return d;
    }
};
