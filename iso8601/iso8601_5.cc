#include <assert.h>
#include <optional>
#include <string_view>

using namespace std;

bool parse_n_digits(string_view& s, int n, int* out) {
  int r = 0;
  for (int i = 0; i < n; ++i) {
    if (s.size() < n || !isdigit(s[i]))
      return false;
    r = 10 * r + s[i] - '0';
  }
  *out = r;
  s.remove_prefix(n);
  return true;
}

enum class DateStyle {
  Year,
  YearMonth,
  YearMonthDay,
};

enum class TimeStyle {
  None,
  HourMinutes,
  HourMinutesSeconds,
  HourMinutesSecondsMilliseconds,
};

enum class TimezoneStyle {
  None,
  ExplicitUtc, // 'Z'
  ExplicitOffsetPlus,
  ExplicitOffsetMinus,
};

struct JsIsoDate {
  DateStyle date_style;
  int year = 0;
  int month = 1;
  int day = 1;

  TimeStyle time_style = TimeStyle::None;
  int hours = 0;
  int minutes = 0;
  int seconds = 0;
  int milliseconds = 0;

  TimezoneStyle timezone_style = TimezoneStyle::None;
  int timezone_hours = 0;
  int timezone_minutes = 0;
};

bool parse_year(string_view& s, JsIsoDate* d) {
  // FIXME: doesn't cover expanded years ('+' or '-' followed by 6 digits)
  d->date_style = DateStyle::Year;
  return parse_n_digits(s, 4, &d->year);
}

bool parse_month(string_view& s, JsIsoDate* d)
{
  assert(d->date_style == DateStyle::Year);
  d->date_style = DateStyle::YearMonth;
  return parse_n_digits(s, 2, &d->month) && d->month >= 1 && d->month <= 12;
}

bool parse_day(string_view& s, JsIsoDate* d)
{
  assert(d->date_style == DateStyle::YearMonth);
  d->date_style = DateStyle::YearMonthDay;
  return parse_n_digits(s, 2, &d->day) && d->day >= 1 && d->day <= 31;
}

bool consume(string_view& s, char c)
{
  if (s.empty() || s[0] != c)
    return false;
  s.remove_prefix(1);
  return true;
}

bool parse_hours_minutes(string_view& s, JsIsoDate* d) {
  assert(d->time_style == TimeStyle::None);
  d->time_style = TimeStyle::HourMinutes;
  return parse_n_digits(s, 2, &d->hours) && d->hours >= 0 && d->hours <= 24 &&
         consume(s, ':') &&
         parse_n_digits(s, 2, &d->minutes) && d->minutes >= 0 && d->minutes <= 59;
}

bool parse_seconds(string_view& s, JsIsoDate* d)
{
  assert(d->time_style == TimeStyle::HourMinutes);
  d->time_style = TimeStyle::HourMinutesSeconds;
  return parse_n_digits(s, 2, &d->seconds) && d->seconds >= 0 && d->seconds <= 59;
}

bool parse_milliseconds(string_view& s, JsIsoDate* d)
{
  assert(d->time_style == TimeStyle::HourMinutesSeconds);
  d->time_style = TimeStyle::HourMinutesSecondsMilliseconds;
  return parse_n_digits(s, 3, &d->milliseconds);
}

bool parse_timezone_offset(string_view s, JsIsoDate* d)
{
  return parse_n_digits(s, 2, &d->timezone_hours) && d->timezone_hours >= 0 && d->timezone_hours <= 24 &&
         consume(s, ':') &&
         parse_n_digits(s, 2, &d->timezone_minutes) && d->timezone_minutes >= 0 && d->timezone_minutes <= 59;
}

// Returns a JsIsoDate if the input matches the format described in
// https://www.ecma-international.org/ecma-262/#sec-date-time-string-format
// That means that it returns a valid object even for things like '2020-02-31' or '2020-01-15T24:59'.
optional<JsIsoDate> parse_js_ios_date(string_view s) {
  JsIsoDate date;

  if (!parse_year(s, &date))
    return {};

  if (s.empty())
    return date;
  if (consume(s, '-')) {
    if (!parse_month(s, &date))
      return {};

    if (s.empty())
      return date;
    if (consume(s, '-')) {
      if (!parse_day(s, &date))
        return {};

      if (s.empty())
        return date;
    }
  }
  if (!consume(s, 'T'))
    return {};

  if (!parse_hours_minutes(s, &date))
    return {};
  if (s.empty())
    return date;
  if (consume(s, ':')) {
    if (!parse_seconds(s, &date))
      return {};
    if (s.empty())
      return date;
    if (consume(s, '.')) {
      if (!parse_milliseconds(s, &date))
        return {};
      if (s.empty())
        return date;
    }
  }

  if (consume(s, 'Z')) {
    if (!s.empty())
      return {};
    date.timezone_style = TimezoneStyle::ExplicitUtc;
    return date;
  }

  if (s[0] != '+' && s[0] != '-')
    return {};
  date.timezone_style = s[0] == '+' ? TimezoneStyle::ExplicitOffsetPlus : TimezoneStyle::ExplicitOffsetMinus;
  s.remove_prefix(1);
  if (s.size() == 5) {
    if (!parse_timezone_offset(s, &date))
      return {};
    return date;
  }
  

  return {};
}

