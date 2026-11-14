"""Write the expected calendar fields of the 64-bit view as a C header.

The oracle is CPython's datetime.date, which shares no arithmetic with
lib/libc/gen/ctime.c or mktime.c. It covers years 1 through 9999; the
Gregorian calendar repeats every 400 years (146097 days, a whole number of
weeks), so an instant outside that span is moved by whole eras into it,
read there, and its year moved back. The weekday and the day of the year
are unchanged by the move.

Every instant names the second count since 1970 and the fields gmtime()
must give for it. The range of the 64-bit build is every instant whose
year an int tm_year holds; the bounds are derived here from the calendar
and written out beside the vectors, so the test also checks the
constants in lib/libc/gen/offtime.h against an independent derivation.

usage: vectors.py OUTPUT
"""

import datetime
import random
import sys

ERA_DAYS = 146097
DAY = 86400
EPOCH_ORDINAL = datetime.date(1970, 1, 1).toordinal()
INT_MIN = -(2 ** 31)
INT_MAX = 2 ** 31 - 1
LLONG_MIN = -(2 ** 63)
LLONG_MAX = 2 ** 63 - 1


def require(condition, message):
    if not condition:
        raise SystemExit("vectors.py: " + message)


def fields(count):
    """The UTC fields of a second count since 1970, by datetime."""
    days, second = divmod(count, DAY)
    ordinal = days + EPOCH_ORDINAL
    eras = (ordinal - 1) // ERA_DAYS - 10
    date = datetime.date.fromordinal(ordinal - eras * ERA_DAYS)
    require(2000 <= date.year <= 6400, "era shift left the datetime range")
    return {
        "year": date.year + 400 * eras - 1900,
        "mon": date.month - 1,
        "mday": date.day,
        "hour": second // 3600,
        "min": second // 60 % 60,
        "sec": second % 60,
        "wday": (date.weekday() + 1) % 7,
        "yday": date.timetuple().tm_yday - 1,
    }


def count_of(year, month, day):
    """The second count of 00:00:00 UTC on a proleptic Gregorian date."""
    eras = (year - 4000) // 400
    date = datetime.date(year - 400 * eras, month, day)
    return (date.toordinal() + eras * ERA_DAYS - EPOCH_ORDINAL) * DAY


CAL_MIN = count_of(INT_MIN + 1900, 1, 1)
CAL_MAX = count_of(INT_MAX + 1900, 12, 31) + DAY - 1
require(fields(CAL_MIN)["year"] == INT_MIN, "lower bound is not year INT_MIN")
require(fields(CAL_MAX)["year"] == INT_MAX, "upper bound is not year INT_MAX")
require(fields(CAL_MIN - 1)["year"] == INT_MIN - 1, "lower bound not first")
require(fields(CAL_MAX + 1)["year"] == INT_MAX + 1, "upper bound not last")


def instants():
    named = [
        0, 1, -1, 59, 60, 86399, 86400, -86400, -86401,
        2 ** 31 - 1, 2 ** 31, 2 ** 31 + 1, -(2 ** 31), -(2 ** 31) - 1,
        2 ** 32 - 1, 2 ** 32, 2 ** 32 + 1, 0x90000000,
        count_of(2038, 1, 19), count_of(2106, 2, 7) + 6 * 3600 + 28 * 60 + 15,
        count_of(2000, 2, 29), count_of(2000, 3, 1), count_of(2100, 2, 28),
        count_of(2100, 3, 1), count_of(2400, 2, 29), count_of(1900, 2, 28),
        count_of(1900, 3, 1), count_of(1600, 2, 29), count_of(1601, 1, 1) - 1,
        count_of(1, 1, 1), count_of(0, 2, 29), count_of(0, 3, 1) - 1,
        count_of(-1, 12, 31), count_of(-400, 3, 1), count_of(-401, 2, 28),
        count_of(9999, 12, 31) + DAY - 1, count_of(10000, 1, 1),
        count_of(1000000, 6, 15), count_of(-1000000, 6, 15),
        CAL_MIN, CAL_MIN + 1, CAL_MIN + DAY, CAL_MAX, CAL_MAX - 1,
        CAL_MAX - DAY,
    ]
    generator = random.Random(2038)
    sampled = []
    for _ in range(1500):
        sampled.append(generator.randint(CAL_MIN, CAL_MAX))
    for _ in range(1500):
        magnitude = generator.randint(0, 2 ** generator.randint(1, 55))
        sampled.append(magnitude if generator.random() < 0.5 else -magnitude)
    for _ in range(500):
        sampled.append(generator.randint(-(2 ** 33), 2 ** 33))
    every = named + [value for value in sampled if CAL_MIN <= value <= CAL_MAX]
    for value in every:
        require(CAL_MIN <= value <= CAL_MAX, "a named instant is out of range")
    return every


