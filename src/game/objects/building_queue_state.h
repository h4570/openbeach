#pragma once

#include <cstdint>
#include <deque>

namespace obl::object {

struct BuildingQueue {
    uint32_t m_ownerId; // owning building id; the sender of the messages to the queued guests
    uint32_t m_maxLength; // 0 = no queue (Enqueue refuses), 1..10 = points table, > 10 = points follow the tail
    std::deque<int32_t> m_qlQueue; // queued guest ids, head first
    std::deque<int32_t> m_qlCommmod; // the commodity each of them asked for (same length)
    float m_tail[2]; // where the next guest stands (x, y); the serve point when the queue is empty
    float m_ServePoint[2]; // m_ServePoint_x / _y
    float m_QueueDirection[2]; // m_QueueDirection_x / _y
    float* m_points; // (limit + 1) x (x, y) when 1 <= limit <= 10, else null
};

} // namespace obl::object
