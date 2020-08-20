#include <assert.h>
#include <optional>
#include <string_view>

using namespace std;

bool parse_n_digits(string_view s, int n, int* out) {
  int r = 0;
  for (int i = 0; i < n; ++i) {
    if (s.size() < n || !isdigit(s[i]))
      return false;
    r = 10 * r + s[i] - '0';
  }
  *out = r;
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
  TimeStyle time_style = TimeStyle::None;
  TimezoneStyle timezone_style = TimezoneStyle::None;

  int year = 0;
  int month = 1;
  int day = 1;
  int hours = 0;
  int minutes = 0;
  int seconds = 0;
  int milliseconds = 0;

  int timezone_hours = 0;
  int timezone_minutes = 0;
};

bool parse_year(string_view s, JsIsoDate* d) {
  // FIXME: doesn't cover expanded years ('+' or '-' followed by 6 digits)
  return parse_n_digits(s, 4, &d->year);
}

bool parse_month(string_view s, JsIsoDate* d)
{
  return parse_n_digits(s, 2, &d->month) && d->month >= 1 && d->month <= 12;
}

bool parse_day(string_view s, JsIsoDate* d)
{
  return parse_n_digits(s, 2, &d->day) && d->day >= 1 && d->day <= 31;
}

bool parse_hours_minutes(string_view s, JsIsoDate* d) {
  if (s.size() < 5 || s[2] != ':')
    return false;
  return parse_n_digits(s, 2, &d->hours) && d->hours >= 0 && d->hours <= 24 &&
         parse_n_digits(s.substr(3), 2, &d->minutes) && d->minutes >= 0 && d->minutes <= 59;
}

bool parse_seconds(string_view s, JsIsoDate* d)
{
  return parse_n_digits(s, 2, &d->seconds) && d->seconds >= 0 && d->seconds <= 59;
}

bool parse_milliseconds(string_view s, JsIsoDate* d) { return parse_n_digits(s, 3, &d->milliseconds); }

bool parse_timezone_offset(string_view s, JsIsoDate* d) {
  if (s.size() < 5 || s[2] != ':')
    return false;
  return parse_n_digits(s, 2, &d->timezone_hours) && d->timezone_hours >= 0 && d->timezone_hours <= 24 &&
         parse_n_digits(s.substr(3), 2, &d->timezone_minutes) && d->timezone_minutes >= 0 && d->timezone_minutes <= 59;
}


