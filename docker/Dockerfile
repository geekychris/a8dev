# Atari 8-bit dev toolchain: cc65 (cl65/ca65/ld65, targets atari + atarixl)
# and the MADS assembler. Native arm64 and amd64 (built from pinned sources).
#
#   scripts/build-toolchain-image.sh   builds this as a8-dev-toolchain:<N>
ARG DEBIAN_IMAGE=debian:bookworm-slim

FROM ${DEBIAN_IMAGE} AS build
ARG CC65_COMMIT
ARG MADS_REF
RUN apt-get update -qq && apt-get install -y -qq --no-install-recommends \
      git ca-certificates build-essential fp-compiler >/dev/null
RUN git init -q /src/cc65 && cd /src/cc65 \
 && git remote add origin https://github.com/cc65/cc65.git \
 && git fetch -q --depth 1 origin ${CC65_COMMIT} && git checkout -q FETCH_HEAD \
 && make -j"$(nproc)" PREFIX=/opt/cc65 >/dev/null 2>&1 \
 && make install PREFIX=/opt/cc65 >/dev/null
RUN git clone -q --depth 1 --branch ${MADS_REF} https://github.com/tebe6502/Mad-Assembler.git /src/mads \
 && cd /src/mads && fpc -Mdelphi -vh -O3 mads.pas >/dev/null \
 && install -m 755 mads /opt/cc65/bin/mads

FROM ${DEBIAN_IMAGE}
COPY --from=build /opt/cc65 /opt/cc65
RUN apt-get update -qq && apt-get install -y -qq --no-install-recommends make python3 >/dev/null \
 && rm -rf /var/lib/apt/lists/*
ENV PATH=/opt/cc65/bin:$PATH
WORKDIR /work
