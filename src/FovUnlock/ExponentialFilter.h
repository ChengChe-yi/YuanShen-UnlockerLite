#pragma once
#include <chrono>
#include <cmath>
#include <concepts>

namespace Util
{

    template <typename Real, typename Clock = std::chrono::steady_clock>
        requires std::floating_point<Real>
    class ExponentialFilter
    {
    public:
        explicit ExponentialFilter(Real timeConstant = Real{ 0 }, Real initialValue = Real{ 0 }) noexcept
            : m_timeConstant(timeConstant)
            , m_value(initialValue)
            , m_lastTime(Clock::now()) {}

        Real GetTimeConstant() const noexcept { return m_timeConstant; }
        void SetTimeConstant(Real value) noexcept { m_timeConstant = value; }

        Real GetValue() const noexcept { return m_value; }

        void Reset(Real value) noexcept
        {
            m_value = value;
            m_lastTime = Clock::now();
        }

        Real Update(Real value) noexcept
        {
            const auto now = Clock::now();
            using Seconds = std::chrono::duration<Real>;
            const Real dt = std::chrono::duration_cast<Seconds>(now - m_lastTime).count();
            m_lastTime = now;

            if (m_timeConstant <= Real{ 0 })
                return value;

            const Real alpha = std::exp(-dt / m_timeConstant);
            m_value = alpha * m_value + (Real{ 1 } - alpha) * value;
            return m_value;
        }

    private:
        Real m_timeConstant;
        Real m_value;
        std::chrono::time_point<Clock> m_lastTime;
    };
}
