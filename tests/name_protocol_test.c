#include "name_protocol.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    const char *valid[] = {"Smart Baton", "Orchestra 2", "12345678901234567890", "Bâton", "🎵 Baton"};
    for (size_t i = 0; i < sizeof(valid)/sizeof(valid[0]); i++) {
        assert(baton_name_valid((const uint8_t *)valid[i], strlen(valid[i])));
    }
    const char *invalid[] = {"", " ", " name", "name ", "123456789012345678901", "a\nb", "a\tb", "a\x7f", "a\xc2\x85", "\xed\xa0\x80", "\xc0\xaf", "\xf4\x90\x80\x80", "\xe2\x80\xa8", "\x80", "\xf0\x9f"};
    for (size_t i = 0; i < sizeof(invalid)/sizeof(invalid[0]); i++) {
        assert(!baton_name_valid((const uint8_t *)invalid[i], strlen(invalid[i])));
    }
    const uint8_t nul[] = {'a', 0, 'b'};
    assert(!baton_name_valid(nul, sizeof(nul)));
    assert(!baton_name_valid(NULL, 1));
    puts("Device-name UTF-8, byte limits, whitespace, controls and malformed encoding checks passed");
}
