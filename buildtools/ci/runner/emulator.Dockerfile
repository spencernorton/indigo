# syntax=docker/dockerfile:1
# The emulator runner: Dolphin built from a pinned release with the patches in
# dolphin/ (an SD card adapter on the EXI bus), a virtual X server with
# software OpenGL, FFmpeg and the Python the harness in buildtools/ui/emulator/
# uses, the FAT tools that make its SD card, gxtexconv for the demonstration
# disc's posters, and a GitHub Actions runner that takes one job.
FROM ghcr.io/extremscorner/libogc2@sha256:f9ffb7e33c4ba4010da8e40c01366f7c24732130146abd99690403bd42d3e4c2 AS toolchain

FROM ubuntu:26.04@sha256:c098726dd9b9733f3e885f8de0ef375206a311b0a317eadc40d347b1115fb944 AS dolphin

ENV DEBIAN_FRONTEND=noninteractive LC_ALL=C.UTF-8 TZ=UTC

RUN apt-get update \
 && apt-get install -y --no-install-recommends \
      build-essential ca-certificates cmake git ninja-build pkg-config python3 \
      libbluetooth-dev libegl-dev libevdev-dev libgl-dev libsystemd-dev libudev-dev \
      libx11-dev libxi-dev libxrandr-dev \
 && rm -rf /var/lib/apt/lists/*

# Dolphin as the SD card adapter was written on: master of 2026-03-18 (2603
# and 81 commits), with its submodules. See dolphin/README.md.
ARG DOLPHIN_COMMIT=de44626d23a85aa3cc07260f6f97e64f36600652
RUN git init --quiet /src && cd /src \
 && git remote add origin https://github.com/dolphin-emu/dolphin.git \
 && git fetch --quiet --depth 1 origin "$DOLPHIN_COMMIT" \
 && git checkout --quiet FETCH_HEAD \
 && git submodule update --quiet --init --recursive --depth 1 --jobs 8

COPY dolphin/0001-sd2sp2-adapter.patch dolphin/0002-flush-sd-writes.patch dolphin/0003-controller-no-response.patch dolphin/0004-sd-faults.patch dolphin/0005-emulated-clock.patch dolphin/0006-gcloader.patch dolphin/0007-dabr.patch /src/patches/
RUN cd /src && git apply patches/0001-sd2sp2-adapter.patch patches/0002-flush-sd-writes.patch \
      patches/0003-controller-no-response.patch patches/0004-sd-faults.patch patches/0005-emulated-clock.patch \
      patches/0006-gcloader.patch patches/0007-dabr.patch \
 && cmake -S /src -B /build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/opt/dolphin \
      -DENABLE_QT=OFF -DENABLE_NOGUI=ON -DENABLE_TESTS=OFF -DENABLE_AUTOUPDATE=OFF \
      -DENABLE_ANALYTICS=OFF -DUSE_DISCORD_PRESENCE=OFF -DUSE_RETRO_ACHIEVEMENTS=OFF \
      -DUSE_MGBA=OFF -DUSE_UPNP=OFF -DENABLE_LLVM=OFF -DENCODE_FRAMEDUMPS=OFF -DENABLE_SDL=OFF \
      -DENABLE_ALSA=OFF -DENABLE_PULSEAUDIO=OFF -DENABLE_CUBEB=OFF \
 && cmake --build /build -j"$(nproc)" \
 && cmake --install /build \
 && strip /opt/dolphin/bin/dolphin-emu-nogui

FROM ubuntu:26.04@sha256:c098726dd9b9733f3e885f8de0ef375206a311b0a317eadc40d347b1115fb944

ENV DEBIAN_FRONTEND=noninteractive LC_ALL=C.UTF-8 TZ=UTC

RUN apt-get update \
 && apt-get install -y --no-install-recommends \
      ca-certificates curl dosfstools ffmpeg genisoimage git jq libbluetooth3 libegl-mesa0 \
      libegl1 libevdev2 libgl1 libgl1-mesa-dri libicu78 libkrb5-3 liblttng-ust1t64 libssl3t64 \
      libudev1 libx11-6 libxi6 libxrandr2 mtools python3 python3-numpy python3-pil unzip \
      xauth xvfb xz-utils \
 && rm -rf /var/lib/apt/lists/*

COPY --from=dolphin /opt/dolphin /opt/dolphin
RUN ln -s /opt/dolphin/bin/dolphin-emu-nogui /usr/local/bin/dolphin-emu-nogui

# The same static gxtexconv the build runner has, from the same image.
COPY --from=toolchain /opt/devkitpro/tools/bin/gxtexconv /usr/local/bin/gxtexconv

# X keeps its sockets in /tmp/.X11-unix, which only root may create.
RUN useradd --create-home --uid 1001 --shell /bin/bash runner \
 && mkdir -p /etc/indigo-ci \
 && mkdir -m 1777 /tmp/.X11-unix \
 && dolphin-emu-nogui --version > /etc/indigo-ci/emulator \
 && ! ldd /opt/dolphin/bin/dolphin-emu-nogui | grep 'not found' \
 && gxtexconv --version

ARG RUNNER_VERSION
ARG RUNNER_SHA256
RUN mkdir /home/runner/actions-runner \
 && curl -fsSL -o /tmp/runner.tgz \
      "https://github.com/actions/runner/releases/download/v${RUNNER_VERSION}/actions-runner-linux-x64-${RUNNER_VERSION}.tar.gz" \
 && echo "${RUNNER_SHA256}  /tmp/runner.tgz" | sha256sum -c - \
 && tar -xzf /tmp/runner.tgz -C /home/runner/actions-runner \
 && rm /tmp/runner.tgz \
 && chown -R runner:runner /home/runner
COPY --chmod=0755 entrypoint.sh /opt/indigo-ci/

USER runner
WORKDIR /home/runner
ENTRYPOINT ["/opt/indigo-ci/entrypoint.sh"]
