
import os

path = r'C:\Users\Deepali\Desktop\OS DEV 18-03-2026\Retro-Os-MATTERS HTTPS ACCESSIBLE\edge264_port\edge264.c'

with open(path, 'r') as f:
    content = f.read()

# Replace edge264_find_start_code with a scalar-friendly version
# We'll search for the function signature and replace the whole thing.

scalar_find_start_code = """
const uint8_t *edge264_find_start_code(const uint8_t *buf, const uint8_t *end, int four_byte) {
	four_byte = four_byte != 0;
	buf += four_byte;
	while (buf + 3 <= end) {
		if (buf[0] == 0 && buf[1] == 0 && buf[2] == 1)
			return buf;
		buf++;
	}
	return end;
}
"""

import re
pattern = r'const uint8_t \*edge264_find_start_code\(const uint8_t \*buf, const uint8_t \*end, int four_byte\) \{.*?^\}'
content = re.sub(pattern, scalar_find_start_code, content, flags=re.DOTALL | re.MULTILINE)

# Also fix the time64 conflict if possible by adding a guard
content = content.replace('#include "edge264_internal.h"', '#define _TIME64_T_DEFINED\\n#include "edge264_internal.h"')

with open(path, 'w') as f:
    f.write(content)

print("Patching complete.")
