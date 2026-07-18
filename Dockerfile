FROM ubuntu:24.04 AS builder

RUN apt-get update && apt-get install -y --no-install-recommends \
    clang-18 cmake ninja-build git make ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /gistdb
COPY . .

RUN cd third_party/libpg_query && make CC=clang-18

RUN cmake -B build -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER=clang-18 \
    -DCMAKE_CXX_COMPILER=clang++-18 \
    && cmake --build build -j"$(nproc)" --target gistdb

FROM ubuntu:24.04 AS runtime

RUN apt-get update && apt-get install -y --no-install-recommends \
    libstdc++6 \
    && rm -rf /var/lib/apt/lists/*

COPY --from=builder /gistdb/build/gistdb /usr/local/bin/gistdb

WORKDIR /data
ENTRYPOINT ["gistdb"]