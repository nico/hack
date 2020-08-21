#include <assert.h>
#include <functional>
#include <optional>
#include <string_view>

using namespace std;

struct JsIsoDate {
  // -1 for all values means unset.
  // If month is not -1, year won't be.
  // If day is not -1, month won't be.
  // hours and minutes are either both -1 or both set.
  // If seconds is not -1, hours and minutes won't be.
  // If milliseconds is not -1, seconds won't be.
  // timezone_hours and timezone_minutes are set exactly if timezone is '+' or '-'.
  int year = -1;
  int month = -1;
  int day = -1;
  int hours = -1;
  int minutes = -1;
  int seconds = -1;
  int milliseconds = -1;
  char timezone = -1; // 'Z', '+', '-', or -1
  int timezone_hours = -1;
  int timezone_minutes = -1;
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

bool parse_hours_minutes(string_view& s, int& h, int& m) {
  return parse_n_digits(s, 2, &h) && consume(s, ':') && parse_n_digits(s, 2, &m) && h >= 0 && h <= 24 && m >= 0 && m <= 59;
}

bool parse_year(string_view& s, JsIsoDate& d)
{
    // FIXME: for expanded years ('+' or '-' followed by 6 digits), error if it would fit in 4 digits?
    if (consume(s, '+'))
        return parse_n_digits(s, 6, &d.year);
    if (consume(s, '-')) {
        int year;
        if (!parse_n_digits(s, 6, &year))
            return false;
        d.year = -year;
        return true;
    }
    return parse_n_digits(s, 4, &d.year);
}
bool parse_month(string_view& s, JsIsoDate& d) { return parse_n_digits(s, 2, &d.month) && d.month >= 1 && d.month <= 12; }
bool parse_day(string_view& s, JsIsoDate& d) { return parse_n_digits(s, 2, &d.day) && d.month >= 1 && d.month <= 31; }
bool parse_hours_minutes(string_view& s, JsIsoDate& d) { return parse_hours_minutes(s, d.hours, d.minutes); }
bool parse_seconds(string_view& s, JsIsoDate& d) { return parse_n_digits(s, 2, &d.seconds) && d.seconds >= 0 && d.seconds <= 59; }
bool parse_milliseconds(string_view& s, JsIsoDate& d) { return parse_n_digits(s, 3, &d.milliseconds); }

bool parse_seconds_milliseconds(string_view& s, JsIsoDate& d)
{
  return parse_seconds(s, d) && (!consume(s, '.') || parse_milliseconds(s, d));
}

bool parse_month_days(string_view& s, JsIsoDate& d) { return (parse_month(s, d) && (!consume(s, '-') || parse_day(s, d))); }

bool parse_timezone(string_view& s, JsIsoDate& d)
{
  if (consume(s, '+')) {
    d.timezone = '+';
    return parse_hours_minutes(s, d.timezone_hours, d.timezone_minutes);
  }
  if (consume(s, '-')) {
    d.timezone = '-';
    return parse_hours_minutes(s, d.timezone_hours, d.timezone_minutes);
  }
  if (consume(s, 'Z'))
    d.timezone = 'Z';
  return true;
}

bool parse_time(string_view& s, JsIsoDate& d)
{
  return parse_hours_minutes(s, d) &&
          (!consume(s, ':') || parse_seconds_milliseconds(s, d)) &&
          parse_timezone(s, d);
}

// Returns a JsIsoDate if the input matches the format described in
// https://www.ecma-international.org/ecma-262/#sec-date-time-string-format
// That means that it returns a valid object even for things like '2020-02-31' or '2020-01-15T24:59'.
optional<JsIsoDate> parse_js_ios_date(string_view s) {
  JsIsoDate date;
  if (!parse_year(s, date) ||
      consume(s, '-') && !parse_month_days(s, date) ||
      consume(s, 'T') && !parse_time(s, date))
    return {};
  if (s.empty()) return date;
  return {};
}

bool operator==(const JsIsoDate& a, const JsIsoDate& b) {
  return a.year == b.year && a.month == b.month && a.day == b.day &&
         a.hours == b.hours && a.minutes == b.minutes && a.seconds == b.seconds && a.milliseconds == b.milliseconds &&
         a.timezone == b.timezone && a.timezone_hours == b.timezone_hours && a.timezone_minutes == b.timezone_minutes;
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

  if (parse_js_ios_date("2020-11") != JsIsoDate{2020, 11})
    exit(6);

  if (parse_js_ios_date("2020-11-15") != JsIsoDate{2020, 11, 15})
    exit(7);

  if (parse_js_ios_date("2020-11-15T23"))
    exit(8);

  if (parse_js_ios_date("2020-11-15T23:59") != JsIsoDate{2020, 11, 15, 23, 59})
    exit(9);

  if (parse_js_ios_date("2020-11-15T23:59:40") != JsIsoDate{2020, 11, 15, 23, 59, 40})
    exit(10);

  if (parse_js_ios_date("2020-11-15T23:59:40.123") != JsIsoDate{2020, 11, 15, 23, 59, 40, 123})
    exit(11);

  if (parse_js_ios_date("2020-11-15T23:59:40.123Z") != JsIsoDate{2020, 11, 15, 23, 59, 40, 123, 'Z'})
    exit(12);

  if (parse_js_ios_date("2020-11-15T23:59:40.123Zo"))
    exit(13);

  if (parse_js_ios_date("2020-11-15T23:59:40.123+14:15") != JsIsoDate{2020, 11, 15, 23, 59, 40, 123, '+', 14, 15})
    exit(14);

  if (parse_js_ios_date("2020-11-15T23:59:40.123-14:15") != JsIsoDate{2020, 11, 15, 23, 59, 40, 123, '-', 14, 15})
    exit(15);

  if (parse_js_ios_date("2020-11-15T23:59:40.123=14:15"))
    exit(16);

  if (parse_js_ios_date("2020-11-15Z"))
    exit(17);

  if (parse_js_ios_date("2020T23:59:40.123") != JsIsoDate{2020, .hours=23, .minutes=59, .seconds=40, .milliseconds=123})
    exit(18);

  if (parse_js_ios_date("2020-05T23:59:40") != JsIsoDate{2020, 5, -1, 23, 59, 40})
    exit(19);

  if (parse_js_ios_date("2020-05-20T23:59") != JsIsoDate{2020, 5, 20, 23, 59})
    exit(20);

  if (parse_js_ios_date("2020-05-20T23:59:40Z") != JsIsoDate{2020, 5, 20, 23, 59, 40, .timezone='Z'})
    exit(21);

  if (parse_js_ios_date("2020-11-15T23:59Z") != JsIsoDate{2020, 11, 15, 23, 59, .timezone='Z'})
    exit(21);

  if (parse_js_ios_date("2020-11-15T23:59Z+04:05"))
    exit(23);

  if (parse_js_ios_date("2020-11-15T23:59+04:05Z"))
    exit(24);

  if (parse_js_ios_date("2020-11-15T23:59.123"))
    exit(25);

  if (parse_js_ios_date("+202020") != JsIsoDate{202020})
    exit(26);

  if (parse_js_ios_date("-202020") != JsIsoDate{-202020})
    exit(27);

  if (parse_js_ios_date("+2020"))
    exit(28);

  if (parse_js_ios_date("2020-+11"))
    exit(29);

  if (parse_js_ios_date("2020-11-+12"))
    exit(30);

  if (parse_js_ios_date("2020-11-12T+14:15"))
    exit(31);

  if (parse_js_ios_date("2020-11-12T14:+15"))
    exit(32);

  if (parse_js_ios_date("2020-11-12T14:15+-12:13"))
    exit(33);
}
