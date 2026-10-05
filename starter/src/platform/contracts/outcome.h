#pragma once

namespace pet::platform {
// Result of one native or host operation, such as selecting a tab or raising a window.
enum class Outcome {
    Skipped,        // Not attempted, because an earlier step decided against it.
    Unsupported,    // This host, backend or session cannot do it (a program or service is missing).
    MissingTarget,  // No usable identity to act on: absent or malformed.
    TargetNotFound, // The identity was valid but nothing matches it now.
    Failed,
    TimedOut,
    Requested,      // Sent; the desktop is expected but not known to have complied.
    Confirmed,      // Done and observed.
};
inline bool succeeded(Outcome outcome) { return outcome == Outcome::Requested || outcome == Outcome::Confirmed; }
inline const char *outcomeName(Outcome outcome) {
    switch (outcome) {
    case Outcome::Skipped: return "skipped";
    case Outcome::Unsupported: return "unsupported";
    case Outcome::MissingTarget: return "missing-target";
    case Outcome::TargetNotFound: return "target-not-found";
    case Outcome::Failed: return "failed";
    case Outcome::TimedOut: return "timed-out";
    case Outcome::Requested: return "requested";
    case Outcome::Confirmed: return "confirmed";
    }
    return "?";
}
// Whether something is in front of the user. Unknown is never evidence that it is.
enum class ActiveState { Unknown, Inactive, Active };
}
