# Builds HAL Zero for the Cardputer Zero (Debian 13 trixie, arm64) on an
# Apple-silicon Mac: same distro and libraries as the device.
FROM debian:trixie
RUN apt-get update && apt-get install -y --no-install-recommends \
      build-essential pkg-config libsdl2-dev python3 python3-venv git ca-certificates \
    && rm -rf /var/lib/apt/lists/*
RUN python3 -m venv /opt/pio && /opt/pio/bin/pip install --no-cache-dir platformio
ENV PATH="/opt/pio/bin:${PATH}" PLATFORMIO_CORE_DIR=/opt/pio-core
WORKDIR /src
