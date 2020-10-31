# NOTE: this dockerfile compiles but doesn't work yet
# TODO: start from an nvidia docker instead

FROM ubuntu:20.04

LABEL maintainer "Visky Development Team"

# Install the build and lib dependencies, install vulkan and a recent version of CMake
RUN \
    apt-get update && \
    DEBIAN_FRONTEND=noninteractive apt-get install -y tzdata && \
    apt-get install -y build-essential git cmake wget ninja-build xcb libx11-xcb-dev \
    libxcb-glx0 libglfw3-dev libpng-dev libavcodec-dev libavformat-dev libavfilter-dev \
    libavutil-dev libswresample-dev libvncserver-dev xtightvncviewer libqt5opengl5-dev \
    libfreetype6-dev libassimp-dev

# install vulkan sdk
ENV VULKAN_SDK_VERSION=1.2.148.1
RUN echo "downloading Vulkan SDK $VULKAN_SDK_VERSION" \
    && wget -q --show-progress --progress=bar:force:noscroll \
    "https://sdk.lunarg.com/sdk/download/$VULKAN_SDK_VERSION/linux/vulkansdk-linux-x86_64-$VULKAN_SDK_VERSION.tar.gz?Human=true" \
    -O /tmp/vulkansdk-linux-x86_64-$VULKAN_SDK_VERSION.tar.gz \
    && echo "installing Vulkan SDK $VULKAN_SDK_VERSION" \
    && mkdir -p /opt/vulkan \
    && tar -xf /tmp/vulkansdk-linux-x86_64-$VULKAN_SDK_VERSION.tar.gz -C /opt/vulkan \
    &&  rm /tmp/vulkansdk-linux-x86_64-$VULKAN_SDK_VERSION.tar.gz
ENV VULKAN_SDK=/opt/vulkan/${VULKAN_SDK_VERSION}/x86_64
ENV PATH="$VULKAN_SDK/bin:$PATH" \
    LD_LIBRARY_PATH="$VULKAN_SDK/lib:${LD_LIBRARY_PATH:-}" \
    VK_LAYER_PATH="$VULKAN_SDK/etc/vulkan/explicit_layer.d"

# TODO: move up
RUN apt-get install -y python3-dev python3-pip python3-numpy cython3

# Add the code (TODO: replace with git clone later)
# NOTE: to avoid conflicts with cmake cache, clone another copy of the repo locally in ./experimental/
COPY . /visky
RUN \
    pip3 install -r /visky/bindings/cython/requirements.txt && \
    mkdir -p /visky/build
WORKDIR /visky
RUN ./manage.sh build
RUN \
    mv /visky/libv* /visky/vk* /visky/build
ENV VK_ICD_FILENAMES=/visky/build/vk_swiftshader_icd.json

RUN \
    groupadd -g 1000 visky && \
    useradd -d /home/visky -s /bin/bash -m visky -u 1000 -g 1000 && \
    chown -R visky:visky /visky
USER visky
ENV HOME /home/visky

RUN \
    cd /visky/build && ./visky test

# cd /visky/bindings/cython/ && \
# ./build.sh
