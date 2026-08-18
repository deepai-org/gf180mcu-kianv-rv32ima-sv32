FROM ubuntu@sha256:4fbb8e6a8395de5a7550b33509421a2bafbc0aab6c06ba2cef9ebffbc7092d90

RUN apt-get update \
    && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
        bc \
        build-essential \
        ca-certificates \
        cmake \
        cpio \
        device-tree-compiler \
        file \
        gawk \
        git \
        python3 \
        rsync \
        unzip \
        wget \
    && rm -rf /var/lib/apt/lists/*
