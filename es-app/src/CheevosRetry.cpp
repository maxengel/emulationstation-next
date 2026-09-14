#include "CheevosRetry.h"

namespace CheevosRetry
{
	int nextDelayMs(bool unreachable, bool online, int unreachableInARow)
	{
		if (!unreachable || !online)
			return ScheduledMs;

		// The count is 1 on the first failure. Anything under that is a
		// caller that did not count, and a guard that cannot see its input
		// does not retry.
		if (unreachableInARow < 1 || unreachableInARow > Attempts)
			return ScheduledMs;

		return IntervalMs;
	}
}
