#include <iostream>
#include <asio.hpp>
#include <glog/logging.h>

#include "sockslwip.hpp"

using asio::ip::tcp;

class SocksClient final : public std::enable_shared_from_this<SocksClient>
{
  using self_t = std::shared_ptr<SocksClient>;

  asio::io_service &io_service;

  // This is the socket that is connected to the SOCKS client;
  tcp::socket socket;

  enum {
    // We need to read this many bytes from a commmand to figure out
    // how long it is.
    INITIAL_COMMAND_BYTES = 5,
  };

  enum VERSION : uint8_t {
    SOCKS_VERSION = 5
  };

  enum AUTH_METHOD : uint8_t {
    NO_AUTHENTICATION = 0,
    NO_ACCEPTABLE     = 0xFF,
  };

  enum COMMAND : uint8_t {
    CONNECT       = 1,
    BIND          = 2,
    UDP_ASSOCIATE = 3,
  };

  enum ADDRESS_TYPE : uint8_t {
    IPV4       = 1,
    DOMAINNAME = 3,
    IPV6       = 4,
  };

  // Contains incoming packet data
  std::array<uint8_t, 1 << 16> rcv_buffer;

public:

  tcp::socket &get_socket() { return socket; }

  SocksClient(asio::io_service &io)
    : io_service(io), socket(io)
  { }


  static self_t create(asio::io_service &io)
  {
    return std::make_shared<SocksClient>(io);
  }

  static const char *command_string(COMMAND c)
  {
    switch (c) {
    case CONNECT:       return "CONNECT";
    case BIND:          return "BIND";
    case UDP_ASSOCIATE: return "UDP";
    default:            return "unknown";
    }
  }

  static const char *address_type_string(ADDRESS_TYPE t)
  {
    switch (t) {
    case IPV4:       return "IPv4";
    case DOMAINNAME: return "domain name";
    case IPV6:       return "IPv6";
    default:         return "unknown";
    }
  }

  void command_received_cb(const asio::error_code &error, size_t len)
  {
    if (error) {
      LOG(ERROR) << "Error while receiving command header: " << error;
      return;
    }

    auto self = shared_from_this();

    COMMAND      cmd = COMMAND(rcv_buffer.at(1));
    ADDRESS_TYPE at  = ADDRESS_TYPE(rcv_buffer.at(3));

    LOG(INFO) << "Command '" << command_string(cmd) << "' Address '" << address_type_string(at) << "'";

    if (at != DOMAINNAME) {
      LOG(ERROR) << "Only domain names supported for now.";
      return;
    }

    const char *n = reinterpret_cast<const char *>(rcv_buffer.data() + INITIAL_COMMAND_BYTES);
    std::string name (n, rcv_buffer.at(INITIAL_COMMAND_BYTES - 1));
    LOG(INFO) << "Name: " << name;

  }

  void read_command_first_cb(const asio::error_code &error, size_t len)
  {
    if (error) {
      LOG(ERROR) << "Error while receiving command header: " << error;
      return;
    }

    CHECK_EQ(len, INITIAL_COMMAND_BYTES);

    auto self = shared_from_this();
    uint8_t version = rcv_buffer.at(0);

    if (version != SOCKS_VERSION) {
      LOG(ERROR) << "Client specified wrong SOCKS version: " << int(version);
      return;
    }

    // Read address type first to figure out how long this packet is.
    size_t plen = 2;
    ADDRESS_TYPE at = ADDRESS_TYPE(rcv_buffer.at(3));

    switch (at) {
    case IPV4:       plen += 3;                    break;
    case DOMAINNAME: plen += rcv_buffer.at(4);     break;
    case IPV6:       plen += 15;                   break;
    default:
      // Not supported. Proper reply will be sent in command_received_cb.
      break;
    };

    CHECK(INITIAL_COMMAND_BYTES + plen < rcv_buffer.size());

    // Wait for rest of command packet.
    asio::async_read(socket, asio::buffer(rcv_buffer.begin() + INITIAL_COMMAND_BYTES, plen),
                     ASIO_CB_SHARED(self, command_received_cb));
  }