int main() {
  optional<JsIsoDate> d;

  d = parse_js_ios_date("2020-");
  if (d) exit(1);

  d = parse_js_ios_date("2020");
  if (d->date_style != DateStyle::Year || d->year != 2020) exit(2);

  d = parse_js_ios_date("2020-99");
  if (d) exit(3);

  d = parse_js_ios_date("2020-1");
  if (d) exit(4);

  d = parse_js_ios_date("2020-01-");
  if (d) exit(5);

  d = parse_js_ios_date("2020-11");
  if (d->date_style != DateStyle::YearMonth || d->year != 2020 || d->month != 11 || d->time_style != TimeStyle::None || d->timezone_style != TimezoneStyle::None) exit(6);

  d = parse_js_ios_date("2020-11-15");
  if (d->date_style != DateStyle::YearMonthDay || d->year != 2020 || d->month != 11 || d->day != 15 || d->time_style != TimeStyle::None || d->timezone_style != TimezoneStyle::None) exit(7);

  d = parse_js_ios_date("2020-11-15T23");
  if (d) exit(8);

  d = parse_js_ios_date("2020-11-15T23:59");
  if (d->date_style != DateStyle::YearMonthDay || d->year != 2020 || d->month != 11 || d->day != 15 || d->time_style != TimeStyle::HourMinutes || d->hours != 23 || d->minutes != 59 || d->timezone_style != TimezoneStyle::None) exit(9);

  d = parse_js_ios_date("2020-11-15T23:59:40");
  if (d->date_style != DateStyle::YearMonthDay || d->year != 2020 || d->month != 11 || d->day != 15 || d->time_style != TimeStyle::HourMinutesSeconds || d->hours != 23 || d->minutes != 59 || d->seconds != 40 || d->timezone_style != TimezoneStyle::None) exit(10);

  d = parse_js_ios_date("2020-11-15T23:59:40.123");
  if (d->date_style != DateStyle::YearMonthDay || d->year != 2020 || d->month != 11 || d->day != 15 || d->time_style != TimeStyle::HourMinutesSecondsMilliseconds || d->hours != 23 || d->minutes != 59 || d->seconds != 40 || d->milliseconds != 123 || d->timezone_style != TimezoneStyle::None) exit(11);

  d = parse_js_ios_date("2020-11-15T23:59:40.123Z");
  if (d->date_style != DateStyle::YearMonthDay || d->year != 2020 || d->month != 11 || d->day != 15 || d->time_style != TimeStyle::HourMinutesSecondsMilliseconds || d->hours != 23 || d->minutes != 59 || d->seconds != 40 || d->milliseconds != 123 || d->timezone_style != TimezoneStyle::ExplicitUtc) exit(12);

  d = parse_js_ios_date("2020-11-15T23:59:40.123Zo");
  if (d) exit(13);

  d = parse_js_ios_date("2020-11-15T23:59:40.123+14:15");
  if (d->date_style != DateStyle::YearMonthDay || d->year != 2020 || d->month != 11 || d->day != 15 || d->time_style != TimeStyle::HourMinutesSecondsMilliseconds || d->hours != 23 || d->minutes != 59 || d->seconds != 40 || d->milliseconds != 123 || d->timezone_style != TimezoneStyle::ExplicitOffsetPlus || d->timezone_hours != 14 || d->timezone_minutes != 15) exit(14);

  d = parse_js_ios_date("2020-11-15T23:59:40.123-14:15");
  if (d->date_style != DateStyle::YearMonthDay || d->year != 2020 || d->month != 11 || d->day != 15 || d->time_style != TimeStyle::HourMinutesSecondsMilliseconds || d->hours != 23 || d->minutes != 59 || d->seconds != 40 || d->milliseconds != 123 || d->timezone_style != TimezoneStyle::ExplicitOffsetMinus || d->timezone_hours != 14 || d->timezone_minutes != 15) exit(15);

  d = parse_js_ios_date("2020-11-15T23:59:40.123=14:15");
  if (d) exit(16);

  d = parse_js_ios_date("2020-11-15Z");
  if (d) exit(17);

  d = parse_js_ios_date("2020T23:59:40.123");
  if (d->date_style != DateStyle::Year || d->year != 2020 || d->time_style != TimeStyle::HourMinutesSecondsMilliseconds || d->hours != 23 || d->minutes != 59 || d->seconds != 40 || d->milliseconds != 123 || d->timezone_style != TimezoneStyle::None) exit(18);

  d = parse_js_ios_date("2020-05T23:59:40");
  if (d->date_style != DateStyle::YearMonth || d->year != 2020 || d->month != 5 || d->time_style != TimeStyle::HourMinutesSeconds || d->hours != 23 || d->minutes != 59 || d->seconds != 40 || d->timezone_style != TimezoneStyle::None) exit(19);

  d = parse_js_ios_date("2020-05-20T23:59");
  if (d->date_style != DateStyle::YearMonthDay || d->year != 2020 || d->month != 5 || d->day != 20 || d->time_style != TimeStyle::HourMinutes || d->hours != 23 || d->minutes != 59 || d->timezone_style != TimezoneStyle::None) exit(20);

  d = parse_js_ios_date("2020-11-15T23:59:40Z");
  if (d->date_style != DateStyle::YearMonthDay || d->year != 2020 || d->month != 11 || d->day != 15 || d->time_style != TimeStyle::HourMinutesSeconds || d->hours != 23 || d->minutes != 59 || d->seconds != 40 || d->timezone_style != TimezoneStyle::ExplicitUtc) exit(21);

  d = parse_js_ios_date("2020-11-15T23:59Z");
  if (d->date_style != DateStyle::YearMonthDay || d->year != 2020 || d->month != 11 || d->day != 15 || d->time_style != TimeStyle::HourMinutes || d->hours != 23 || d->minutes != 59 || d->timezone_style != TimezoneStyle::ExplicitUtc) exit(22);
}
