#include <kernel/time.h>

const char *witty_comments[] = {
    "Surprise! Haha. Well, this is awkward.",
    "Oh - I know what I did wrong!",
    "Uhh... Did I do that?",
    "But it works on my machine.",
    "Typo in the code.",
    "Oops.",
    "System consumed all the paper for paging!",
    "I have no idea what I'm doing. But neither do you, right?",
    "Something went wrong. But hey, at least you're still alive... probably.",
    "Segmentation fault? More like GDT Init... FAIL.",
    "Abort, Retry, Fail?",
    "Not ready reading drive A.",
    "Your last commit broke everything. Again.",
    "Undefined behavior detected.",
    "Compiler said 'good luck'."
};

const char *witty(void) {
    size_t nsec, usec;
    uptime(NULL, &nsec);
    usec = nsec / 1000;
    size_t count = sizeof(witty_comments) / sizeof(*witty_comments);
    return witty_comments[usec % count];
}