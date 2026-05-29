// Compile-time checks for UdpTransport
import net_io.udp_transport;
import net_io.udp_endpoint;
import net_io_concepts;
#include <span>

static_assert(net_io_concepts::Readable<modern::net::UdpTransport>, "UdpTransport must be Readable (read(buf,size))");
static_assert(net_io_concepts::Writable<modern::net::UdpTransport>, "UdpTransport must be Writable (write(buf,size))");
static_assert(std::same_as<modern::net::UdpTransport, net_io::UdpTransport>, "Legacy UdpTransport name must remain compatible");
static_assert(std::same_as<modern::net::UdpEndpoint, net_io::UdpEndpoint>, "Legacy UdpEndpoint name must remain compatible");

int main() { return 0; }
