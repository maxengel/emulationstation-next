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

		// Days since 1970-01-01 for a civil date, so two tm values compare
		// as calendar days without any year-length or leap arithmetic here
		// (Howard Hinnant's days_from_civil; valid for every year the tm
		// can hold).
		static long dayNumber(const tm& t)
		{
			long y = t.tm_year + 1900;
			const unsigned m = (unsigned)(t.tm_mon + 1);
			const unsigned d = (unsigned)t.tm_mday;
			y -= m <= 2;
			const long era = (y >= 0 ? y : y - 399) / 400;
			const unsigned yoe = (unsigned)(y - era * 400);
			const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
			const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
			return era * 146097 + (long)doe - 719468;
		}

		DayRelation dayRelation(const tm& stamp, const tm& now)
		{
			const long ago = dayNumber(now) - dayNumber(stamp);
			if (ago == 0)
				return DayRelation::Today;
			if (ago == 1)
				return DayRelation::Yesterday;
			return DayRelation::Older;
		}
	}
}
