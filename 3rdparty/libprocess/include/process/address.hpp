#ifndef __PROCESS_ADDRESS_HPP__
#define __PROCESS_ADDRESS_HPP__

#include <stdint.h>
#include <unistd.h>

#include <arpa/inet.h>

#include <glog/logging.h>

#include <sstream>

#include <boost/functional/hash.hpp>

#include <stout/abort.hpp>
#include <stout/ip.hpp>
#include <stout/stringify.hpp>

namespace process {
namespace network {

// Represents a network "address", subsuming the struct addrinfo and
// struct sockaddr* that typically is used to encapsulate IP and port.
//
// TODO(benh): Create a Family enumeration to replace sa_family_t.
// TODO(jieyu): Move this class to stout.
class Address
{
public:
  Address() : ip(INADDR_ANY), port(0) {}

  Address(const net::IP& _ip, uint16_t _port) : ip(_ip), port(_port) {}

  static Try<Address> create(const struct sockaddr_storage& storage)
  {
    switch (storage.ss_family) {
       case AF_INET: {
         struct sockaddr_in addr = *(struct sockaddr_in*) &storage;
         return Address(net::IP(addr.sin_addr), ntohs(addr.sin_port));
       }
       default: {
         return Error(
             "Unsupported family type: " +
             stringify(storage.ss_family));
       }
     }
  }

  int family() const
  {
    return ip.family();
  }

  // Returns the storage size (i.e., either sizeof(sockaddr_in) or
  // sizeof(sockaddr_in6) depending on the family) of this address.
  size_t size() const
  {
    switch (family()) {
      case AF_INET:
        return sizeof(sockaddr_in);
      default:
        ABORT("Unsupported family type: " + stringify(family()));
    }
  }

  bool operator < (const Address& that) const
  {
    if (ip == that.ip) {
      return port < that.port;
    } else {
      return ip < that.ip;
    }
  }

  bool operator > (const Address& that) const
  {
    if (ip == that.ip) {
      return port > that.port;
    } else {
      return ip > that.ip;
    }
  }

  bool operator == (const Address& that) const
  {
    return (ip == that.ip && port == that.port);
  }

  bool operator != (const Address& that) const
  {
    return !(*this == that);
  }

  net::IP ip;
  uint16_t port;
};


inline std::ostream& operator << (std::ostream& stream, const Address& address)
{
  stream << address.ip << ":" << address.port;
  return stream;
}


// Address hash value (for example, to use in Boost's unordered maps).
inline std::size_t hash_value(const Address& address)
{
  size_t seed = 0;
  boost::hash_combine(seed, address.ip);
  boost::hash_combine(seed, address.port);
  return seed;
}

} // namespace network {
} // namespace process {

#endif // __PROCESS_ADDRESS_HPP__
