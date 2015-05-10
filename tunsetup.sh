ip tuntap add dev lwip0 mode tun user julian
ip addr add 10.0.0.1 dev lwip0
ip link set dev lwip0 up
ip route add 10.0.0.0/24 dev lwip0
