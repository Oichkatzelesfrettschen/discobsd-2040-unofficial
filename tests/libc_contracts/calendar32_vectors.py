"""Write the expected calendar fields of the 32-bit view as a C header.

In the 32-bit view (sys/sys/_time_t.h) time_t is the kernel's unsigned
32-bit count, so gmtime() covers 1970-01-01 00:00:00 to 2106-02-07
06:28:15 UTC and timegm() inverts it over the same span. The oracle is
CPython's datetime, which shares no arithmetic with lib/libc/gen/ctime.c
or mktime.c.

The header holds two tables and two constants:

 - instant32_vectors: a second count and the UTC fields gmtime() must give
   for it, named counts at each end, at the 2038 carry out of bit 31 and
   at the leap days the century rule decides, then seeded random counts
   over the whole range and over its upper half;
 - field32_vectors: fields as a caller passes them to timegm(), some
   outside their ranges, with the count they name or a refusal, which
   every instant outside 0..4294967295 is; a test that reads them defines
   CALENDAR32_FIELDS;
 - ORACLE_COUNT_MAX and ORACLE_LAST_DAY, the last count and the day it
   falls on, which bound the tests' own sweeps.

usage: calendar32_vectors.py OUTPUT
"""

import datetime
import random
import sys

DAY = 86400
EPOCH = datetime.datetime(1970, 1, 1)
COUNT_MAX = 2 ** 32 - 1
INT_MIN = -(2 ** 31)
INT_MAX = 2 ** 31 - 1


def require(condition, message):
    if not condition:
        raise SystemExit("calendar32_vectors.py: " + message)


def fields(count):
    """The UTC fields of a second count since 1970, by datetime."""
    require(0 <= count <= COUNT_MAX, "a count leaves the 32-bit range")
    moment = EPOCH + datetime.timedelta(seconds=count)
    return (moment.year - 1900, moment.month - 1, moment.day, moment.hour,
            moment.minute, moment.second, (moment.weekday() + 1) % 7,
            moment.timetuple().tm_yday - 1)


def count_of(year, month, day, hour=0, minute=0, second=0):
    """The second count of a UTC instant, which may lie outside the range."""
    delta = datetime.datetime(year, month, day, hour, minute, second) - EPOCH
    return delta.days * DAY + delta.seconds


require(count_of(2106, 2, 7, 6, 28, 15) == COUNT_MAX,
        "the last count is not 2106-02-07 06:28:15")


def instants():
    named = [
        0, 1, 59, 60, 3599, 3600, 86399, 86400,
        2 ** 31 - 1, 2 ** 31, 2 ** 31 + 1, 0x90000000, 0xc0000000,
        COUNT_MAX - 1, COUNT_MAX, COUNT_MAX - DAY,
        count_of(2038, 1, 19), count_of(2038, 1, 20) - 1,
        count_of(2096, 2, 29), count_of(2100, 2, 28),
        count_of(2100, 3, 1), count_of(2100, 12, 31, 23, 59, 59),
        count_of(2104, 2, 29), count_of(2105, 12, 31, 23, 59, 59),
        count_of(2106, 1, 1), count_of(2106, 2, 7),
        count_of(2106, 2, 7) - 1,
    ]
    generator = random.Random(2106)
    sampled = [generator.randint(0, COUNT_MAX) for _ in range(1500)]
    sampled += [generator.randint(2 ** 31, COUNT_MAX) for _ in range(1500)]
    return named + sampled


