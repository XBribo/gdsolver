#pragma once

#include "core/state.hpp"
#include "geometry/corridor.hpp"
#include "models/ufo_params.hpp"

namespace gdapprox {

// UFO control is an event stream, not a level. `ControlSegment::held == true`
// means "a flap is issued on the FIRST tick of this segment"; the remaining
// ticks of the segment are coast. Holding does not repeat the flap, which is
// what GD does (one press = one flap), so a segment is exactly "flap, then
// wait `ticks` ticks".
class UfoModel final : public IApproxModel {
public:
    UfoModel() = default;

    ApproxResult simulate(const ApproxState& start,
                          const std::vector<ControlSegment>& controls,
                          const ICorridor* corridor,
                          const SimulateOptions& options) const override;
    using IApproxModel::simulate;

    // `boostLatch` is GD's velocity-limit exemption, the byte [player+0x952]
    // (dp/state.hpp State::boost, dp/slopes.hpp boostLatchMode). Unlike the
    // ship's, the UFO's acceleration block (updateJump 0x38c701-0x38c8df)
    // never reads it -- a byte scan of the whole flying branch finds the four
    // references at 0x38c59e (the clear), 0x38c5be / 0x38c5d8 (the ship) and
    // 0x38ca9f (the clamp) and no other -- so for the UFO the latch does one
    // thing only: it skips the terminal clamp.
    // `slopeVel`: GD's m_slopeVelocity when the flap comes off a ramp contact (dp's
    // --ufolawflap), 0 otherwise.
    static double stepVy(double vy, bool flap, const UfoParams& p,
                         bool gravityFlipped = false, bool boostLatch = false,
                         double slopeVel = 0.0, float gravityMod = 1.f,
                         double timeScale = 1.0) {
        // A flap RAISES vy to the target and then the same call's gravity step
        // runs, which is where the old constant 6.871 came from. It is not an
        // overwrite: GD (PlayerObject::updateJump) only calls setYVelocity when
        // vy is below s*literal, so a UFO already climbing faster than the
        // target keeps its speed and merely spends the press.
        if (flap && vy < p.flapTargetVy) {
            vy = p.flapTargetVy;
            // ...and off a ramp (m_isOnSlope / m_wasOnSlope, m_slopeVelocity > 0) it adds
            // half the ramp's velocity, at most 1.4x the flap (the float product).
            if (slopeVel > 0.0) {
                const double cap = (double)(float)(vy * 1.399999976158142);
                vy = std::min((double)((float)slopeVel * 0.5f) + vy, cap);
            }
        }
        const double s = gravityFlipped ? -p.accelSwitchVy : p.accelSwitchVy;
        double a = (vy <= s) ? p.gravityWeak : p.gravityStrong;
        // Scale the native float product before quantisation, never the flap target or cap.
        if (gravityMod != 1.f) {
            const float base = 0.9581990242004395f * gravityMod;
            float step = (float)(0.225 * timeScale) * base;
            step *= vy <= s ? 0.800000011920929f : 1.2000000476837158f;
            step *= 0.5f;
            step /= p.vyMinPlayerFrame < -7.0 ? 0.8500000238418579f : 1.f;
            a = -(double)step;
        }
        double next = vy + a;
        // Both ends of GD's band, as the ship has had all along. Without the
        // rise side a UFO thrown upwards by an orb or a pad kept climbing at a
        // speed the game does not allow (see vyMaxPlayerFrame for the
        // measurement and for the exemption this does not model).
        if (boostLatch) return next;   // 0x38ca9f: the clamp block is jumped
        if (next > p.vyMaxPlayerFrame) next = p.vyMaxPlayerFrame;
        return next < p.vyMinPlayerFrame ? p.vyMinPlayerFrame : next;
    }

    const UfoParams& params(bool mini) const { return mini ? mini_ : normal_; }
    void setParams(bool mini, const UfoParams& p) { (mini ? mini_ : normal_) = p; }

private:
    UfoParams normal_ = UfoParams::normal();
    UfoParams mini_ = UfoParams::mini();
};

}  // namespace gdapprox
