#include <iostream>
#include <asio.hpp>

int main()
{
  static asio::io_service io;
  io.run();
  return 0;
}

// EOF
