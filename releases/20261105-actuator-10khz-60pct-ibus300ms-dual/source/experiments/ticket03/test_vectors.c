#include "exp3.h"
#include <assert.h>
#include <stdio.h>
int main(void) {
    unsigned kind, i;
    Exp3Message m = {0};
    uint8_t b[36];
    m.controller = 0x11223344;
    m.hmi = 0x55667788;
    m.request = 0x12345678;
    m.action = 0x90abcdef;
    m.context = 0x43210098;
    m.generation = 3;
    m.target = 0x102;
    for (kind = 1; kind <= 4; kind++) {
        m.kind = (uint8_t)kind;
        m.sender = kind == 4 ? 2 : 1;
        m.status = kind == 4 ? EXP3_CANCELLED : 0;
        m.command = kind == 4 ? EXP3_QUERY : 0;
        assert(exp3_encode(&m, b));
        for (i = 0; i < 36; i++)
            printf("%02x", b[i]);
        puts("");
    }
}
