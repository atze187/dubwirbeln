#pragma once

namespace dw {

// Spec Part 7: OFF, VELOCITY, VELOCITY + INTENSITY (default).
enum class VelocityFollow { Off, Velocity, VelocityIntensity };

// Intensity used to generate the roll. VELOCITY + INTENSITY replaces the stored value by the played
// velocity (1..127 maps linearly to 0..100); the stored value in the preset is never changed.
double effectiveIntensity(double storedIntensity, VelocityFollow follow, int playedVelocity);

// Trigger velocity after following the played velocity: scaled by playedVelocity / 127, rounded
// half up and clamped to 1..127. OFF leaves it unchanged.
int followVelocity(int triggerVelocity, VelocityFollow follow, int playedVelocity);

}  // namespace dw
