FROM ubuntu:22.04 AS build

RUN apt-get update && apt-get install -y \
    cmake ninja-build g++ ffmpeg \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . .
RUN cmake -B build -G Ninja && cmake --build build

FROM ubuntu:22.04
RUN apt-get update && apt-get install -y ffmpeg \
    && rm -rf /var/lib/apt/lists/*

COPY --from=build /src/build/hls_packager /usr/local/bin/hls_packager

ENTRYPOINT ["hls_packager"]