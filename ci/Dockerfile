ARG MY_PROJECT_VER=0.0.0-rc0
FROM registry.gitlab.com/ndmspc/ndmspc/base:v${MY_PROJECT_VER}
USER root
ARG NDMSPC_INIT_DIR=/var/tmp/ndmspc-init
ENV NDMSPC_INIT_DIR=${NDMSPC_INIT_DIR}
COPY ci ${NDMSPC_INIT_DIR}/ci
RUN ${NDMSPC_INIT_DIR}/ci/prepare-eos-slurm.sh
STOPSIGNAL SIGRTMIN+3
ENTRYPOINT []
CMD ["/sbin/init"]
