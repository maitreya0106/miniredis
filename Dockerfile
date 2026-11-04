FROM ubuntu:24.04 AS build
RUN apt-get update \
    && apt-get install -y --no-install-recommends g++ cmake make ca-certificates \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY CMakeLists.txt ./
COPY src src
COPY tests tests
RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
    && cmake --build build --parallel \
    && ctest --test-dir build --output-on-failure

FROM ubuntu:24.04
COPY --from=build /src/build/miniredis /usr/local/bin/miniredis
RUN useradd --system --no-create-home miniredis \
    && mkdir /data && chown miniredis /data
USER miniredis
WORKDIR /data
EXPOSE 6380
ENTRYPOINT ["miniredis"]
CMD ["--bind", "0.0.0.0", "--port", "6380"]
