#!/usr/bin/env bash
# The P-key pause idles instead of spinning. GamePaused (SOURCES/PERSO.CPP) draws nothing
# and ends on a key, so its loop is a clock pump and an input poll; where wall time is
# being spent, a live clock or a pinned clock paced to real time, it sleeps a millisecond
# per poll, and under a replay or an unpaced pinned clock it does not. Nothing in the loop
# mints a step, so the only thing the sleep changes is wall time, which is what this
# fixture measures: two pauses of different lengths in the same clock regime, differenced.
# Differenced, because every pause carries about 400 ms of sample fades on the wall clock
# whatever its length (docs/knowledge/quirks/sample-fades-end-on-the-wall-clock.md), so a
# single run's floor passes without the sleep for any pause under some 400 polls.
#
# Three arms. A live clock: 980 more paused polls cost at least 980 ms more wall (one-sided,
# since a sleep can only take longer, so a loaded host cannot fail it). An unpaced pinned
# clock, the batch regime: the same 980 polls cost under 490 ms, the bound wide because it
# is a ceiling a loaded host could reach; measured at a few ms. And a paced recording, the
# player's regime: a session that pauses while recording replays clean, so sleeping through
# a recorded pause left nothing in the file a replay disagrees with.
# Tier: medium
TESTNAME=pause_idle
. "$(dirname "$0")/lib.sh"
precheck
need_save

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
# The pause is `key 19`, scancode P on the default binding; a developer's own cfg could
# have moved it, so the run gets a user directory of its own.
export LBA2_USER_DIR="$tmp/user"

# pause_wall <label> <polls> <runner...>: the wall time, in ms, from tick 89 to tick 100
# around a pause of <polls> polls pressed at tick 90 and ended by a second press.
pause_wall() {
    local label="$1" polls="$2"
    shift 2
    local a="$tmp/$label-a.json" b="$tmp/$label-b.json"
    "$@" --load "$LBA2_TEST_SAVE" --tick 110 \
        --exec-at 89 "dumpstate $a" \
        --exec-at 90 "key 19 1 5; key 19 1 $((polls + 5))" \
        --exec-at 100 "dumpstate $b" --exit >/dev/null 2>&1 ||
        fail "$label: run exited non-zero ($?), so the pause did not end on the second press"
    [ -s "$a" ] && [ -s "$b" ] || fail "$label: a state dump is missing"
    echo $(( $(jget "$b" "d['wall_ms']") - $(jget "$a" "d['wall_ms']") ))
}

# Live clock: the sleep is on.
live_long="$(pause_wall live-long 1000 ctl)"
live_short="$(pause_wall live-short 20 ctl)"
d_live=$((live_long - live_short))
[ "$d_live" -ge 980 ] ||
    fail "live clock: 980 more paused polls cost $d_live ms of wall (want >= 980): the pause is not sleeping"

# Unpaced pinned clock: the sleep is off, so a batch run outruns real time through a pause.
pin_long="$(pause_wall pinned-long 1000 ctl --fixed-dt 16)"
pin_short="$(pause_wall pinned-short 20 ctl --fixed-dt 16)"
d_pin=$((pin_long - pin_short))
[ "$d_pin" -le 490 ] ||
    fail "unpaced pinned clock: 980 more paused polls cost $d_pin ms of wall (want <= 490): a batch run is sleeping through its pause"

# Paced recording: the sleep is on, and the replay, which never sleeps, matches it.
rec="$tmp/pause.rec"
ctl_paced --fixed-dt 16 --load "$LBA2_TEST_SAVE" --record "$rec" --tick 150 \
    --exec-at 90 "key 19 1 5; key 19 1 305" --exit >/dev/null 2>&1 ||
    fail "paced recording: run exited non-zero ($?)"
[ -s "$rec" ] || fail "paced recording: nothing written to $rec"
out="$(ctl --fixed-dt 16 --load "$LBA2_TEST_SAVE" --replay "$rec" --tick 300 --exit 2>&1)" ||
    fail "replay: run exited non-zero ($?)"
case "$out" in
*"consistency failure"*) fail "replay: $(printf '%s\n' "$out" | grep -m1 'consistency failure')" ;;
esac
summary="$(printf '%s\n' "$out" | grep -m1 'replay ended')" ||
    fail "replay printed no summary; it cannot be said to have matched"
checked="$(printf '%s\n' "$summary" | sed -n 's/.*: \([0-9]*\) ticks checked.*/\1/p')"
[ -n "$checked" ] && [ "$checked" -ge 140 ] ||
    fail "replay: only ${checked:-0} ticks checked of 150 ($summary)"
case "$summary" in
*"first hash mismatch -1"*) ;;
*) fail "replay through a slept pause diverged: $summary" ;;
esac

pass "live +$d_live ms over 980 polls, pinned +$d_pin ms, a paced recording through a pause replayed clean on $checked ticks"
