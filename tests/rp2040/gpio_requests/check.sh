#!/bin/sh
# Host check of the pin masks, port mapping and request validation in
# sys/arch/rp2040/include/gpio.h, and of its PWM refusals, slice ownership
# and duty arithmetic, and of sixteen mutants that must fail it.

set -eu

topsrc=$(cd "$(dirname "$0")/../../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/gpio-requests.XXXXXX")
cleanup()
{
	cleanup_status=$?
	trap - EXIT HUP INT TERM
	find "$work" -depth -type f -exec rm -f {} +
	find "$work" -depth -type d -exec rmdir {} +
	exit "$cleanup_status"
}
trap cleanup EXIT
trap 'exit 1' HUP INT TERM

build()
{
	${CC:-cc} -std=c17 -O1 -g -Wall -Wextra -Werror -Wpedantic \
		-Wstrict-prototypes -Wold-style-definition \
		-fsanitize=address,undefined -I"$1" -o "$2" \
		"$topsrc/tests/rp2040/gpio_requests/gpio_requests_test.c"
}
run()
{
	ASAN_OPTIONS=abort_on_error=1:detect_leaks=0 \
	UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 "$@"
}

build "$topsrc/sys/arch/rp2040/include" "$work/gpio-requests-test"
run "$work/gpio-requests-test"

mutant()
{
	name=$1
	expression=$2
	mkdir "$work/$name"
	sed "$expression" "$topsrc/sys/arch/rp2040/include/gpio.h" >"$work/$name/gpio.h"
	if cmp -s "$topsrc/sys/arch/rp2040/include/gpio.h" "$work/$name/gpio.h"; then
		echo "gpio_requests: FAIL: $name mutation changed nothing" >&2
		exit 1
	fi
	build "$work/$name" "$work/gpio-requests-$name"
	if run "$work/gpio-requests-$name" >"$work/$name.log" 2>&1; then
		echo "gpio_requests: FAIL: $name mutation was accepted" >&2
		exit 1
	fi
	echo "gpio_requests: PASS ($name mutation rejected)"
}
mutant pico-uart-pin 's/return 0x1c7ffffcU;/return 0x1c7ffffeU;/'
mutant picocalc-audio-pin 's/return 1U << 28;/return 1U << 27;/'
mutant picocalc-keyboard-uart 's/return 1U << 28;/return 1U << 8 | 1U << 9 | 1U << 28;/'
mutant port-b-reach 's/(mask << 16) \& GPIO_PORTB_PINS/mask << 16/'
mutant open-drain 's/GPIO_STORE | GPIO_SET | GPIO_CLEAR/GPIO_CONFOD | GPIO_STORE | GPIO_SET | GPIO_CLEAR/'
mutant unknown-bit 's/(commands \& ~known) != 0)/(commands \& ~known) == GPIO_COMMAND)/'
mutant group 's/(GPIO_IOC_VOID | .g. << 8))/(GPIO_IOC_VOID | (request \& 0xff00)))/'
mutant pwm-alone 's/commands != (GPIO_PWM \& GPIO_COMMAND))/commands == 0)/'
mutant pwm-held-pin 's/(pins \& ~gpio_board_pins(board)) != 0 ||/(pins \& ~gpio_board_pins(board)) == 99 ||/'
mutant pwm-empty-mask 's/if (pins == 0 ||/if (pins == 99 ||/'
mutant pwm-slice5 's/if ((pins \& GPIO_PWM_SLICE_PINS(GPIO_PWM_SLICE_BELL)) != 0)/if (0)/'
mutant pwm-audio-slice 's/return audio_open != 0 \&\&/return audio_open == 99 \&\&/'
mutant pwm-duty-range 's/duty > GPIO_PWM_DUTY_MAX)$/duty > 2 * GPIO_PWM_DUTY_MAX)/'
mutant pwm-duty-scale 's/| duty << shift;/| (duty * 2) << shift;/'
mutant pwm-top 's/(GPIO_PWM_DUTY_MAX - 1)/(GPIO_PWM_DUTY_MAX)/'
mutant pwm-channel 's/16 \* (pin \& 1)/16 * (~pin \& 1)/'
mutant pwm-slice-map 's/(((pin) >> 1) \& 7U)/(((pin) >> 1) \& 3U)/'
