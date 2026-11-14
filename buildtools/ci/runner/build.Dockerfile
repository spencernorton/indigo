# syntax=docker/dockerfile:1
# The build runner: the pinned devkitPPC/libogc2 image every Indigo DOL is
# built in, the host-test toolchain (GCC and Clang with their sanitizers,
# Python with Pillow and NumPy, zlib), and a GitHub Actions runner that takes
# one job. indigo_ci.py builds it from beta; a job checks it runs on the
# toolchain its workflow names (buildtools/ci/toolchain.sh).
FROM ghcr.io/extremscorner/libogc2@sha256:7c91503254afcf0acebc8edb6f81fc8535860c42ae31ca33dd1573464eb12565

ENV DEBIAN_FRONTEND=noninteractive LC_ALL=C.UTF-8 TZ=UTC

COPY --chmod=0755 libiconv-lite.sh /opt/indigo-ci/libiconv-lite.sh
RUN /opt/indigo-ci/libiconv-lite.sh --cache /usr/local/share/indigo

RUN apt-get update \
 && apt-get install -y --no-install-recommends \
      ca-certificates clang curl git jq libclang-rt-14-dev libicu72 libkrb5-3 \
      liblttng-ust1 libssl3 llvm python3 python3-numpy python3-pil \
      shellcheck time unzip xz-utils zip zlib1g-dev \
 && rm -rf /var/lib/apt/lists/*

# gh publishes a release (release.yml's publish job).
ARG GH_VERSION=2.101.0
ARG GH_SHA256=9bca2d1c16825f109907a23307628a2f0698fbf99662b73a5cf0b020293072b8
RUN curl -fsSL -o /tmp/gh.tgz \
      "https://github.com/cli/cli/releases/download/v${GH_VERSION}/gh_${GH_VERSION}_linux_amd64.tar.gz" \
 && echo "${GH_SHA256}  /tmp/gh.tgz" | sha256sum -c - \
 && tar -xzf /tmp/gh.tgz -C /tmp \
 && install -m 0755 "/tmp/gh_${GH_VERSION}_linux_amd64/bin/gh" /usr/local/bin/gh \
 && rm -rf /tmp/gh.tgz "/tmp/gh_${GH_VERSION}_linux_amd64"

RUN useradd --create-home --uid 1001 --shell /bin/bash runner \
 && mkdir -p /etc/indigo-ci \
 && echo "ghcr.io/extremscorner/libogc2@sha256:7c91503254afcf0acebc8edb6f81fc8535860c42ae31ca33dd1573464eb12565" \
      > /etc/indigo-ci/toolchain

# Last, so a new runner release rebuilds only this layer, never the toolchain.
ARG RUNNER_VERSION
ARG RUNNER_SHA256
RUN mkdir /home/runner/actions-runner \
 && curl -fsSL -o /tmp/runner.tgz \
      "https://github.com/actions/runner/releases/download/v${RUNNER_VERSION}/actions-runner-linux-x64-${RUNNER_VERSION}.tar.gz" \
 && echo "${RUNNER_SHA256}  /tmp/runner.tgz" | sha256sum -c - \
 && tar -xzf /tmp/runner.tgz -C /home/runner/actions-runner \
 && rm /tmp/runner.tgz \
 && chown -R runner:runner /home/runner
COPY --chmod=0755 entrypoint.sh egress_proxy.py /opt/indigo-ci/

USER runner
WORKDIR /home/runner
ENTRYPOINT ["/opt/indigo-ci/entrypoint.sh"]
