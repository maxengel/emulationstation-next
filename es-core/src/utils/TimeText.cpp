#include "utils/TimeText.h"

#include <stdio.h>

namespace Utils
{
	namespace Time
	{
		std::string clockText(const tm& t, bool twelveHour, const std::string& localeMarker)
		{
			char buf[16];

			if (!twelveHour)
			{
				snprintf(buf, sizeof(buf), "%02d:%02d", t.tm_hour, t.tm_min);
				return buf;
			}

			// %I: 1 to 12, with midnight and noon both written 12.
			int hour = t.tm_hour % 12;
			if (hour == 0)
				hour = 12;
			snprintf(buf, sizeof(buf), "%02d:%02d", hour, t.tm_min);

			const std::string marker = !localeMarker.empty() ? localeMarker : (t.tm_hour < 12 ? "AM" : "PM");
			return std::string(buf) + " " + marker;
		}
	}
}
