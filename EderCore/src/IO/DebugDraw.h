#pragma once
#include <glm/glm.hpp>
#include <vector>
#include <mutex>
#include <algorithm>

namespace Krayon {

// A single debug line segment with an optional lifetime.
// duration <= 0  → drawn for one frame then discarded  
// duration >  0  → drawn until 'duration' seconds elapse (decremented by Tick)
struct DebugLine
{
    glm::vec3 start;
    glm::vec3 end;
    glm::vec4 color;    // RGBA, 0-1
    float     duration; // seconds remaining
};

// Thread-safe singleton that accumulates debug lines for the gizmo renderer.
// Usage (C++):
//   Krayon::DebugDraw::Get().AddRay({0,0,0}, {0,1,0}, {1,0,0,1}, 2.0f);
// Usage (Lua):
//   Debug.drawRay(0,0,0, 0,1,0, 1,0,0,1, 2.0)
class DebugDraw
{
public:
    static DebugDraw& Get()
    {
        static DebugDraw s_instance;
        return s_instance;
    }

    // Add a line from 'start' to 'end'.
    // color: RGBA 0-1 (default green)
    // duration: seconds to display; 0 = one frame only
    void AddLine(const glm::vec3& start, const glm::vec3& end,
                 const glm::vec4& color    = { 0.f, 1.f, 0.f, 1.f },
                 float            duration = 0.f)
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_lines.push_back({ start, end, color, duration });
    }

    // Add a ray: origin + direction*length drawn as a line.
    // direction is NOT normalised; its magnitude determines the line length.
    void AddRay(const glm::vec3& origin, const glm::vec3& direction,
                const glm::vec4& color    = { 0.f, 1.f, 0.f, 1.f },
                float            duration = 0.f)
    {
        AddLine(origin, origin + direction, color, duration);
    }

    // Advance lifetimes by dt seconds and remove expired lines.
    // Call once per frame AFTER rendering.
    void Tick(float dt)
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        for (auto& l : m_lines)
            l.duration -= dt;
        m_lines.erase(
            std::remove_if(m_lines.begin(), m_lines.end(),
                           [](const DebugLine& l) { return l.duration < 0.f; }),
            m_lines.end());
    }

    // Returns current set of lines (read-only snapshot for renderer).
    // Call from render thread; holds mutex briefly.
    const std::vector<DebugLine>& GetLines() const { return m_lines; }

    // Immediately removes all pending lines.
    void Clear()
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_lines.clear();
    }

private:
    DebugDraw() = default;
    DebugDraw(const DebugDraw&)            = delete;
    DebugDraw& operator=(const DebugDraw&) = delete;

    mutable std::mutex       m_mutex;
    std::vector<DebugLine>   m_lines;
};

} // namespace Krayon
