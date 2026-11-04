#!/usr/bin/env python3
"""The only way out of a CI job container.

Job containers sit on Docker networks with no route out; this proxy is
attached to those networks and to one that has a route. It opens HTTPS
tunnels (CONNECT host:443) to GitHub, Sigstore and the watch's exact API
destinations. It refuses other names, other ports, plain HTTP, IP literals,
and any name that resolves to a private, loopback, link-local or otherwise non-public address.
A job cannot reach anything on the machine's own networks.
"""

from __future__ import annotations

import asyncio
import ipaddress
import os
import socket
import sys

# The runner's own traffic, checkout, artifacts and logs are GitHub; build
# provenance is signed through Sigstore. Swiss watch also uses OpenAI and
# Microsoft Graph; those three entries are exact names, never suffix rules.
ALLOWED = (
    "github.com",
    ".github.com",
    ".githubusercontent.com",
    ".blob.core.windows.net",
    "ghcr.io",
    ".sigstore.dev",
    "api.openai.com",
    "login.microsoftonline.com",
    "graph.microsoft.com",
)
NAME_CHARS = frozenset("abcdefghijklmnopqrstuvwxyz0123456789.-")


def host_allowed(host: str) -> bool:
    host = host.lower().rstrip(".")
    if not host or not set(host) <= NAME_CHARS or host.replace(".", "").isdigit():
        return False
    return any(host == rule or (rule[0] == "." and host.endswith(rule)) for rule in ALLOWED)


def parse_connect(line: bytes) -> tuple[str, int] | None:
    parts = line.decode("latin-1").split()
    if len(parts) != 3 or parts[0] != "CONNECT" or not parts[2].startswith("HTTP/1."):
        return None
    host, sep, port = parts[1].rpartition(":")
    if not sep or not port.isdigit() or not host:
        return None
    return host, int(port)


def public_address(address: str) -> bool:
    ip = ipaddress.ip_address(address.split("%", 1)[0])
    return ip.is_global and not ip.is_multicast


async def pipe(source: asyncio.StreamReader, sink: asyncio.StreamWriter) -> None:
    try:
        while data := await source.read(65536):
            sink.write(data)
            await sink.drain()
    except (ConnectionError, OSError):
        pass
    finally:
        try:
            if sink.can_write_eof():
                sink.write_eof()
        except (OSError, RuntimeError):
            pass


async def refuse(writer: asyncio.StreamWriter, status: str) -> None:
    writer.write(f"HTTP/1.1 {status}\r\nContent-Length: 0\r\nConnection: close\r\n\r\n".encode())
    try:
        await writer.drain()
    finally:
        writer.close()


async def handle(reader: asyncio.StreamReader, writer: asyncio.StreamWriter) -> None:
    client = (writer.get_extra_info("peername") or ("?",))[0]
    try:
        head = await asyncio.wait_for(reader.readuntil(b"\r\n\r\n"), 15)
    except (asyncio.TimeoutError, asyncio.IncompleteReadError, asyncio.LimitOverrunError, OSError):
        writer.close()
        return
    target = parse_connect(head.split(b"\r\n", 1)[0])
    if target is None:
        print(f"refused {client}: not a CONNECT request", flush=True)
        await refuse(writer, "405 Method Not Allowed")
        return
    host, port = target
    if port != 443 or not host_allowed(host):
        print(f"refused {client}: {host}:{port}", flush=True)
        await refuse(writer, "403 Forbidden")
        return
    try:
        infos = await asyncio.get_running_loop().getaddrinfo(host, port, type=socket.SOCK_STREAM)
    except OSError:
        await refuse(writer, "502 Bad Gateway")
        return
    addresses = list(dict.fromkeys(info[4][0] for info in infos))
    if not addresses or not all(public_address(a) for a in addresses):
        print(f"refused {client}: {host} resolves to {addresses}", flush=True)
        await refuse(writer, "403 Forbidden")
        return
    upstream = None
    for address in addresses:
        try:
            upstream = await asyncio.wait_for(asyncio.open_connection(address, port), 15)
            break
        except (asyncio.TimeoutError, OSError):
            continue
    if upstream is None:
        await refuse(writer, "502 Bad Gateway")
        return
    up_reader, up_writer = upstream
    print(f"tunnel {client}: {host}", flush=True)
    writer.write(b"HTTP/1.1 200 Connection Established\r\n\r\n")
    try:
        await writer.drain()
        await asyncio.gather(pipe(reader, up_writer), pipe(up_reader, writer))
    finally:
        up_writer.close()
        writer.close()


# Every open connection's task. asyncio holds tasks only weakly, and
# start_server keeps no reference to its handler's task (none before Python
# 3.12, and none after the client's side closes). A tunnel whose client has
# closed its side is then reachable from nothing, and the garbage collector
# would destroy it while the server may still be answering.
LIVE: set[asyncio.Task] = set()


def accept(reader: asyncio.StreamReader, writer: asyncio.StreamWriter) -> None:
    task = asyncio.get_running_loop().create_task(handle(reader, writer))
    LIVE.add(task)
    task.add_done_callback(LIVE.discard)


async def serve(port: int) -> None:
    server = await asyncio.start_server(accept, "0.0.0.0", port)
    print(f"egress proxy on :{port}; tunnels to {' '.join(ALLOWED)} only", flush=True)
    async with server:
        await server.serve_forever()


if __name__ == "__main__":
    try:
        asyncio.run(serve(int(os.environ.get("PROXY_PORT", "3128"))))
    except KeyboardInterrupt:
        sys.exit(0)
