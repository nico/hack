#include <assert.h>
#include <functional>
#include <optional>
#include <string_view>

using namespace std;

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

class JsIsoDate {
 public:
  JsIsoDate() {}
  JsIsoDate(int y) { set_date(y); }
  JsIsoDate(int y, int m) { set_date(y, m); }
  JsIsoDate(int y, int m, int d) { set_date(y, m, d); }
  JsIsoDate(int y, int m, int d, int mi, int h) {
    set_date(y, m, d);
    set_time(mi, h);
  }
  JsIsoDate(int y, int m, int d, int mi, int h, int s) {
    set_date(y, m, d);
    set_time(mi, h, s);
  }
  JsIsoDate(int y, int m, int d, int mi, int h, int s, int ms) {
    set_date(y, m, d);
    set_time(mi, h, s, ms);
  }

  DateStyle date_style() const { return m_date_style; }
  int year() const {
    assert(date_style() >= DateStyle::Year);
    return m_year;
  }
  int month() const {
    assert(date_style() >= DateStyle::YearMonth);
    return m_month;
  }
  int day() const {
    assert(date_style() >= DateStyle::YearMonthDay);
    return m_day;
  }

  JsIsoDate& set_date(int y) {
    m_date_style = DateStyle::Year;
    m_year = y;
    return *this;
  }
  JsIsoDate& set_date(int y, int m) {
    set_date(y);
    m_date_style = DateStyle::YearMonth;
    m_month = m;
    return *this;
  }
  JsIsoDate& set_date(int y, int m, int d) {
    set_date(y, m);
    m_date_style = DateStyle::YearMonthDay;
    m_day = d;
    return *this;
  }


  void append_year(int y) {
    assert(date_style() == DateStyle::Invalid);
    set_date(y);
  }
  void append_month(int m) {
    assert(date_style() == DateStyle::Year);
    set_date(year(), m);
  }
  void append_day(int d) {
    assert(date_style() == DateStyle::YearMonth);
    set_date(year(), month(), d);
  }

  TimeStyle time_style() const { return m_time_style; }
  int hours() const {
    assert(time_style() >= TimeStyle::HourMinutes);
    return m_hours;
  }
  int minutes() const {
    assert(time_style() >= TimeStyle::HourMinutes);
    return m_minutes;
  }
  int seconds() const {
    assert(time_style() >= TimeStyle::HourMinutesSeconds);
    return m_seconds;
  }
  int milliseconds() const {
    assert(time_style() >= TimeStyle::HourMinutesSecondsMilliseconds);
    return m_milliseconds;
  }

  JsIsoDate& clear_time() {
    m_time_style = TimeStyle::None;
    return *this;
  }
  JsIsoDate& set_time(int h, int m) {
    m_time_style = TimeStyle::HourMinutes;
    m_hours = h;
    m_minutes = m;
    return *this;
  }
  JsIsoDate& set_time(int h, int m, int s) {
    set_time(h, m);
    m_time_style = TimeStyle::HourMinutesSeconds;
    m_seconds = s;
    return *this;
  }
  JsIsoDate& set_time(int h, int m, int s, int ms) {
    set_time(h, m, s);
    m_time_style = TimeStyle::HourMinutesSecondsMilliseconds;
    m_milliseconds = ms;
    return *this;
  }

  void append_hours_minutes(int h, int m) {
    assert(time_style() == TimeStyle::None);
    set_time(h, m);
  }
  void append_seconds(int s) {
    assert(time_style() == TimeStyle::HourMinutes);
    set_time(hours(), minutes(), s);
  }
  void append_milliseconds(int ms) {
    assert(time_style() == TimeStyle::HourMinutesSeconds);
    set_time(hours(), minutes(), seconds(), ms);
  }

  TimezoneStyle timezone_style() const { return m_timezone_style; }
  int timezone_hours() const {
    assert(timezone_style() >= TimezoneStyle::ExplicitOffsetPlus);
    return m_timezone_hours;
  }
  int timezone_minutes() const {
    assert(timezone_style() >= TimezoneStyle::ExplicitOffsetPlus);
    return m_timezone_minutes;
  }

