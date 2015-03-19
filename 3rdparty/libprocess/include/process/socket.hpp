#ifndef __PROCESS_SOCKET_HPP__
#define __PROCESS_SOCKET_HPP__

#include <memory>

#include <process/address.hpp>
#include <process/future.hpp>

#include <stout/abort.hpp>
#include <stout/nothing.hpp>
#include <stout/os.hpp>
#include <stout/try.hpp>

namespace process {
namespace network {

// An abstraction around a socket (file descriptor) that provides
// reference counting such that the socket is only closed (and thus,
// has the possiblity of being reused) after there are no more
// references.
class Socket
{
public:
  // Available kinds of implementations.
  enum Kind {
    POLL,
    // TODO(jmlvanre): Add libevent SSL socket.
  };

  // Returns an instance of a Socket using the specified kind of
  // implementation and potentially wrapping the specified file
  // descriptor.
  static Try<Socket> create(Kind kind = DEFAULT_KIND(), int s = -1);

  // Returns the default kind of implementation of Socket.
  static const Kind& DEFAULT_KIND();

  // Each socket is a reference counted, shared by default, concurrent
  // object. However, since we want to support multiple
  // implementations we use the Pimpl pattern (often called the
  // compilation firewall) rather than forcing each Socket
  // implementation to do this themselves.
  class Impl : public std::enable_shared_from_this<Impl>
  {
  public:
    virtual ~Impl()
    {
      CHECK(s >= 0);
      Try<Nothing> close = os::close(s);
      if (close.isError()) {
        ABORT("Failed to close socket " +
              stringify(s) + ": " + close.error());
      }
    }

    int get() const
    {
      return s;
    }

    // Socket::Impl interface.
    virtual Try<Address> address() const;
    virtual Try<Address> bind(const Address& address);
    virtual Try<Nothing> listen(int backlog) = 0;
    virtual Future<Socket> accept() = 0;
    virtual Future<Nothing> connect(const Address& address) = 0;
    virtual Future<size_t> recv(char* data, size_t size) = 0;
    virtual Future<size_t> send(const char* data, size_t size) = 0;
    virtual Future<size_t> sendfile(int fd, off_t offset, size_t size) = 0;

    // An overload of 'recv', receives data based on the specified
    // 'size' parameter:
    //
    //   Value of 'size'   |    Semantics
    // --------------------|-----------------
    //          0          |  Returns an empty string.
    //          -1         |  Receives until EOF.
    //          N          |  Returns a string of size N.
    //        'None'       |  Returns a string of the available data.
    //
    // That is, if 'None' is specified than whenever data becomes
    // available on the socket that much data will be returned.
    //
    // TODO(benh): Consider returning Owned<std::string> or
    // Shared<std::string>, the latter enabling reuse of a pool of
    // preallocated strings/buffers.
    virtual Future<std::string> recv(const Option<ssize_t>& size = None());

    // An overload of 'send', sends all of the specified data unless
    // sending fails in which case a failure is returned.
    //
    // TODO(benh): Consider taking Shared<std::string>, the latter
    // enabling reuse of a pool of preallocated strings/buffers.
    virtual Future<Nothing> send(const std::string& data);

  protected:
    explicit Impl(int _s) : s(_s) { CHECK(s >= 0); }

    // Construct a Socket wrapper from this implementation.
    Socket socket() { return Socket(shared_from_this()); }

    int s;
  };

  bool operator == (const Socket& that) const
  {
    return impl == that.impl;
  }

  operator int () const
  {
    return impl->get();
  }

  Try<Address> address() const
  {
    return impl->address();
  }

  int get() const
  {
    return impl->get();
  }

  Try<Address> bind(const Address& address)
  {
    return impl->bind(address);
  }

  Try<Nothing> listen(int backlog)
  {
    return impl->listen(backlog);
  }

  Future<Socket> accept()
  {
    return impl->accept();
  }

  Future<Nothing> connect(const Address& address)
  {
    return impl->connect(address);
  }

  Future<size_t> recv(char* data, size_t size) const
  {
    return impl->recv(data, size);
  }

  Future<size_t> send(const char* data, size_t size) const
  {
    return impl->send(data, size);
  }

  Future<size_t> sendfile(int fd, off_t offset, size_t size) const
  {
    return impl->sendfile(fd, offset, size);
  }

  Future<std::string> recv(const Option<ssize_t>& size)
  {
    return impl->recv(size);
  }

  Future<Nothing> send(const std::string& data)
  {
    return impl->send(data);
  }

private:
  explicit Socket(std::shared_ptr<Impl>&& that) : impl(std::move(that)) {}

  explicit Socket(const std::shared_ptr<Impl>& that) : impl(that) {}

  std::shared_ptr<Impl> impl;
};

} // namespace network {
} // namespace process {

#endif // __PROCESS_SOCKET_HPP__
