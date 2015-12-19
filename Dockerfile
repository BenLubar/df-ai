FROM benlubar/dwarffortress:dfhack-0.40.24-r5

ADD . /df-ai

RUN cd /dfhack/build-docker \
&& echo 'add_subdirectory(df-ai)' >> /dfhack/plugins/CMakeLists.custom.txt \
&& ln -s /df-ai/df-ai /dfhack/plugins/df-ai \
&& make install \
&& echo "enable df-ai" > /df_linux/dfhack.init
