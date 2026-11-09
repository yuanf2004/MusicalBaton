#include "name_protocol.h"

bool baton_name_valid(const uint8_t *name, size_t len)
{
	if (name == NULL || len == 0 || len > BATON_NAME_MAX_BYTES ||
	    name[0] == ' ' || name[len - 1] == ' ') {
		return false;
	}
	for (size_t i = 0; i < len;) {
		size_t start = i;
		uint32_t cp = name[i++];
		uint32_t minimum = 0;
		size_t continuation = 0;

		if (cp >= 0xc2 && cp <= 0xdf) {
			cp &= 0x1f; continuation = 1; minimum = 0x80;
		} else if (cp >= 0xe0 && cp <= 0xef) {
			cp &= 0x0f; continuation = 2; minimum = 0x800;
		} else if (cp >= 0xf0 && cp <= 0xf4) {
			cp &= 0x07; continuation = 3; minimum = 0x10000;
		} else if (cp >= 0x80) {
			return false;
		}
		if (i + continuation > len) { return false; }
		for (size_t j = 0; j < continuation; j++) {
			uint8_t b = name[i++];
			if ((b & 0xc0) != 0x80) { return false; }
			cp = (cp << 6) | (b & 0x3f);
		}
		if (cp < minimum || cp > 0x10ffff ||
		    (cp >= 0xd800 && cp <= 0xdfff) || cp < 0x20 ||
		    (cp >= 0x7f && cp <= 0x9f) || cp == 0x2028 || cp == 0x2029) {
			return false;
		}
		bool whitespace = cp == 0x20 || cp == 0xa0 || cp == 0x1680 ||
			(cp >= 0x2000 && cp <= 0x200a) || cp == 0x202f ||
			cp == 0x205f || cp == 0x3000;
		if (whitespace && (start == 0 || i == len)) { return false; }
	}
	return true;
}
