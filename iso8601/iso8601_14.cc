#include <assert.h>
#include <functional>
#include <optional>
#include <string_view>

using namespace std;

#if 0


enum class DateStyle {
  Invalid,
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
#endif

struct MonthDays {
  int month;
  optional<int> day;
};

struct SecondsMilliseconds {
  int seconds;
  optional<int> milliseconds;
};

struct BaseTime {
  int hours;
  int minutes;
};

struct Timezone {
  optional<BaseTime> offset;
};

struct Time : BaseTime {
  optional<SecondsMilliseconds> seconds_milliseconds;
  optional<Timezone> timezone; // If set but offset member not set, in UTC, else no explicit timezone
};

struct JsIsoDate {
  int year;
  optional<MonthDays> month_days;
  optional<Time> time;
};

bool parse_n_digits(string_view& s, size_t n, int* out) {
  int r = 0;
  for (size_t i = 0; i < n; ++i) {
    if (s.size() < n || !isdigit(s[i]))
      return false;
    r = 10 * r + s[i] - '0';
  }
  *out = r;
  s.remove_prefix(n);
  return true;
}

bool consume(string_view& s, char c)
{
  if (s.empty() || s[0] != c)
    return false;
  s.remove_prefix(1);
  return true;
}

#if 0
bool parse_n_digits(string_view& s, size_t n, std::function<void(int)> action) {
  int i;
  if (!parse_n_digits(s, n, &i))
    return false;
  action(i);
  return true;
}

bool parse_year(string_view& s, JsIsoDate* d) {
  // FIXME: doesn't cover expanded years ('+' or '-' followed by 6 digits)
  return parse_n_digits(s, 4, [d](int y) { d->append_year(y); });
}

bool parse_month(string_view& s, JsIsoDate* d)
{
  return parse_n_digits(s, 2, [d](int m) { d->append_month(m); }) && d->month() >= 1 && d->month() <= 12;
}

bool parse_day(string_view& s, JsIsoDate* d)
{
  return parse_n_digits(s, 2, [d](int day) { d->append_day(day); }) && d->day() >= 1 && d->day() <= 31;
}

bool parse_hours_minutes(string_view& s, std::function<void(int, int)> action) {
  int h, m;
  if (!(parse_n_digits(s, 2, &h) && consume(s, ':') && parse_n_digits(s, 2, &m) && h >= 0 && h <=24 && m >= 0 && m <= 59))
    return false;
  action(h, m);
  return true;
}

bool parse_hours_minutes(string_view& s, JsIsoDate* d) {
  return parse_hours_minutes(s, [d](int h, int m) { d->append_hours_minutes(h, m); });
}

bool parse_seconds(string_view& s, JsIsoDate* d)
{
  return parse_n_digits(s, 2, [d](int s) { d->append_seconds(s); }) && d->seconds() >= 0 && d->seconds() <= 59;
}

bool parse_milliseconds(string_view& s, JsIsoDate* d)
{
  return parse_n_digits(s, 3, [d](int s) { d->append_milliseconds(s); });
}

bool parse_date(string_view& s, JsIsoDate* d) {
  return parse_year(s, d) &&
         (!consume(s, '-') || parse_month(s, d)) &&
         (!consume(s, '-') || parse_day(s, d));
}

bool maybe_parse_timezone(string_view& s, JsIsoDate* d) {
  assert(d->time_style() != TimeStyle::None);
  if (consume(s, '+'))
    return parse_hours_minutes(s, [d](int h, int m) { d->set_utc_plus(h, m); });
  if (consume(s, '-'))
    return parse_hours_minutes(s, [d](int h, int m) { d->set_utc_minus(h, m); });
  if (consume(s, 'Z'))
    d->set_utc();
  return true;
}

bool parse_time(string_view& s, JsIsoDate* d) {
  return parse_hours_minutes(s, d) &&
         (!consume(s, ':') || parse_seconds(s, d)) &&
         (!consume(s, '.') || parse_milliseconds(s, d)) &&
         maybe_parse_timezone(s, d);
}
#endif

optional<optional<int>> parse_days(string_view& s) {
  if (!consume(s, '-'))
    return make_optional(optional<int>()); // No error, no value
  int d;
  if (!parse_n_digits(s, 2, &d) || d < 1 || d > 31)
    return {}; // Error
  return make_optional(d);
}

optional<optional<MonthDays>> parse_month_days(string_view& s) {
  if (!consume(s, '-'))
    return make_optional(optional<MonthDays>()); // No error, no value
  int m;
  if (!parse_n_digits(s, 2, &m) || m < 1 || m > 12)
    return {}; // Error
  MonthDays md = {m};

  optional<optional<int>> d = parse_days(s);
  if (!d)
    return {};
  md.day = *d;

  return make_optional(md);
}

// Returns a JsIsoDate if the input matches the format described in
// https://www.ecma-international.org/ecma-262/#sec-date-time-string-format
// That means that it returns a valid object even for things like '2020-02-31' or '2020-01-15T24:59'.
optional<JsIsoDate> parse_js_ios_date(string_view s) {
  int y;
  if (!parse_n_digits(s, 4, &y))
    return {};

  JsIsoDate date{y};
  optional<optional<MonthDays>> md = parse_month_days(s);
  if (!md)
    return {};
  date.month_days = *md;

  //optional<optional<MonthDays>> t = parse_time();
  //if (!t)
   // return {};
//  date.time = *t;

  if (s.empty()) return date;
  return {};
}

bool operator==(const SecondsMilliseconds& a, const SecondsMilliseconds& b) {
  return a.seconds == b.seconds && a.milliseconds == b.milliseconds;
}

bool operator==(const MonthDays& a, const MonthDays& b) {
  return a.month == b.month && a.day == b.day;
}

bool operator==(const BaseTime& a, const BaseTime& b) {
  return a.hours == b.hours && a.minutes == b.minutes;
}

bool operator==(const Timezone& a, const Timezone& b) {
  return a.offset == b.offset;
}

bool operator==(const Time& a, const Time& b) {
  return (BaseTime)a == (BaseTime)b && a.seconds_milliseconds == b.seconds_milliseconds && a.timezone == b.timezone;
}

bool operator==(const JsIsoDate& a, const JsIsoDate& b) {
  return a.year == b.year && a.month_days == b.month_days && a.time == b.time;
}

bool operator!=(const JsIsoDate& a, const JsIsoDate& b) { return !(a == b); }

int main() {
  if (parse_js_ios_date("2020-"))
    exit(1);

  if (parse_js_ios_date("2020") != JsIsoDate{2020})
    exit(2);

  if (parse_js_ios_date("2020-99"))
    exit(3);

  if (parse_js_ios_date("2020-1"))
    exit(4);

  if (parse_js_ios_date("2020-01-"))
    exit(5);

  if (parse_js_ios_date("2020-11") != JsIsoDate{2020, MonthDays{11}})
    exit(6);

  if (parse_js_ios_date("2020-11-15") != JsIsoDate{2020, MonthDays{11, 15}})
    exit(7);

  if (parse_js_ios_date("2020-11-15T23"))
    exit(8);

#if 0
  if (parse_js_ios_date("2020-11-15T23:59") != JsIsoDate(2020, 11, 15, 23, 59))
    exit(9);

  if (parse_js_ios_date("2020-11-15T23:59:40") != JsIsoDate(2020, 11, 15, 23, 59, 40))
    exit(10);

  if (parse_js_ios_date("2020-11-15T23:59:40.123") != JsIsoDate(2020, 11, 15, 23, 59, 40, 123))
    exit(11);

  if (parse_js_ios_date("2020-11-15T23:59:40.123Z") != JsIsoDate(2020, 11, 15, 23, 59, 40, 123).set_utc())
    exit(12);

  if (parse_js_ios_date("2020-11-15T23:59:40.123Zo"))
    exit(13);

  if (parse_js_ios_date("2020-11-15T23:59:40.123+14:15") != JsIsoDate(2020, 11, 15, 23, 59, 40, 123).set_utc_plus(14, 15))
    exit(14);

  if (parse_js_ios_date("2020-11-15T23:59:40.123-14:15") != JsIsoDate(2020, 11, 15, 23, 59, 40, 123).set_utc_minus(14, 15))
    exit(15);

  if (parse_js_ios_date("2020-11-15T23:59:40.123=14:15"))
    exit(16);

  if (parse_js_ios_date("2020-11-15Z"))
    exit(17);

  if (parse_js_ios_date("2020T23:59:40.123") != JsIsoDate(2020).set_time(23, 59, 40, 123))
    exit(18);

  if (parse_js_ios_date("2020-05T23:59:40") != JsIsoDate(2020, 5).set_time(23, 59, 40))
    exit(19);

  if (parse_js_ios_date("2020-05-20T23:59") != JsIsoDate(2020, 5, 20).set_time(23, 59))
    exit(20);

  if (parse_js_ios_date("2020-11-15T23:59:40Z") != JsIsoDate(2020, 11, 15, 23, 59, 40).set_utc())
    exit(21);

  if (parse_js_ios_date("2020-11-15T23:59Z") != JsIsoDate(2020, 11, 15, 23, 59).set_utc())
    exit(21);

  if (parse_js_ios_date("2020-11-15T23:59Z+04:05"))
    exit(23);

  if (parse_js_ios_date("2020-11-15T23:59+04:05Z"))
    exit(24);
#endif
}
