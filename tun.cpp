#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <linux/if_tun.h>

#include <asio/write.hpp>
#include <asio/io_service.hpp>
#include <asio/deadline_timer.hpp>
#include <asio/posix/stream_descriptor.hpp>

#include <glog/logging.h>
#include <cstring>
#include <array>
#include <system_error>

#include <lwip/init.h>
#include <lwip/netif.h>
#include <lwip/ip.h>
#include <lwip/pbuf.h>
#include <lwip/timers.h>

#include "macgyvernet.hpp"

static int open_tun(const char *name)
{
  struct ifreq ifr;
  int fd, err;

  if ((fd = open("/dev/net/tun", O_RDWR)) < 0) {
    throw std::system_error(std::error_code(errno, std::system_category()), "open");
  }

  memset(&ifr, 0, sizeof(ifr));

  ifr.ifr_flags = IFF_TUN | IFF_NO_PI;

  strncpy(ifr.ifr_name, name, IFNAMSIZ);

  if ((err = ioctl(fd, TUNSETIFF, (void *) &ifr)) < 0) {
    close(fd);
    throw std::system_error(std::error_code(errno, std::system_category()), "ioctl");
  }

  LOG(INFO) << ifr.ifr_name << " opened.";

  return fd;
}

class TunInterface : public netif {

  asio::posix::stream_descriptor tun_fd;

  std::array<uint8_t, 1500> incoming_buffer;

  asio::deadline_timer timer;

  void read_cb(const asio::error_code &error, size_t len)
  {
    if (error) {
      LOG(ERROR) << "Error reading packet";
      return;
    }

    LOG(INFO) << "Got packet " << len;

    // XXX This could be optimized, if asio::buffer has some readv
    // like features. Need to check.

    pbuf *p = pbuf_alloc(PBUF_IP, len, PBUF_POOL);
    if (p) {
      uint8_t *data = incoming_buffer.data();

      // We allocated a buffer chain. Copy packet data.
      for (pbuf *c = p; c; c = c->next) {
        memcpy(c->payload, data, c->len);
        data += c->len;
      }

      input(p, this);
    } else {
      LOG(ERROR) << "Dropped packet, because no pbuf was available.";
    }

    tun_fd.async_read_some(asio::buffer(incoming_buffer), ASIO_CB(read_cb));
  }

  err_t netif_init()
  {
    CHECK_EQ(state, this);

    name[0] = 't';
    name[1] = 'u';
    output = &TunInterface::static_packet_output;

    tun_fd.async_read_some(asio::buffer(incoming_buffer), ASIO_CB(read_cb));

    return ERR_OK;
  }

  err_t packet_output(netif *netif, pbuf *p, ip_addr_t const *ipaddr)
  {
    CHECK_EQ(netif, this);

    // Mark buffer as still being in use.
    pbuf_ref(p);

    std::vector<asio::const_buffer> gather_list;

    for (pbuf *c = p; c; c = c->next) {
      gather_list.emplace_back(c->payload, c->len);
    }

    LOG(INFO) << "lwIP sends " << int(p->tot_len) << " bytes in " << gather_list.size() << " buffers.";

    asio::async_write(tun_fd, gather_list,
                      [p] (const asio::error_code &error, size_t len) {
                        if (error) {
                          LOG(ERROR) << "Error while sending packet: " << error;
                        }
                        pbuf_free(p);
                      });

    return ERR_OK;
  }

public:
  TunInterface(asio::io_service &io, int fd)
    : tun_fd(io, fd), timer(io)
  {
    memset(static_cast<netif *>(this), 0, sizeof(netif));
  }

  static err_t static_netif_init(netif *netif)
  {
    return static_cast<TunInterface *>(netif)->netif_init();
  }

  static err_t static_packet_output(netif *netif, pbuf *p, ip_addr_t const *ipaddr)
  {
    return static_cast<TunInterface *>(netif)->packet_output(netif, p, ipaddr);
  }

  void start_timer()
  {
    timer.expires_from_now(boost::posix_time::milliseconds(100));
    timer.async_wait([this] (const asio::error_code &err) {
        if (err) {
          LOG(ERROR) << "Timer error: " << err;
          return;
        }

        sys_check_timeouts();
        start_timer();
      });
  }
};

void initialize_backend(asio::io_service &io)
{
  int fd = open_tun("lwip0");
  CHECK(fd >= 0);

  static TunInterface tunif { io, fd };

  lwip_init();


  LOG(INFO) << "lwIP initialized. Version: " << std::hex << LWIP_VERSION;

  ip_addr_t ipaddr, netmask, gw;

  IP4_ADDR(&gw, 10,0,0,1);
  IP4_ADDR(&ipaddr, 10,0,0,100);
  IP4_ADDR(&netmask, 255,0,0,0);

  netif_add(&tunif, &ipaddr, &netmask, &gw, &tunif,
            &TunInterface::static_netif_init, ip4_input);

  netif_set_default(&tunif);
  netif_set_up(&tunif);
  netif_set_link_up(&tunif);

  tunif.start_timer();

}

// EOF
