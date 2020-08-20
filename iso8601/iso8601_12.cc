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

  JsIsoDate utc() { JsIsoDate r = *this; r.timezone_style = TimezoneStyle::ExplicitUtc; return r; }
  JsIsoDate utc_plus(int h, int m) { JsIsoDate r = *this; r.timezone_style = TimezoneStyle::ExplicitOffsetPlus; r.timezone_hours = h; r.timezone_minutes = m; return r; }
  JsIsoDate utc_minus(int h, int m) { JsIsoDate r = *this; r.timezone_style = TimezoneStyle::ExplicitOffsetMinus; r.timezone_hours = h; r.timezone_minutes = m; return r; }

  JsIsoDate time(int hours, int minutes) { JsIsoDate r = *this; r.time_style = TimeStyle::HourMinutes; r.hours = hours; r.minutes = minutes; return r; }
  JsIsoDate time(int hours, int minutes, int seconds) { JsIsoDate r = time(hours, minutes); r.time_style = TimeStyle::HourMinutesSeconds; r.seconds = seconds; return r; }
  JsIsoDate time(int hours, int minutes, int seconds, int milliseconds) { JsIsoDate r = time(hours, minutes, seconds); r.time_style = TimeStyle::HourMinutesSecondsMilliseconds; r.milliseconds = milliseconds; return r; }

  JsIsoDate() {}
  JsIsoDate(int year) : date_style(DateStyle::Year), year(year) {}
  JsIsoDate(int year, int month) : date_style(DateStyle::YearMonth), year(year), month(month) {}
  JsIsoDate(int year, int month, int day) : date_style(DateStyle::YearMonthDay), year(year), month(month), day(day) {}
  JsIsoDate(int year, int month, int day, int hours, int minutes) : date_style(DateStyle::YearMonthDay), year(year), month(month), day(day), time_style(TimeStyle::HourMinutes), hours(hours), minutes(minutes) {}
  JsIsoDate(int year, int month, int day, int hours, int minutes, int seconds) : date_style(DateStyle::YearMonthDay), year(year), month(month), day(day), time_style(TimeStyle::HourMinutesSeconds), hours(hours), minutes(minutes), seconds(seconds) {}
  JsIsoDate(int year, int month, int day, int hours, int minutes, int seconds, int milliseconds) : date_style(DateStyle::YearMonthDay), year(year), month(month), day(day), time_style(TimeStyle::HourMinutesSecondsMilliseconds), hours(hours), minutes(minutes), seconds(seconds), milliseconds(milliseconds) {}
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

bool parse_timezone_offset(string_view& s, JsIsoDate* d)
{
  assert(d->timezone_style == TimezoneStyle::ExplicitOffsetPlus || d->timezone_style == TimezoneStyle::ExplicitOffsetMinus);
  return parse_n_digits(s, 2, &d->timezone_hours) && d->timezone_hours >= 0 && d->timezone_hours <= 24 &&
         consume(s, ':') &&
         parse_n_digits(s, 2, &d->timezone_minutes) && d->timezone_minutes >= 0 && d->timezone_minutes <= 59;
}

bool parse_date(string_view& s, JsIsoDate* d) {
  return parse_year(s, d) &&
         (!consume(s, '-') || parse_month(s, d)) &&
         (!consume(s, '-') || parse_day(s, d));
}

bool maybe_parse_timezone(string_view& s, JsIsoDate* d) {
  assert(d->time_style != TimeStyle::None);
  if (consume(s, 'Z')) {
    d->timezone_style = TimezoneStyle::ExplicitUtc;
    return true;
  }
  if (consume(s, '+')) {
    d->timezone_style = TimezoneStyle::ExplicitOffsetPlus;
    return parse_timezone_offset(s, d);
  }
  if (consume(s, '-')) {
    d->timezone_style = TimezoneStyle::ExplicitOffsetMinus;
    return parse_timezone_offset(s, d);
  }
  return true;
}

bool parse_time(string_view& s, JsIsoDate* d) {
  return parse_hours_minutes(s, d) &&
         (!consume(s, ':') || parse_seconds(s, d)) &&
         (!consume(s, '.') || parse_milliseconds(s, d)) &&
         maybe_parse_timezone(s, d);
}

// Returns a JsIsoDate if the input matches the format described in
// https://www.ecma-international.org/ecma-262/#sec-date-time-string-format
// That means that it returns a valid object even for things like '2020-02-31' or '2020-01-15T24:59'.
optional<JsIsoDate> parse_js_ios_date(string_view s) {
  JsIsoDate date;

  if (parse_date(s, &date) &&
      (!consume(s, 'T') || parse_time(s, &date)) &&
      s.empty())
    return date;

  return {};
}

