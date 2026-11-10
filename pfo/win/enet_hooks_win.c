/* Windows build: the relay hooks that enet-1.3.18-relay-hooks.patch adds to ENet's unix.c,
   without patching win32.c. scripts/build_pfo_windows.sh compiles win32.c with
   -Denet_socket_send=enet_socket_send_real -Denet_socket_receive=enet_socket_receive_real,
   and the functions ENet calls are the two below.

   Addresses in 0.0.0.0/8 are never valid on the wire, so they stand for relayed peers: sends to
   them go to enet_virtual_send_hook, and datagrams the relay received come back first in
   enet_socket_receive through enet_virtual_receive_hook. Both hooks are NULL unless set. */
#include <enet/enet.h>

int enet_socket_send_real (ENetSocket, const ENetAddress *, const ENetBuffer *, size_t);
int enet_socket_receive_real (ENetSocket, ENetAddress *, ENetBuffer *, size_t);

int (* enet_virtual_send_hook) (const ENetAddress *, const ENetBuffer *, size_t) = NULL;
int (* enet_virtual_receive_hook) (ENetAddress *, ENetBuffer *, size_t) = NULL;

int
enet_socket_send (ENetSocket socket, const ENetAddress * address, const ENetBuffer * buffers, size_t bufferCount)
{
    if (address != NULL && address -> host != 0 && (ENET_NET_TO_HOST_32 (address -> host) >> 24) == 0 && enet_virtual_send_hook != NULL)
      return enet_virtual_send_hook (address, buffers, bufferCount);

    return enet_socket_send_real (socket, address, buffers, bufferCount);
}

int
enet_socket_receive (ENetSocket socket, ENetAddress * address, ENetBuffer * buffers, size_t bufferCount)
{
    if (address != NULL && enet_virtual_receive_hook != NULL)
    {
        int relayed = enet_virtual_receive_hook (address, buffers, bufferCount);
        if (relayed > 0)
          return relayed;
    }

    return enet_socket_receive_real (socket, address, buffers, bufferCount);
}
