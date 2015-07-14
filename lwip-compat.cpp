#include <cstdarg>
#include <arch/cc.h>
#include <lwip/sys.h>
#include <glog/logging.h>
#include <chrono>

void lwip_platform_diag(const char *m, ...)
{
  va_list ap;
  va_start(ap, m);

  size_t len = vsnprintf(nullptr, 0, m, ap);
  char   buf[len];

  vsnprintf(buf, len, m, ap);

  LOG(WARNING) << buf;

  va_end(ap);
}

void lwip_platform_assert(const char *file, int line, const char *msg)
{
  LOG(ERROR) << "[" << file << ":" << line << "] " << msg;
  abort();
}

u32_t sys_now()
{
  static auto boot_time = std::chrono::steady_clock::now();

  auto now = std::chrono::steady_clock::now();
  return std::chrono::duration_cast<std::chrono::microseconds>(now - boot_time).count();
}

/* EOF */