bool operator==(const JsIsoDate& a, const JsIsoDate& b) {
  return a.date_style == b.date_style &&
         a.year == b.year &&
         (a.date_style < DateStyle::YearMonth || a.month == b.month) &&
         (a.date_style < DateStyle::YearMonthDay || a.day == b.day) &&
         a.time_style == b.time_style &&
         (a.time_style < TimeStyle::HourMinutes || a.hours == b.hours && a.minutes == b.minutes) &&
         (a.time_style < TimeStyle::HourMinutesSeconds || a.seconds == b.seconds) &&
         (a.time_style < TimeStyle::HourMinutesSecondsMilliseconds || a.milliseconds == b.milliseconds) &&
         a.timezone_style == b.timezone_style &&
         (a.timezone_style < TimezoneStyle::ExplicitOffsetPlus || a.timezone_hours == b.timezone_hours && a.timezone_minutes == b.timezone_minutes);
}

bool operator!=(const JsIsoDate& a, const JsIsoDate& b) { return !(a == b); }

int main() {
  optional<JsIsoDate> d;

  d = parse_js_ios_date("2020-");
  if (d) exit(1);

  d = parse_js_ios_date("2020");
  if (d != JsIsoDate(2020)) exit(2);

  d = parse_js_ios_date("2020-99");
  if (d) exit(3);

  d = parse_js_ios_date("2020-1");
  if (d) exit(4);

  d = parse_js_ios_date("2020-01-");
  if (d) exit(5);

  d = parse_js_ios_date("2020-11");
  if (d != JsIsoDate(2020, 11)) exit(6);

  d = parse_js_ios_date("2020-11-15");
  if (d != JsIsoDate(2020, 11, 15)) exit(7);

  d = parse_js_ios_date("2020-11-15T23");
  if (d) exit(8);

  d = parse_js_ios_date("2020-11-15T23:59");
  if (d != JsIsoDate(2020, 11, 15, 23, 59)) exit(9);

  d = parse_js_ios_date("2020-11-15T23:59:40");
  if (d != JsIsoDate(2020, 11, 15, 23, 59, 40)) exit(10);

  d = parse_js_ios_date("2020-11-15T23:59:40.123");
  if (d != JsIsoDate(2020, 11, 15, 23, 59, 40, 123)) exit(11);

  d = parse_js_ios_date("2020-11-15T23:59:40.123Z");
  if (d != JsIsoDate(2020, 11, 15, 23, 59, 40, 123).utc()) exit(12);

  d = parse_js_ios_date("2020-11-15T23:59:40.123Zo");
  if (d) exit(13);

  d = parse_js_ios_date("2020-11-15T23:59:40.123+14:15");
  if (d != JsIsoDate(2020, 11, 15, 23, 59, 40, 123).utc_plus(14, 15)) exit(14);

  d = parse_js_ios_date("2020-11-15T23:59:40.123-14:15");
  if (d != JsIsoDate(2020, 11, 15, 23, 59, 40, 123).utc_minus(14, 15)) exit(15);

  d = parse_js_ios_date("2020-11-15T23:59:40.123=14:15");
  if (d) exit(16);

  d = parse_js_ios_date("2020-11-15Z");
  if (d) exit(17);

  d = parse_js_ios_date("2020T23:59:40.123");
  if (d != JsIsoDate(2020).time(23, 59, 40, 123)) exit(18);

  d = parse_js_ios_date("2020-05T23:59:40");
  if (d != JsIsoDate(2020, 5).time(23, 59, 40)) exit(19);

  d = parse_js_ios_date("2020-05-20T23:59");
  if (d != JsIsoDate(2020, 5, 20).time(23, 59)) exit(20);

  d = parse_js_ios_date("2020-11-15T23:59:40Z");
  if (d != JsIsoDate(2020, 11, 15, 23, 59, 40).utc()) exit(21);

  d = parse_js_ios_date("2020-11-15T23:59Z");
  if (d != JsIsoDate(2020, 11, 15, 23, 59).utc()) exit(21);

  d = parse_js_ios_date("2020-11-15T23:59Z+04:05");
  if (d) exit(23);

  d = parse_js_ios_date("2020-11-15T23:59+04:05Z");
  if (d) exit(24);
}
