/* Copyright (c) 2015 Diego Ongaro
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR(S) DISCLAIM ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL AUTHORS BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <algorithm>
#include <cstdlib>
#include <ctype.h>
#include <errno.h>
#include <functional>
#include <stdexcept>

#include "Examples/Util.h"

namespace LogCabin {
namespace Examples {
namespace Util {

uint64_t
parseTime(const std::string& description)
{
    const char* start = description.c_str();
    char* end = NULL;
    errno = 0;
    uint64_t r = strtoul(start, &end, 10);
    if (errno != 0 || start == end) {
        throw std::runtime_error(
            std::string("Invalid time description: "
                        "could not parse number from ") + description);
    }

    std::string units = end;
    // The black magic is from https://stackoverflow.com/a/217605
    // trim whitespace at end of string
    units.erase(std::find_if(units.rbegin(), units.rend(),
                         std::not1(std::ptr_fun<int, int>(std::isspace)))
                .base(),
            units.end());
    // trim whitespace at beginning of string
    units.erase(units.begin(),
            std::find_if(units.begin(), units.end(),
                         std::not1(std::ptr_fun<int, int>(std::isspace))));

    if (units == "ns") {
        // pass
    } else if (units == "us") {
        r *= 1000UL;
    } else if (units == "ms") {
        r *= 1000000UL;
    } else if (units == "s" || units == "") {
        r *= 1000000000UL;
    } else {
        throw std::runtime_error(
            std::string("Invalid time description: "
                        "could not parse units from ") + description);
    }
    return r;
}

} // namespace LogCabin::Examples::Util
} // namespace LogCabin::Examples
} // namespace LogCabin