def field_cases():
    """Fields as (year, mon, mday, hour, min, sec), tm_year relative to 1900."""
    return [
        (70, 0, 1, 0, 0, 0),
        (69, 11, 31, 23, 59, 59),
        (69, 11, 31, 0, 0, 0),
        (70, 0, 0, 0, 0, 0),
        (70, 0, 1, 0, 0, -1),
        (70, 0, 1, -1, 0, 0),
        (70, 0, 2, -24, 0, 0),
        (71, 0, 0, 0, 0, 0),
        (138, 0, 19, 3, 14, 7),
        (138, 0, 19, 3, 14, 8),
        (138, 0, 19, 23, 0, 0),
        (146, 6, 23, 0, 38, 24),
        (200, 1, 29, 0, 0, 0),
        (200, 11, 31, 0, 0, 0),
        (206, 1, 7, 0, 0, 0),
        (206, 1, 7, 6, 28, 14),
        (206, 1, 7, 6, 28, 15),
        (206, 1, 7, 6, 28, 16),
        (206, 1, 7, 6, 29, 0),
        (206, 1, 8, 0, 0, 0),
        (206, 1, 6, 30, 28, 15),
        (206, 1, 6, 30, 28, 16),
        (70, 0, 49711, 6, 28, 15),
        (70, 0, 49712, 0, 0, 0),
        (70, 1633, 1, 0, 0, 0),
        (70, 1633, 7, 6, 28, 15),
        (70, 1633, 7, 6, 28, 16),
        (70, 0, 1, 0, 0, INT_MAX),
        (70, 0, 1, 0, INT_MAX, 0),
        (70, 0, 1, 1193046, 28, 15),
        (70, 0, 1, 1193046, 28, 16),
        (1, 0, 25568, 0, 0, 0),
        (1, 0, 25567, 23, 59, 59),
        (-1900, 0, 1, 0, 0, 0),
        (8099, 0, 1, 0, 0, 0),
        (INT_MAX, 0, 1, 0, 0, 0),
        (INT_MIN, 0, 1, 0, 0, 0),
    ]


def count_of_fields(year, mon, mday, hour, minute, sec):
    """The count fields name, by datetime, or None outside the range."""
    carry, month = divmod(mon, 12)
    if not 1 <= year + carry + 1900 <= 9999:
        # datetime has no such year; with every other field in its own
        # range the instant lies centuries outside 1970..2106.
        require(1 <= mday <= 31 and 0 <= hour < 24 and 0 <= minute < 60 and
                0 <= sec < 60, "a year outside datetime carries other fields")
        return None
    count = (count_of(year + carry + 1900, month + 1, 1) + (mday - 1) * DAY +
             hour * 3600 + minute * 60 + sec)
    return count if 0 <= count <= COUNT_MAX else None


def int_literal(value):
    """A C int constant; INT_MIN has no literal of its own."""
    require(INT_MIN <= value <= INT_MAX, "a field leaves an int")
    return "INT_MIN" if value == INT_MIN else "%d" % value


def main():
    require(len(sys.argv) == 2, "usage: calendar32_vectors.py OUTPUT")
    lines = [
        "/* Generated by tests/libc_contracts/calendar32_vectors.py; "
        "do not edit. */",
        "#define ORACLE_COUNT_MAX %dUL" % COUNT_MAX,
        "#define ORACLE_LAST_DAY %dL" % (COUNT_MAX // DAY),
        "",
        "static const struct instant32_vector instant32_vectors[] = {",
    ]
    for count in instants():
        lines.append("\t{ %dUL, %d, %d, %d, %d, %d, %d, %d, %d }," % (
            (count,) + fields(count)))
    lines += ["};", "", "#ifdef CALENDAR32_FIELDS",
              "static const struct field32_vector field32_vectors[] = {"]
    for case in field_cases():
        count = count_of_fields(*case)
        if count is not None:
            require(fields(count)[0] >= 70, "an accepted count is not after 1969")
        lines.append("\t{ %s, %d, %dUL }," % (
            ", ".join(int_literal(value) for value in case),
            int(count is not None), 0 if count is None else count))
    lines += ["};", "#endif", ""]
    with open(sys.argv[1], "w", encoding="ascii") as output:
        output.write("\n".join(lines))


if __name__ == "__main__":
    main()
