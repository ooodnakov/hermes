#pragma once

#include <cstring>

namespace protocol {

// Hermes has sent speech as both {type:say,url:...} and the older
// {type:say,say:...}. Notifications carry an optional top-level say URL.
inline const char* speechUrlForFrame(const char* type, const char* url,
                                    const char* say) {
  if (!type) return nullptr;
  if (!strcmp(type, "say")) {
    if (url && url[0]) return url;
    return say && say[0] ? say : nullptr;
  }
  if (!strcmp(type, "notify")) return say && say[0] ? say : nullptr;
  return nullptr;
}

}  // namespace protocol