  JsIsoDate& clear_timezone() {
    m_timezone_style = TimezoneStyle::None;
    return *this;
  }
  JsIsoDate& set_utc() {
    assert(time_style() != TimeStyle::None);
    m_timezone_style = TimezoneStyle::ExplicitUtc;
    return *this;
  }
  JsIsoDate& set_utc_plus(int h, int m) {
    assert(time_style() != TimeStyle::None);
    m_timezone_style = TimezoneStyle::ExplicitOffsetPlus;
    m_timezone_hours = h;
    m_timezone_minutes = m;
    return *this;
  }
  JsIsoDate& set_utc_minus(int h, int m) {
    assert(time_style() != TimeStyle::None);
    m_timezone_style = TimezoneStyle::ExplicitOffsetMinus;
    m_timezone_hours = h;
    m_timezone_minutes = m;
    return *this;
  }

 private:
  DateStyle m_date_style = DateStyle::Invalid;
  int m_year = 0;
  int m_month = 1;
  int m_day = 1;

  TimeStyle m_time_style = TimeStyle::None;
  int m_hours = 0;
  int m_minutes = 0;
  int m_seconds = 0;
  int m_milliseconds = 0;

  TimezoneStyle m_timezone_style = TimezoneStyle::None;
  int m_timezone_hours = 0;
  int m_timezone_minutes = 0;

};

bool parse_n_digits(string_view& s, size_t n, int* out)
{
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

bool parse_n_digits(string_view& s, size_t n, std::function<void(int)> action)
{
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

bool parse_hours_minutes(string_view& s, std::function<void(int, int)> action)
{
  int h, m;
  if (!(parse_n_digits(s, 2, &h) && consume(s, ':') && parse_n_digits(s, 2, &m) && h >= 0 && h <=24 && m >= 0 && m <= 59))
    return false;
  action(h, m);
  return true;
}

bool parse_year(string_view& s, JsIsoDate* d)
{
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

bool maybe_parse_timezone(string_view& s, JsIsoDate* d)
{
  assert(d->time_style() != TimeStyle::None);
  if (consume(s, '+'))
    return parse_hours_minutes(s, [d](int h, int m) { d->set_utc_plus(h, m); });
  if (consume(s, '-'))
    return parse_hours_minutes(s, [d](int h, int m) { d->set_utc_minus(h, m); });
  if (consume(s, 'Z'))
    d->set_utc();
  return true;
}

bool parse_seconds_milliseconds(string_view& s, JsIsoDate* d)
{
  return parse_seconds(s, d) && (!consume(s, '.') || parse_milliseconds(s, d));
}

bool parse_time(string_view& s, JsIsoDate* d)
{
  return parse_hours_minutes(s, d) &&
         (!consume(s, ':') || parse_seconds_milliseconds(s, d)) &&
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
  return a.date_style() == b.date_style() &&
         a.year() == b.year() &&
         (a.date_style() < DateStyle::YearMonth || a.month() == b.month()) &&
         (a.date_style() < DateStyle::YearMonthDay || a.day() == b.day()) &&
         a.time_style() == b.time_style() &&
         (a.time_style() < TimeStyle::HourMinutes || (a.hours() == b.hours() && a.minutes() == b.minutes())) &&
         (a.time_style() < TimeStyle::HourMinutesSeconds || a.seconds() == b.seconds()) &&
         (a.time_style() < TimeStyle::HourMinutesSecondsMilliseconds || a.milliseconds() == b.milliseconds()) &&
         a.timezone_style() == b.timezone_style() &&
         (a.timezone_style() < TimezoneStyle::ExplicitOffsetPlus || (a.timezone_hours() == b.timezone_hours() && a.timezone_minutes() == b.timezone_minutes()));
}

bool operator!=(const JsIsoDate& a, const JsIsoDate& b) { return !(a == b); }

int main() {
  if (parse_js_ios_date("2020-"))
    exit(1);

  if (parse_js_ios_date("2020") != JsIsoDate(2020))
    exit(2);

  if (parse_js_ios_date("2020-99"))
    exit(3);

  if (parse_js_ios_date("2020-1"))
    exit(4);

  if (parse_js_ios_date("2020-01-"))
    exit(5);

  if (parse_js_ios_date("2020-11") != JsIsoDate(2020, 11))
    exit(6);

  if (parse_js_ios_date("2020-11-15") != JsIsoDate(2020, 11, 15))
    exit(7);

  if (parse_js_ios_date("2020-11-15T23"))
    exit(8);

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

  if (parse_js_ios_date("2020-11-15T23:59.123"))
    exit(25);
}
