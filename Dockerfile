FROM sendence/ponyc-runner:0.0.5

COPY . /build
RUN apt-get update \
 && apt-get install -y make g++ git wget xz-utils \
                       zlib1g-dev libncurses5-dev libssl-dev \
                       libpcre2-dev \
 && rm -rf /var/lib/apt/lists/* \
 && wget http://llvm.org/releases/3.9.0/clang+llvm-3.9.0-x86_64-linux-gnu-ubuntu-16.04.tar.xz \
 && tar xf clang+llvm-3.9.0-x86_64-linux-gnu-ubuntu-16.04.tar.xz \
 && cp -r clang+llvm-3.9.0-x86_64-linux-gnu-ubuntu-16.04/* /usr/local \
 && rm -rf /clang*

WORKDIR /build

ARG PONYC_CONFIG
ENV PONYC_CONFIG ${PONYC_CONFIG:-debug}

RUN LLVM_CONFIG=true make config=${PONYC_CONFIG} install
RUN cp /build/build/arm-libponyrt.a /usr/arm-linux-gnueabihf/lib/libponyrt.a
RUN make \
 && make install \
 && rm -rf /src/ponyc/build

RUN mkdir /src/main
WORKDIR /src/main

WORKDIR /build/build/pony-stable

RUN make install

WORKDIR /tmp

RUN rm -rf /build

ADD armhf-*.tar.gz /usr/arm-linux-gnueabihf/
ADD amd64-*.tar.gz /usr/local/

ENTRYPOINT ["ponyc"]
