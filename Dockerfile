FROM benlubar/dwarffortress:dfhack-0.42.05-alpha1

ADD df-ai /df-ai

RUN apt-get update && apt-get install -y --no-install-recommends \
	ca-certificates \
	cmake \
	g++-multilib \
	gcc-multilib \
	git \
	libxml-libxml-perl \
	libxml-libxslt-perl \
	make \
	zlib1g-dev:i386 \
&& git clone -b 0.42.05-alpha1 --recursive --depth=1 https://github.com/DFHack/dfhack.git /dfhack \
&& mkdir -p /dfhack/build-docker \
&& cd /dfhack/build-docker \
&& echo 'add_subdirectory(df-ai)' >> /dfhack/plugins/CMakeLists.custom.txt \
&& mv /df-ai /dfhack/plugins/ \
&& cmake -DCMAKE_INSTALL_PREFIX:STRING=/df_linux .. \
&& make df-ai \
&& cd /dfhack/build-docker/plugins/df-ai \
&& make install/local \
&& cd /df_linux \
&& rm -rf /dfhack \
&& echo "enable df-ai" > /df_linux/dfhack.init \
&& echo -n > /df_linux/hack/scripts/gui/prerelease-warning.lua \
&& apt-get purge -y --auto-remove \
	ca-certificates \
	cmake \
	g++-multilib \
	gcc-multilib \
	git \
	libxml-libxml-perl \
	libxml-libxslt-perl \
	make \
	zlib1g-dev:i386 \
&& apt-get clean \
&& rm -rf /var/lib/apt/lists/*
