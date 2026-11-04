#!/usr/bin/env python3
"""Job egress stays restricted to the documented CI and watch services."""

import asyncio
import gc
import unittest
from unittest import mock

import egress_proxy
from egress_proxy import accept, host_allowed, parse_connect, public_address


class Rules(unittest.TestCase):
    def test_github_and_sigstore_pass(self):
        for host in ("github.com", "api.github.com", "codeload.github.com",
                     "objects.githubusercontent.com", "pipelines.actions.githubusercontent.com",
                     "productionresultssa0.blob.core.windows.net", "ghcr.io",
                     "fulcio.sigstore.dev", "rekor.sigstore.dev", "GitHub.com."):
            self.assertTrue(host_allowed(host), host)

    def test_everything_else_is_refused(self):
        for host in ("", "evilgithub.com", "github.com.evil.net", "githubusercontent.com.evil.net",
                     "example.com", "localhost", "intranet", "140.82.112.3", "10.0.0.1",
                     "[::1]", "git.hub.com", "github.com:443", "sigstore.dev.evil.net"):
            self.assertFalse(host_allowed(host), host)

    def test_swiss_watch_services_pass(self):
        for host in ("api.openai.com", "login.microsoftonline.com", "graph.microsoft.com",
                     "API.OpenAI.com."):
            self.assertTrue(host_allowed(host), host)

    def test_swiss_watch_services_are_exact_names(self):
        for host in ("openai.com", "microsoft.com", "microsoftonline.com",
                     "other.openai.com", "other.microsoftonline.com", "other.microsoft.com",
                     "evilapi.openai.com", "sub.api.openai.com", "api.openai.com.evil.net",
                     "sub.login.microsoftonline.com", "login.microsoftonline.com.evil.net",
                     "sub.graph.microsoft.com", "graph.microsoft.com.evil.net"):
            self.assertFalse(host_allowed(host), host)

    def test_connect_line(self):
        self.assertEqual(parse_connect(b"CONNECT api.github.com:443 HTTP/1.1"), ("api.github.com", 443))
        for line in (b"GET http://github.com/ HTTP/1.1", b"CONNECT github.com HTTP/1.1",
                     b"CONNECT :443 HTTP/1.1", b"CONNECT github.com:https HTTP/1.1",
                     b"CONNECT github.com:443", b"CONNECT github.com:443 SPDY/3"):
            self.assertIsNone(parse_connect(line), line)

    def test_only_public_addresses(self):
        for address in ("140.82.112.3", "2606:50c0:8000::153"):
            self.assertTrue(public_address(address), address)
        for address in ("10.1.2.3", "172.17.0.1", "192.168.0.1", "100.64.0.1", "127.0.0.1",
                        "169.254.169.254", "0.0.0.0", "224.0.0.1", "::1", "fe80::1%eth0",
                        "fd00::1"):
            self.assertFalse(public_address(address), address)


class Refusals(unittest.TestCase):
    """End to end over a real socket; none of these needs the internet."""

    def exchange(self, request: bytes) -> bytes:
        async def run() -> bytes:
            server = await asyncio.start_server(accept, "127.0.0.1", 0)
            port = server.sockets[0].getsockname()[1]
            async with server:
                reader, writer = await asyncio.open_connection("127.0.0.1", port)
                writer.write(request)
                await writer.drain()
                reply = await asyncio.wait_for(reader.read(), 10)
                writer.close()
                return reply
        return asyncio.run(run())

    def test_plain_http_is_refused(self):
        self.assertTrue(self.exchange(b"GET http://github.com/ HTTP/1.1\r\nHost: github.com\r\n\r\n")
                        .startswith(b"HTTP/1.1 405"))

    def test_other_hosts_and_ports_are_refused(self):
        for target in (b"example.com:443", b"github.com:22", b"127.0.0.1:443", b"localhost:443",
                       b"api.openai.com:80", b"graph.microsoft.com:22"):
            reply = self.exchange(b"CONNECT " + target + b" HTTP/1.1\r\nHost: x\r\n\r\n")
            self.assertTrue(reply.startswith(b"HTTP/1.1 403"), (target, reply))

    def test_watch_hosts_resolving_to_private_addresses_are_refused(self):
        async def run(host: str) -> bytes:
            server = await asyncio.start_server(accept, "127.0.0.1", 0)
            async with server:
                reader, writer = await asyncio.open_connection(*server.sockets[0].getsockname()[:2])
                # Mixed public/private DNS must fail before opening any tunnel.
                answers = [(egress_proxy.socket.AF_INET, egress_proxy.socket.SOCK_STREAM,
                            egress_proxy.socket.IPPROTO_TCP, "", (address, 443))
                           for address in ("8.8.8.8", "127.0.0.1")]
                with mock.patch.object(asyncio.get_running_loop(), "getaddrinfo",
                                       new=mock.AsyncMock(return_value=answers)):
                    writer.write(f"CONNECT {host}:443 HTTP/1.1\r\nHost: x\r\n\r\n".encode())
                    await writer.drain()
                    reply = await asyncio.wait_for(reader.read(), 10)
                writer.close()
                await writer.wait_closed()
                return reply

        for host in ("api.openai.com", "login.microsoftonline.com", "graph.microsoft.com"):
            self.assertTrue(asyncio.run(run(host)).startswith(b"HTTP/1.1 403"), host)


class Lifetime(unittest.TestCase):
    def test_a_connection_is_kept_until_it_ends(self):
        """Once a client closes its side, nothing but the proxy refers to that
        connection's task; a garbage collection must not end the tunnel."""
        async def run() -> list[str]:
            events: list[str] = []
            started = asyncio.Event()

            async def waits(reader, writer):
                started.set()
                try:
                    await asyncio.get_running_loop().create_future()  # only this task refers to it
                finally:
                    events.append("ended")
                    writer.close()

            with mock.patch.object(egress_proxy, "handle", waits):
                server = await asyncio.start_server(accept, "127.0.0.1", 0)
                async with server:
                    _, writer = await asyncio.open_connection(*server.sockets[0].getsockname()[:2])
                    await started.wait()
                    writer.close()
                    await asyncio.sleep(0.2)  # the proxy reads the client's end
                    gc.collect()
                    events.append(f"live {len(egress_proxy.LIVE)}")
                    tasks = list(egress_proxy.LIVE)
                    for task in tasks:
                        task.cancel()
                    await asyncio.gather(*tasks, return_exceptions=True)
                    events.append(f"live {len(egress_proxy.LIVE)}")
            return events
        self.assertEqual(asyncio.run(run()), ["live 1", "ended", "live 0"])


if __name__ == "__main__":
    unittest.main()
