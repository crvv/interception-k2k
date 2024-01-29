#ifdef VERBOSE
# include <stdio.h> /* fprintf() */
#endif
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h> /* EXIT_FAILURE */
#include <errno.h> /* errno */
#include <unistd.h> /* STD*_FILENO, read() */
#include <string.h> /* memcpy() */
#include <linux/input.h> /* KEY_*, struct input_event */
#include <time.h> /* CLOCK_*, clock_gettime() */
#include <limits.h> /* INT_MAX */

#ifndef MAX_EVENTS
/* How many events to buffer internally.
 *
 * Note that it doesn't introduce any delays, just aims reducing the number of
 * read(2)s and write(2)s.
 * */
# define MAX_EVENTS 10
#endif

/* KEY_* codes: /usr/include/linux/input-event-codes.h */

/** Map a key to another. */
static struct map_rule {
    int const from_key; /** Map what? */
    int const to_key; /** To what? */
} MAP_RULES[] = {
#include "map-rules.h.in"
};

/** Bind actions to multiple keys.
 *
 * Take care of `down_press` and `up_press` to be balanced.
 */
static struct multi_rule {
    int const keys[8]; /** Keys to watch. */
    int const to_key;
    uint8_t keys_down; /** Bitmap of down `keys`. */
    bool triggered;
} MULTI_RULES[] = {

#include "multi-rules.h.in"

};

#define ARRAY_LEN(a) (int)(sizeof(a) / sizeof(*a))

#ifdef VERBOSE
# define dbgprintf(msg, ...) fprintf(stderr, msg "\n", ##__VA_ARGS__)
#else
# define dbgprintf(msg, ...) ((void)0)
#endif

enum event_values {
    EVENT_VALUE_KEYUP = 0,
    EVENT_VALUE_KEYDOWN = 1,
    EVENT_VALUE_KEYREPEAT = 2,
};

static struct input_event revbuf[MAX_EVENTS];
static size_t revlen = 0;
static size_t riev = 0;
static struct input_event wevbuf[MAX_EVENTS];
static size_t wevlen = 0;

static void flush_events(void) {
    if (wevlen == 0)
        return;

    for (;;) {
        switch (write(STDOUT_FILENO, wevbuf, sizeof *wevbuf * wevlen)) {
        case -1:
            if (errno == EINTR)
                continue;
            exit(EXIT_FAILURE);
        default:
            wevlen = 0;
            return;
        }
    }
}

static void write_event(struct input_event const *e) {
    if (e->type == EV_KEY) {
        dbgprintf("<   Code: %3d Value: %d", e->code, e->value);
    }

    wevbuf[wevlen] = *e;
    wevlen++;
    if (wevlen == MAX_EVENTS) flush_events();
}

static void read_events(void) {
    for (;;) {
        switch ((revlen = read(STDIN_FILENO, revbuf, sizeof revbuf))) {
        case -1:
            if (errno == EINTR)
                continue;
            /* Fall through. */
        case 0:
            exit(EXIT_FAILURE);
        default:
            revlen /= sizeof *revbuf, riev = 0;
            return;
        }
    }
}

static void write_key_event(int code, int value) {
    struct input_event e = {
        .type = EV_KEY,
        .code = code,
        .value = value
    };
    write_event(&e);
}

bool handle_key_event(struct input_event* e) {
    for (int i = 0; i < ARRAY_LEN(MAP_RULES); ++i) {
        struct map_rule *const v = &MAP_RULES[i];
        if (e->code == v->from_key) {
            dbgprintf("Map rule #%d: %d -> %d.", i, e->code, v->to_key);
            e->code = v->to_key;
            break;
        }
    }

    bool handled = false;
    for (int i = 0; i < ARRAY_LEN(MULTI_RULES); ++i) {
        struct multi_rule *const rule = &MULTI_RULES[i];
        int j;
        int ndown = 0; // how many keys are down
        int ntotal; // how many keys in this rule
        bool key_matches = false;

        for (j = 0; j < ARRAY_LEN(rule->keys) && rule->keys[j] != KEY_RESERVED; ++j) {
            if (e->code == rule->keys[j]) {
                key_matches = true;

                switch (e->value) {
                case EVENT_VALUE_KEYDOWN:
                    rule->keys_down |= 1 << j;
                    break;
                case EVENT_VALUE_KEYUP:
                    rule->keys_down &= ~(1 << j);
                    break;
                case EVENT_VALUE_KEYREPEAT:
                    break;
                }
            }
            ndown += (rule->keys_down >> j) & 1;
        }

        if (!key_matches) continue;
        ntotal = j;

        dbgprintf("triggered: %d, ntotal: %d, ndown: %d", rule->triggered, ntotal, ndown);

        if (!rule->triggered && e->value == EVENT_VALUE_KEYDOWN) {
            if (ndown == ntotal) {
                rule->triggered = true;
                for (int k = 0; rule->keys[k] != KEY_RESERVED; ++k) {
                    if ((rule->keys_down >> k) & 1) {
                        if (rule->keys[k] != e->code) {
                            write_key_event(rule->keys[k], EVENT_VALUE_KEYUP);
                        }
                    }
                }
                write_key_event(rule->to_key, EVENT_VALUE_KEYDOWN);
                handled = true;
            }
        }
        if (rule->triggered && e->value == EVENT_VALUE_KEYDOWN) {
            if (ndown == ntotal) {
                rule->triggered = true;
                write_key_event(rule->to_key, EVENT_VALUE_KEYDOWN);
                handled = true;
            }
        }

        if (rule->triggered && e->value == EVENT_VALUE_KEYREPEAT) {
            if (ndown == ntotal) {
                write_key_event(rule->to_key, EVENT_VALUE_KEYREPEAT);
                handled = true;
            }
        }

        if (rule->triggered && e-> value == EVENT_VALUE_KEYUP) {
            handled = true;
            if (ntotal - ndown == 1) {
                write_key_event(rule->to_key, EVENT_VALUE_KEYUP);
            }
        }
        if (rule->triggered && ndown == 0) {
            rule->triggered = false;
        }

    }
    return handled;
}

int main(void) {
    for (;;) {
        if (riev == revlen) {
            flush_events();
            read_events();
        }
        struct input_event e = revbuf[riev++];

        if (e.type == EV_MSC && e.code == MSC_SCAN) {
            /* We don't care about scan codes. */
            continue;
        }
        if (e.type != EV_KEY) {
            write_event(&e);
            continue;
        }
        dbgprintf("  > Code: %3d Value: %d", e.code, e.value);
        if (!handle_key_event(&e)) {
            write_event(&e);
        }
    }
}
