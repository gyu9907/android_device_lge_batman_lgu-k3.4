#ifndef BATMAN_CAMERA_AREA_H
#define BATMAN_CAMERA_AREA_H

#include <string>

// Legacy area tuples contain spaces after commas. Camera.Parameters expects
// compact integers. Only trim token-boundary whitespace; malformed numbers
// such as "1 0" must not silently turn into a different valid coordinate.
static inline bool cameraAreaSpace(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' ||
            c == '\f' || c == '\v';
}

static inline bool cameraAreaDelimiter(char c)
{
    return c == '(' || c == ')' || c == ',';
}

static inline std::string cameraCompactArea(const char *area)
{
    std::string result;
    for (const char *p = area; *p;) {
        if (!cameraAreaSpace(*p)) {
            result += *p++;
            continue;
        }
        const char *begin = p;
        while (cameraAreaSpace(*p)) ++p;
        if (!result.empty() && *p &&
                !cameraAreaDelimiter(result.back()) &&
                !cameraAreaDelimiter(*p)) {
            result.append(begin, p - begin);
        }
    }
    return result;
}

#endif
