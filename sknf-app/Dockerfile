FROM debian@sha256:01a723bf5bfb21b9dda0c9a33e0538106e4d02cce8f557e118dd61259553d598

ENV GO_VERSION=1.25.3
ENV PATH="/usr/local/go/bin:${PATH}"

RUN apt-get update && \
    apt-get install -y --no-install-recommends \
        curl ca-certificates gcc make build-essential pkg-config \
        libnl-3-dev libnl-route-3-dev libnftnl-dev libmnl-dev libjson-c-dev && \
    rm -rf /var/lib/apt/lists/*

RUN curl -fsSL "https://go.dev/dl/go${GO_VERSION}.linux-amd64.tar.gz" -o /tmp/go.tar.gz && \
    tar -C /usr/local -xzf /tmp/go.tar.gz && \
    rm /tmp/go.tar.gz

RUN useradd -m -s /bin/bash sknf
USER sknf
WORKDIR /home/sknf

COPY --chown=sknf:sknf . .

RUN make build-sknf-cni
RUN make build-sknf-app

CMD ["./sknf-app/bin/sknf-app"]