// Returns a JsIsoDate if the input matches the format described in
// https://www.ecma-international.org/ecma-262/#sec-date-time-string-format
// That means that it returns a valid object even for things like '2020-02-31' or '2020-01-15T24:59'.
optional<JsIsoDate> parse_js_ios_date(string_view s) {
  JsIsoDate date;

  if (!parse_year(s, &date))
    return {};
  if (s.size() == 4) {
    date.date_style = DateStyle::Year;
    return date;
  }
  if (s[4] != '-')
    return {};
  s = s.substr(5);

  if (!parse_month(s, &date))
    return {};
  if (s.size() == 2) {
    date.date_style = DateStyle::YearMonth;
    return date;
  }
  if (s[2] != '-')
    return {};
  s = s.substr(3);

  if (!parse_day(s, &date))
    return {};
  if (s.size() == 2) {
    date.date_style = DateStyle::YearMonthDay;
    return date;
  }
  if (s[2] != 'T')
    return {};
  s = s.substr(3);

  date.date_style = DateStyle::YearMonthDay; // FIXME: test

  // FIXME: Can have optional timezone after each of the following

  if (!parse_hours_minutes(s, &date))
    return {};
  if (s.size() == 5) {
    date.time_style = TimeStyle::HourMinutes;
    return date;
  }
  if (s[5] != ':')
    return {};
  s = s.substr(6);

  if (!parse_seconds(s, &date))
    return {};
  if (s.size() == 2) {
    date.time_style = TimeStyle::HourMinutesSeconds;
    return date;
  }
  if (s[2] != '.')
    return {};
  s = s.substr(3);

  if (!parse_milliseconds(s, &date))
    return {};
  if (s.size() == 3) {
    date.time_style = TimeStyle::HourMinutesSecondsMilliseconds;
    return date;
  }
  if (s[3] != '+' && s[3] != '-' && s[3] != 'Z')
    return {};
  s = s.substr(3);

  date.time_style = TimeStyle::HourMinutesSecondsMilliseconds; // FIXME: test

  if (s[0] == 'Z') {
    if (s.size() != 1)
      return {};
    date.timezone_style = TimezoneStyle::ExplicitUtc;
    return date;
  }

  assert(s[0] == '+' || s[0] == '-');
  date.timezone_style = s[0] == '+' ? TimezoneStyle::ExplicitOffsetPlus : TimezoneStyle::ExplicitOffsetMinus;
  s = s.substr(1);
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
  if (d->date_style != DateStyle::YearMonth || d->year != 2020 || d->month != 11) exit(6);

  d = parse_js_ios_date("2020-11-15");
  if (d->date_style != DateStyle::YearMonthDay || d->year != 2020 || d->month != 11 || d->day != 15) exit(7);

  d = parse_js_ios_date("2020-11-15T23");
  if (d) exit(8);

  d = parse_js_ios_date("2020-11-15T23:59");
  if (d->date_style != DateStyle::YearMonthDay || d->time_style != TimeStyle::HourMinutes || d->year != 2020 || d->month != 11 || d->day != 15 || d->hours != 23 || d->minutes != 59) exit(9);

  d = parse_js_ios_date("2020-11-15T23:59:40");
  if (d->date_style != DateStyle::YearMonthDay || d->time_style != TimeStyle::HourMinutesSeconds || d->year != 2020 || d->month != 11 || d->day != 15 || d->hours != 23 || d->minutes != 59 || d->seconds != 40) exit(10);

  d = parse_js_ios_date("2020-11-15T23:59:40.123");
  if (d->date_style != DateStyle::YearMonthDay || d->time_style != TimeStyle::HourMinutesSecondsMilliseconds || d->year != 2020 || d->month != 11 || d->day != 15 || d->hours != 23 || d->minutes != 59 || d->seconds != 40 || d->milliseconds != 123) exit(11);

  d = parse_js_ios_date("2020-11-15T23:59:40.123Z");
  if (d->date_style != DateStyle::YearMonthDay || d->time_style != TimeStyle::HourMinutesSecondsMilliseconds || d->year != 2020 || d->month != 11 || d->day != 15 || d->hours != 23 || d->minutes != 59 || d->seconds != 40 || d->milliseconds != 123 || d->timezone_style != TimezoneStyle::ExplicitUtc) exit(12);

  d = parse_js_ios_date("2020-11-15T23:59:40.123Zo");
  if (d) exit(13);

  d = parse_js_ios_date("2020-11-15T23:59:40.123+14:15");
  if (d->date_style != DateStyle::YearMonthDay || d->time_style != TimeStyle::HourMinutesSecondsMilliseconds || d->year != 2020 || d->month != 11 || d->day != 15 || d->hours != 23 || d->minutes != 59 || d->seconds != 40 || d->milliseconds != 123 || d->timezone_style != TimezoneStyle::ExplicitOffsetPlus || d->timezone_hours != 14 || d->timezone_minutes != 15) exit(14);

  d = parse_js_ios_date("2020-11-15T23:59:40.123-14:15");
  if (d->date_style != DateStyle::YearMonthDay || d->time_style != TimeStyle::HourMinutesSecondsMilliseconds || d->year != 2020 || d->month != 11 || d->day != 15 || d->hours != 23 || d->minutes != 59 || d->seconds != 40 || d->milliseconds != 123 || d->timezone_style != TimezoneStyle::ExplicitOffsetMinus || d->timezone_hours != 14 || d->timezone_minutes != 15) exit(15);

  d = parse_js_ios_date("2020-11-15T23:59:40.123=14:15");
  if (d) exit(16);

  d = parse_js_ios_date("2020-11-15Z");
  if (d) exit(17);

  // FIXME:
  d = parse_js_ios_date("2020T23:59:40.123");
  d = parse_js_ios_date("2020-05T23:59:40");
  d = parse_js_ios_date("2020-05-20T23:59");
  d = parse_js_ios_date("2020-11-15T23:59:40Z");
  d = parse_js_ios_date("2020-11-15T23:59Z");
}
