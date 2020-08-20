#include <assert.h>
#include <functional>
#include <optional>
#include <string_view>

using namespace std;

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

enum class TimezoneSign { Plus, Minus };
struct TimezoneOffset {
  TimezoneSign sign;
  BaseTime offset;
};

struct Timezone {
  optional<TimezoneOffset> offset;
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

bool parse_n_digits(string_view& s, size_t n, std::function<void(int)> action) {
  int i;
  if (!parse_n_digits(s, n, &i))
    return false;
  action(i);
  return true;
}

bool consume(string_view& s, char c)
{
  if (s.empty() || s[0] != c)
    return false;
  s.remove_prefix(1);
  return true;
}

bool parse_hours_minutes(string_view& s, std::function<void(int, int)> action) {
  int h, m;
  if (!(parse_n_digits(s, 2, &h) && consume(s, ':') && parse_n_digits(s, 2, &m) && h >= 0 && h <= 24 && m >= 0 && m <= 59))
    return false;
  action(h, m);
  return true;
}

bool parse_year(string_view& s, optional<JsIsoDate>& ojid) {
  return parse_n_digits(s, 4, [&ojid](int y) { ojid.emplace(JsIsoDate{y}); });
}

bool parse_month(string_view& s, optional<MonthDays>& omd)
{
  return parse_n_digits(s, 2, [&omd](int m) { omd.emplace(MonthDays{m}); }) && omd->month >= 1 && omd->month <= 12;
}

bool parse_day(string_view& s, MonthDays& md)
{
  return parse_n_digits(s, 2, [&md](int d) { md.day.emplace(d); }) && *md.day >= 1 && *md.day <= 31;
}

bool parse_hours_minutes(string_view& s, optional<Time>& ot) {
  return parse_hours_minutes(s, [&ot](int h, int m) { ot.emplace(Time{h, m}); });
}

bool parse_seconds(string_view& s, optional<SecondsMilliseconds>& osm)
{
  return parse_n_digits(s, 2, [&osm](int s) { osm.emplace(SecondsMilliseconds{s}); }) && osm->seconds >= 0 && osm->seconds <= 59;
}

bool parse_milliseconds(string_view& s, optional<int>& oms)
{
  return parse_n_digits(s, 3, [&oms](int ms) { oms.emplace(ms); });
}

bool parse_seconds_milliseconds(string_view& s, optional<SecondsMilliseconds>& osm)
{
  return parse_seconds(s, osm) && (!consume(s, '.') || parse_milliseconds(s, osm->milliseconds));
}

bool parse_month_days(string_view& s, optional<MonthDays>& omd)
{
  return (parse_month(s, omd) && (!consume(s, '-') || parse_day(s, *omd)));
}

bool parse_timezone(string_view& s, optional<Timezone>& ot)
{
  if (consume(s, '+'))
    return parse_hours_minutes(s, [&ot](int h, int m) { ot.emplace(Timezone{TimezoneOffset{TimezoneSign::Plus, h, m}}); });
  if (consume(s, '-'))
    return parse_hours_minutes(s, [&ot](int h, int m) { ot.emplace(Timezone{TimezoneOffset{TimezoneSign::Minus, h, m}}); });
  if (consume(s, 'Z'))
    ot.emplace(Timezone{});
  return true;
}

bool parse_time(string_view& s, optional<Time>& ot)
{
  return parse_hours_minutes(s, ot) &&
          (!consume(s, ':') || parse_seconds_milliseconds(s, ot->seconds_milliseconds)) &&
          parse_timezone(s, ot->timezone);
}

// Returns a JsIsoDate if the input matches the format described in
// https://www.ecma-international.org/ecma-262/#sec-date-time-string-format
// That means that it returns a valid object even for things like '2020-02-31' or '2020-01-15T24:59'.
optional<JsIsoDate> parse_js_ios_date(string_view s) {
  optional<JsIsoDate> date;
  // FIXME: doesn't cover expanded years ('+' or '-' followed by 6 digits)
  if (!parse_year(s, date))
    return {};

  if (consume(s, '-') && !parse_month_days(s, date->month_days))
    return {};

  if (consume(s, 'T') && !parse_time(s, date->time))
    return {};

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

bool operator==(const TimezoneOffset& a, const TimezoneOffset& b) {
  return a.sign == b.sign && a.offset == b.offset;
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

  if (parse_js_ios_date("2020-11-15") != JsIsoDate{2020, {{11, 15}}})
    exit(7);

  if (parse_js_ios_date("2020-11-15T23"))
    exit(8);

  if (parse_js_ios_date("2020-11-15T23:59") != JsIsoDate{2020, {{11, 15}}, {{23, 59}}})
    exit(9);

  if (parse_js_ios_date("2020-11-15T23:59:40") != JsIsoDate{2020, {{11, 15}}, {{23, 59, {{40}}}}})
    exit(10);

  if (parse_js_ios_date("2020-11-15T23:59:40.123") != JsIsoDate{2020, {{11, 15}}, {{23, 59, {{40, 123}}}}})
    exit(11);

  if (parse_js_ios_date("2020-11-15T23:59:40.123Z") != JsIsoDate{2020, {{11, 15}}, {{23, 59, {{40, 123}}, {Timezone{}}}}})
    exit(12);

  if (parse_js_ios_date("2020-11-15T23:59:40.123Zo"))
    exit(13);

  if (parse_js_ios_date("2020-11-15T23:59:40.123+14:15") != JsIsoDate{2020, {{11, 15}}, {{23, 59, {{40, 123}}, {{{{TimezoneSign::Plus, 14, 15}}}}}}})
    exit(14);

  if (parse_js_ios_date("2020-11-15T23:59:40.123-14:15") != JsIsoDate{2020, {{11, 15}}, {{23, 59, {{40, 123}}, {{{{TimezoneSign::Minus, 14, 15}}}}}}})
    exit(15);

  if (parse_js_ios_date("2020-11-15T23:59:40.123=14:15"))
    exit(16);

  if (parse_js_ios_date("2020-11-15Z"))
    exit(17);

  if (parse_js_ios_date("2020T23:59:40.123") != JsIsoDate{2020, {}, {{23, 59, {{40, 123}}}}})
    exit(18);

  if (parse_js_ios_date("2020-05T23:59:40") != JsIsoDate{2020, {{5}}, {{23, 59, {{40}}}}})
    exit(19);

  if (parse_js_ios_date("2020-05-20T23:59") != JsIsoDate{2020, {{5, 20}}, {{23, 59}}})
    exit(20);

  if (parse_js_ios_date("2020-05-20T23:59:40Z") != JsIsoDate{2020, {{5, 20}}, {{23, 59, {{40}}, {Timezone{}}}}})
    exit(21);

  if (parse_js_ios_date("2020-11-15T23:59Z") != JsIsoDate{2020, {{11, 15}}, {{23, 59, {}, {Timezone{}}}}})
    exit(21);

  if (parse_js_ios_date("2020-11-15T23:59Z+04:05"))
    exit(23);

  if (parse_js_ios_date("2020-11-15T23:59+04:05Z"))
    exit(24);

  if (parse_js_ios_date("2020-11-15T23:59.123"))
    exit(25);
}