  // Called when we have successfully replied to the client's auth
  // packet.
  void version_written_cb(const asio::error_code &error, size_t len)
  {
    if (error) {
      LOG(ERROR) << "Error while sending greeting: " << error;
      return;
    }

    auto self = shared_from_this();

    // Wait for command packet.
    asio::async_read(socket, asio::buffer(rcv_buffer, INITIAL_COMMAND_BYTES),
                     ASIO_CB_SHARED(self, read_command_first_cb));
  }

  // The client has sent his list of authentication methods.
  void methods_received_cb(const asio::error_code &error, size_t len)
  {
    if (error) {
      LOG(ERROR) << "Error reading auth methods from client: " << error;
      return;
    }

    auto self = shared_from_this();

    CHECK_EQ(rcv_buffer.at(1), len);

    for (size_t i = 0; i < len; i++) {
      uint8_t method = rcv_buffer.at(2 + i);

      LOG(INFO) << "Method: " << int(method);

      if (method == NO_AUTHENTICATION) {
        LOG(INFO) << "Selected no authentication.";
        static uint8_t version_response[] { SOCKS_VERSION, NO_AUTHENTICATION };

        asio::async_write(socket, asio::buffer(version_response, sizeof(version_response)),
                          ASIO_CB_SHARED(self, version_written_cb));
        return;
      }

      LOG(ERROR) << "We don't understand any auth method. Closing connection.";
    }
  }

  void hello_received_cb(const asio::error_code &error, size_t len)
  {
    if (error) {
      LOG(ERROR) << "Error reading hello from client: " << error;
      return;
    }

    auto self = shared_from_this();

    CHECK_EQ(len, 2);

    uint8_t client_version = rcv_buffer.at(0);
    uint8_t methods        = rcv_buffer.at(1);

    LOG(INFO) << "Client wants version " << int(client_version) << " with "
              << int(methods) << " authentication methods.";

    if (client_version != SOCKS_VERSION) {
      LOG(ERROR) << "Invalid version from client. Disconnecting.";
      return;
    }

    // Read method data.
    CHECK(rcv_buffer.size() >= 2 + methods);
    asio::async_read(socket, asio::buffer(rcv_buffer.begin() + 2, methods),
                     ASIO_CB_SHARED(self, methods_received_cb));
  }


  void start()
  {
    auto self = shared_from_this();


    // We expect a version and authentication method packet first. We
    // receive this in two parts. First the two-byte header and the
    // methods data.

    asio::async_read(socket, asio::buffer(rcv_buffer, 2), ASIO_CB_SHARED(self, hello_received_cb));
  }

  ~SocksClient() {
    LOG(INFO) << "Connection terminated.";
  }
};

class SocksServer : public std::enable_shared_from_this<SocksServer>
{
  asio::io_service &io_service;
  tcp::acceptor acceptor;

public:

  SocksServer(asio::io_service &io_service, int port)
    : io_service(io_service), acceptor(io_service, tcp::endpoint(tcp::v4(), port))

  {
    acceptor.set_option(tcp::acceptor::reuse_address(true));
    start_accept();
  }

  void start_accept()
  {
    auto conn = SocksClient::create(acceptor.get_executor().context());

    acceptor.async_accept(conn->get_socket(),
                          [this, conn] (const asio::error_code &error) {
                            handle_accept(conn, error);
                          });
  }

  void handle_accept(std::shared_ptr<SocksClient> conn,
                     const asio::error_code& error)
  {
    if (not error) {
      LOG(INFO) << "Accepted connection.";
      conn->start();
    } else {
      LOG(ERROR) << "Accepting connection failed.";
    }

    start_accept();
  }

  static std::shared_ptr<SocksServer> create(asio::io_service &io, int port)
  {
    return std::make_shared<SocksServer>(io, port);
  }

};

int main(int argc, char **argv)
{
  google::InitGoogleLogging(argv[0]);

  // Log to stderr for now.
  FLAGS_logtostderr = 1;
  LOG(INFO) << "Starting...";

  try {
    static asio::io_service io;

    // This initializes lwIP.
    initialize_backend(io);

    auto server = SocksServer::create(io, 8080);

    io.run();
  } catch (std::system_error &e) {
    LOG(ERROR) << "Fatal error! " << e.what();
  }

  return 0;
}

// EOF
