// Compile-time checks for UdpTransport
import net_io.udp_transport;
import net_io_concepts;
#include <span>

static_assert(net_io_concepts::Readable<net_io::UdpTransport>, "UdpTransport must be Readable (read(buf,size))");
static_assert(net_io_concepts::Writable<net_io::UdpTransport>, "UdpTransport must be Writable (write(buf,size))");

int main() { return 0; }
