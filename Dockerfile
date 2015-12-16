FROM benlubar/dwarffortress:df-0.40.24

RUN apt-get update && apt-get install -y --no-install-recommends \
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

RUN git clone -b v0.40.24-r5 --recursive --depth=1 https://github.com/DFHack/dfhack.git /df-ai/dfhack

ADD . /df-ai

RUN mkdir -p /df-ai/build \
&& cd /df-ai/build \
&& echo 'add_subdirectory(df-ai)' >> /df-ai/dfhack/plugins/CMakeLists.custom.txt \
&& ln -s /df-ai/df-ai /df-ai/dfhack/plugins/df-ai \
&& cmake -DCMAKE_INSTALL_PREFIX:STRING=/df_linux -DBUILD_DFUSION:BOOL=OFF -DBUILD_RUBY:BOOL=OFF -DBUILD_DWARFEXPORT:BOOL=OFF -DBUILD_MAPEXPORT:BOOL=OFF -DBUILD_SUPPORTED:BOOL=OFF -DZLIB_LIBRARY:STRING=/usr/lib/i386-linux-gnu/libz.so /df-ai/dfhack \
&& make install \
&& echo "enable df-ai" > /df_linux/dfhack.init \
&& rm -rf /df-ai

CMD ["./dfhack"]
