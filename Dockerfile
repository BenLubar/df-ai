FROM benlubar/dwarffortress:dfhack-onbuild

RUN echo "enable df-ai" > /df_linux/dfhack.init \
&& mkdir -p /df_linux/dfhack-config \
&& echo "{\"hide\": true}" > /df_linux/dfhack-config/prerelease-warning.json
