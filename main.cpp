#include <iostream>
#include <asio.hpp>
#include <glog/logging.h>

using asio::ip::tcp;

class SocksClient : public std::enable_shared_from_this<SocksClient>
{
  asio::io_service &io_service;
  tcp::socket       socket;

public:

  tcp::socket &get_socket() { return socket; }

  SocksClient(asio::io_service &io)
    : io_service(io), socket(io)
  { }
  

  static std::shared_ptr<SocksClient> create(asio::io_service &io)
  {
    return std::make_shared<SocksClient>(io);
  }

  void start()
  {
    asio::async_write(socket, asio::buffer("Hello!\n"),
                      std::bind(&SocksClient::handle_write, shared_from_this()));
  }

  void handle_write()
  {
    LOG(INFO) << "Write completed.";
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

    auto server = SocksServer::create(io, 8080);
  
    io.run();
  } catch (std::system_error &e) {
    LOG(ERROR) << "Fatal error! " << e.what();
  }

  return 0;
}

// EOF
