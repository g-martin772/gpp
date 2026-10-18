export module GPP.Simulation:Clock;

import std;

export namespace GPP
{
    // Time (physical seconds) is what shaders, particles and orbital phases read: the sum of dt * TimeScale over all ticks
    // played since the clock was last set. Elapsed (simulation seconds) is the sum of dt, the clock scripts' dt lives on.
    // Both stand still while the runner is paused.
    struct SimClock
    {
        double Time{0.0};
        double Elapsed{0.0};
        double TimeCarry{0.0};
        double ElapsedCarry{0.0};

        void Advance(const double dt, const double timeScale) noexcept
        {
            Accumulate(Elapsed, ElapsedCarry, dt);
            Accumulate(Time, TimeCarry, dt * timeScale);
        }

        void SetTime(const double seconds) noexcept
        {
            Time = seconds;
            TimeCarry = 0.0;
        }

        [[nodiscard]] bool operator==(const SimClock& other) const noexcept
        {
            return Time == other.Time && Elapsed == other.Elapsed && TimeCarry == other.TimeCarry &&
                   ElapsedCarry == other.ElapsedCarry;
        }

    private:
        // Two-sum keeps the rounding error of every addition, so ticks stay exact at 1e9 s and beyond.
        static void Accumulate(double& value, double& carry, const double x) noexcept
        {
            const double sum = value + x;
            const double virtualX = sum - value;
            carry += (value - (sum - virtualX)) + (x - virtualX);
            const double normalized = sum + carry;
            carry -= normalized - sum;
            value = normalized;
        }
    };
}
