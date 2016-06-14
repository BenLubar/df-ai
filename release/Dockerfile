FROM benlubar/dwarffortress:dfhack-0.43.03-alpha1

ENV DF_AI_VERSION ${DFHACK_VERSION}

RUN curl -ksSL https://github.com/BenLubar/df-ai/releases/download/${DF_AI_VERSION}/df-ai-${DF_AI_VERSION}-linux.tar.gz | tar -xzv \
&& mv df-ai.plug.so hack/plugins/ \
&& echo "enable df-ai" > /df_linux/dfhack.init \
&& mkdir -p /df_linux/dfhack-config \
&& echo "{\"hide\": true}" > /df_linux/dfhack-config/prerelease-warning.json
