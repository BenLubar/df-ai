FROM benlubar/dwarffortress:dfhack-0.42.06-r1

RUN curl -ksSL https://github.com/BenLubar/df-ai/releases/download/${DFHACK_VERSION}-update2/df-ai-${DFHACK_VERSION}-update2-linux.tar.gz | tar -xzv \
&& mv df-ai.plug.so hack/plugins/ \
&& echo "enable df-ai" > /df_linux/dfhack.init \
&& mkdir -p /df_linux/dfhack-config \
&& echo "{\"hide\": true}" > /df_linux/dfhack-config/prerelease-warning.json
