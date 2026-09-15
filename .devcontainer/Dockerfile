FROM devkitpro/devkitarm:20260610@sha256:116afba8df8453961de2936ffab20dd441edf4d682856c1ec8b0e53d7ed0bbf5

RUN apt-get update \
    && apt-get install --yes --no-install-recommends \
      clang-format \
      cppcheck \
      cmake \
      git \
      ninja-build \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /workspace
