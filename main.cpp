#include <iostream>
#include <asio.hpp>
#include <glog/logging.h>

#include <lwip/tcpip.h>
#include <lwip/tcp.h>

#include "macgyvernet.hpp"
#include "logo.hpp"

using asio::ip::tcp;

class SocksClient final : public std::enable_shared_from_this<SocksClient>
{
  using self_t = std::shared_ptr<SocksClient>;

  asio::io_service &io_service;

  // This is the socket that is connected to the SOCKS client;
  tcp::socket socket;

  // lwIP's connection identifier.
  struct tcp_pcb *tcp_pcb = nullptr;

  struct PointerWrap {
    std::shared_ptr<SocksClient> ptr;
  };

  PointerWrap *tcp_pcb_arg = nullptr;

  enum {
    // We need to read this many bytes from a commmand to figure out
    // how long it is.
    INITIAL_COMMAND_BYTES = 5,

    // At this position in a command packet does the address start.
    ADDRESS_START_OFFSET = 4,
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

  // IF true, an async_read is in progress.
  bool async_read_in_progress = false;

  // Set to true, if tcp_close was called.
  bool close_in_progress = false;

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

  void handle_connect_by_name()
  {

    const char *n = reinterpret_cast<const char *>(rcv_buffer.data() + ADDRESS_START_OFFSET + 1);
    std::string name (n, rcv_buffer.at(ADDRESS_START_OFFSET));
    LOG(INFO) << "Name: " << name;

    LOG(ERROR) << "XXX Implement connect by name";
  }

  void connection_close()
  {
    assert(tcp_pcb);

    tcp_arg (tcp_pcb, nullptr);
    tcp_err (tcp_pcb, nullptr);
    tcp_sent(tcp_pcb, nullptr);

    auto old_tcp_pcb = tcp_pcb;
    tcp_pcb = nullptr;

    /// XXX How do we make sure that noone touches rcv_buffer after
    /// this SocksClient instance has been destroyed?
    tcp_close(old_tcp_pcb);

    close_in_progress = true;
    socket.cancel();
    socket.close();
  }

  /// Same as connection_hard_abort, but can be used in the
  /// destructor.
  void _connection_hard_abort()
  {
    asio::error_code ec { asio::error::operation_aborted };
    socket.close(ec);

    if (tcp_pcb) {
      auto pcb = tcp_pcb;
      tcp_pcb = nullptr;
      tcp_abort(pcb);
    }

    auto *arg = tcp_pcb_arg;
    if (arg) {
      tcp_pcb_arg = nullptr;
      delete arg;
    }

  }

  /// Tells lwIP to abort the connection. If this is called from lwip
  /// event handlers the return value needs to be ERR_ABRT to prevent
  /// double frees.
  void connection_hard_abort()
  {
    // Protect `this' from disappearing.
    auto sthis = shared_from_this();
    _connection_hard_abort();
  }

  void data_received_cb(const asio::error_code &error, size_t len)
  {
    async_read_in_progress = false;

    LOG(INFO) << "Received " << len << " bytes from SOCKS client. sndbuf is " << tcp_sndbuf(tcp_pcb);

    // This can only happen if we start asynchronous reads for sndbuf
    // space we don't have.
    assert(len <= tcp_sndbuf(tcp_pcb));

    if (len) {
      // We pass the copy flag to avoid lwIP touch rcv_buffer, after we've destrpyed this instance.
      err_t err = tcp_write(tcp_pcb, rcv_buffer.data(), len, TCP_WRITE_FLAG_COPY);
      if (err != ERR_OK) {
        LOG(ERROR) << "Couldn't send. tcp_write() returned: " << int(err);
        return;
      }
    }

    if (close_in_progress) {
      LOG(INFO) << "Stop waiting for data from SOCKS client.";
      return;
    }

    if (error == asio::error_code(asio::error::misc_errors::eof)) {
      LOG(INFO) << "EOF. Closing connection.";
      connection_close();
      return;
    } else if (error == asio::error_code(asio::error::operation_aborted)) {
      LOG(ERROR) << "async_read aborted.";
      return;
    } else if (error) {
      LOG(ERROR) << "Error while receiving data from SOCKS client: " << error.message();
      // XXX When we get EOF, shutdown the TCP connection gracefully.
      connection_hard_abort();
      return;
    }

    // New async read with as many bytes as we can actually send.
    size_t buflen = std::min<size_t>(rcv_buffer.size(), tcp_sndbuf(tcp_pcb));
    LOG(INFO) << "Can send " << buflen << " bytes.";

    if (buflen) {
      // Wait for more data.
      auto self = shared_from_this();
      async_read_in_progress = true;
      asio::async_read(socket, asio::buffer(rcv_buffer.begin(), buflen),
                       ASIO_CB_SHARED(self, data_received_cb));
    }
  }

  void connect_success_written_cb(const asio::error_code &error, size_t)
  {
    if (error) {
      LOG(ERROR) << "Error while sending CONNECT response: " << error.message();
      connection_hard_abort();
      return;
    }

    // Wait for data.
    asio::error_code ec;
    data_received_cb(ec, 0);
  }


