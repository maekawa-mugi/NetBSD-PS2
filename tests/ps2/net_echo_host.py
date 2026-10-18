#!/usr/bin/env python3
"""Bounded UDP/TCP echo peer for the PS2 Sockets-backend regression helper."""
import argparse
import json
import selectors
import socket
import time

parser = argparse.ArgumentParser()
parser.add_argument('--bind', required=True)
parser.add_argument('--timeout', type=float, default=120)
args = parser.parse_args()
selector = selectors.DefaultSelector()
udp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
tcp = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
udp.bind((args.bind, 38473))
tcp.bind((args.bind, 38474))
tcp.listen(1)
selector.register(udp, selectors.EVENT_READ, 'udp')
selector.register(tcp, selectors.EVENT_READ, 'listen')
print(json.dumps({'ready': True, 'host': args.bind, 'udp': 38473, 'tcp': 38474}), flush=True)
packets = total = 0
deadline = time.monotonic() + args.timeout
clients = []
try:
    while time.monotonic() < deadline:
        for key, _ in selector.select(timeout=min(1, max(0, deadline - time.monotonic()))):
            if key.data == 'udp':
                data, peer = udp.recvfrom(2048)
                udp.sendto(data, peer)
                packets += 1
            elif key.data == 'listen':
                client, _ = tcp.accept()
                client.settimeout(5)
                clients.append(client)
                selector.register(client, selectors.EVENT_READ, 'client')
            else:
                data = key.fileobj.recv(4096)
                if not data:
                    selector.unregister(key.fileobj)
                    key.fileobj.close()
                    continue
                key.fileobj.sendall(data)
                total += len(data)
        if packets >= 96 and total >= 44800:
            # Stay available briefly for FIN/retransmission, then finish.
            deadline = min(deadline, time.monotonic() + 2)
finally:
    for client in clients:
        client.close()
    udp.close()
    tcp.close()
    selector.close()
print(json.dumps({'udp_packets': packets, 'tcp_bytes': total}), flush=True)
