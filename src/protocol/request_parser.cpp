#include "protocol/request_parser.h"

#include "protocol/inline_parser.h"
#include "protocol/resp.h"

namespace miniredis {

ParseResult parse_request(std::string_view input) {
    if (!input.empty() && input.front() == '*') {
        return parse_resp(input);
    }
    return parse_inline(input);
}

}  // namespace miniredis