  err_t lwip_connected_cb(struct tcp_pcb *pcb, err_t err)
  {
    assert(pcb == tcp_pcb);

    if (err != ERR_OK) {
      LOG(ERROR) << "Connection failed: " << int(err);
      connection_hard_abort();
      return ERR_ABRT;
    }

    LOG(INFO) << "Connected.";

    static char connect_response[10] = { SOCKS_VERSION, 0 };

    auto self = shared_from_this();
    asio::async_write(socket, asio::buffer(connect_response, sizeof(connect_response)),
                      ASIO_CB_SHARED(self, connect_success_written_cb));

    return ERR_OK;
  }

  err_t lwip_tcp_sent_cb(struct tcp_pcb *pcb, uint16_t len)
  {
    LOG(INFO) << "Remote ACK'd " << int(len) << " bytes.";
    assert(pcb == tcp_pcb);

    // If there is an async_read in progress, we don't need to do
    // anything here, because when it is done, it will program a new
    // read. If there is no read in progress, we need to start a new
    // one here.
    //
    // Also make sure this is called from our IO thread, otherwise
    // this will race.

    if (not async_read_in_progress) {
      LOG(INFO) << "Starting new async_read, because none was in progress.";
      asio::error_code ec;
      data_received_cb(ec, 0);
    } else {
      LOG(INFO) << "Not starting new async_read.";
    }

    return ERR_OK;
  }

  static err_t static_lwip_connected_cb(void *arg, struct tcp_pcb *pcb, err_t err)
  {
    auto *wrap = static_cast<PointerWrap *>(arg);
    return wrap->ptr->lwip_connected_cb(pcb, err);
  }

  void lwip_err_cb(err_t err)
  {
    LOG(ERROR) << "Error callback from lwIP: '" << lwip_strerr(err) << "' " << int(err);

    if (tcp_pcb) {
      connection_hard_abort();
    }
  }

  static void static_lwip_err_cb(void *arg, err_t err)
  {
    auto *wrap = static_cast<PointerWrap *>(arg);
    wrap->ptr->lwip_err_cb(err);
  }

  static err_t static_lwip_tcp_sent_cb(void *arg, struct tcp_pcb *pcb, uint16_t len)
  {
    auto *wrap = static_cast<PointerWrap *>(arg);
    return wrap->ptr->lwip_tcp_sent_cb(pcb, len);
  }

  /// Allocate a lwIP PCB and configure it with handler functions.
  bool ensure_tcp_pcb()
  {
    // Allocate new PCB from lwIP;
    if ((tcp_pcb = tcp_new()) == nullptr) {
      LOG(ERROR) << "lwIP out of memory. Couldn't allocate TCP PCB.";
      return false;
    }

    // Create a shared_ptr pointing to this client that will keep the
    // client alive as long as lwIP has the connection open.
    tcp_pcb_arg = new PointerWrap { shared_from_this() };

    tcp_arg (tcp_pcb, tcp_pcb_arg);
    tcp_err (tcp_pcb, static_lwip_err_cb);
    tcp_sent(tcp_pcb, static_lwip_tcp_sent_cb);

    return true;
  }

  void handle_connect_by_ipv4()
  {
    ensure_tcp_pcb();

    ip_addr_t ip_addr;

    memcpy(&ip_addr, rcv_buffer.data() + ADDRESS_START_OFFSET, sizeof(ip_addr.addr));

    uint16_t port = rcv_buffer.at(ADDRESS_START_OFFSET + 4) << 8 | rcv_buffer.at(ADDRESS_START_OFFSET + 5);

    LOG(INFO) << "Connecting to " << std::hex << ip_addr.addr << " port " << port;

    err_t err = tcp_connect(tcp_pcb, &ip_addr, port, static_lwip_connected_cb);

    if (err != ERR_OK) {
      LOG(ERROR) << "tcp_connect failed with " << lwip_strerr(err) << " " << int(err);
      connection_hard_abort();
    }
  }

  void handle_connect()
  {
    ADDRESS_TYPE at = ADDRESS_TYPE(rcv_buffer.at(3));

    switch (at) {
    case ADDRESS_TYPE::DOMAINNAME:
      handle_connect_by_name();
      break;
    case ADDRESS_TYPE::IPV4:
      handle_connect_by_ipv4();
      break;
    default:
      LOG(ERROR) << "Address type " << at << " not supported.";
      break;
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

    switch (cmd) {
    case COMMAND::CONNECT:
      handle_connect();
      break;

    default:
      LOG(ERROR) << "Can't handle command.";
      break;
    }
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

public:

  tcp::socket &get_socket() { return socket; }

  SocksClient(asio::io_service &io)
    : io_service(io), socket(io)
  { }

  static self_t create(asio::io_service &io)
  {
    return std::make_shared<SocksClient>(io);
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
    _connection_hard_abort();
    LOG(INFO) << "Connection terminated.";
  }
};

/// Handles accepting connections and creates a SocksClient instance
/// for each connection.
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
  LOG(INFO) << "When your corporate VPN policy sucks, you turn to...\n" << logo << "\n";

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