def denormal_cases():
    """Fields outside their ranges, as (year, mon, mday, hour, min, sec)."""
    cases = [
        (70, 0, INT_MAX, 0, 0, 0),
        (70, 0, INT_MIN, 0, 0, 0),
        (70, 0, 1, INT_MAX, 0, 0),
        (70, 0, 1, INT_MIN, 0, 0),
        (70, 0, 1, 0, INT_MAX, INT_MAX),
        (70, 0, 1, 0, INT_MIN, INT_MIN),
        (70, INT_MAX, 1, 0, 0, 0),
        (70, INT_MIN, 1, 0, 0, 0),
        (INT_MAX, -1, 1, 0, 0, 0),
        (INT_MIN, 12, 1, 0, 0, 0),
        (INT_MAX, 11, 31, 23, 59, 59),
        (INT_MIN, 0, 1, 0, 0, 0),
        (INT_MAX, 11, 31, 23, 59, 60),
        (INT_MIN, 0, 1, 0, 0, -1),
        (INT_MAX, 12, 1, 0, 0, 0),
        (INT_MIN, -1, 31, 0, 0, 0),
        (INT_MAX, INT_MAX, INT_MAX, INT_MAX, INT_MAX, INT_MAX),
        (INT_MIN, INT_MIN, INT_MIN, INT_MIN, INT_MIN, INT_MIN),
        (-1900, 1, 29, 12, 0, 0),
        (200, 1, 29, 0, 0, 0),
        (8099, 0, 1, 0, 0, 0),
    ]
    out = []
    for year, mon, mday, hour, minute, sec in cases:
        carry, month = divmod(mon, 12)
        count = (count_of(year + carry + 1900, month + 1, 1) +
                 (mday - 1) * DAY + hour * 3600 + minute * 60 + sec)
        accepted = CAL_MIN <= count <= CAL_MAX
        out.append((year, mon, mday, hour, minute, sec, accepted,
                    count if accepted else -1))
    return out


WEEKDAYS = ("Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat")
MONTHS = ("Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep",
          "Oct", "Nov", "Dec")


def ctime_text(count):
    """asctime(3)'s line for an instant in UTC, or None past year 9999."""
    f = fields(count)
    year = f["year"] + 1900
    if not 1000 <= year <= 9999:
        return None
    return "%s %s%3d %02d:%02d:%02d %d\\n" % (
        WEEKDAYS[f["wday"]], MONTHS[f["mon"]], f["mday"], f["hour"],
        f["min"], f["sec"], year)


CTIME_INSTANTS = [
    0, -1, 2 ** 31 - 1, 2 ** 31, 0x90000000, 2 ** 32 - 1, 2 ** 32,
    count_of(1000, 1, 1), count_of(1000, 1, 1) - 1,
    count_of(9999, 12, 31) + DAY - 1, count_of(10000, 1, 1),
]


def literal(value):
    """A C constant of type long long for any 64-bit value."""
    require(LLONG_MIN <= value <= LLONG_MAX, "a value leaves a long long")
    if value == LLONG_MIN:
        return "(-0x7fffffffffffffffLL - 1)"
    return "%dLL" % value


def main():
    require(len(sys.argv) == 2, "usage: vectors.py OUTPUT")
    lines = [
        "/* Generated by tests/time64_calendar/vectors.py; do not edit. */",
        "#define ORACLE_CAL_MIN %s" % literal(CAL_MIN),
        "#define ORACLE_CAL_MAX %s" % literal(CAL_MAX),
        "",
        "static const struct instant_vector instant_vectors[] = {",
    ]
    for count in instants():
        f = fields(count)
        lines.append("\t{ %s, %d, %d, %d, %d, %d, %d, %d, %d }," % (
            literal(count), f["year"], f["mon"], f["mday"], f["hour"],
            f["min"], f["sec"], f["wday"], f["yday"]))
    lines += ["};", "",
              "static const struct denormal_vector denormal_vectors[] = {"]
    for year, mon, mday, hour, minute, sec, accepted, count in denormal_cases():
        lines.append("\t{ %d, %d, %d, %d, %d, %d, %d, %s }," % (
            year, mon, mday, hour, minute, sec, int(accepted),
            literal(count)))
    lines += ["};", "",
              "static const struct ctime_vector ctime_vectors[] = {"]
    for count in CTIME_INSTANTS:
        text = ctime_text(count)
        lines.append("\t{ %s, %s }," % (
            literal(count), "0" if text is None else '"%s"' % text))
    lines += ["};", ""]
    with open(sys.argv[1], "w", encoding="ascii") as output:
        output.write("\n".join(lines))


if __name__ == "__main__":
    main()
