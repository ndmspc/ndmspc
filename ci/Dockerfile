FROM registry.gitlab.com/ndmspc/user/al9:dep AS builder
ARG MY_PROJECT_VER=0.0.0-rc0
COPY . /builder/
WORKDIR /builder
RUN dnf update -y
RUN scripts/make.sh clean rpm


FROM docker.io/library/almalinux:9
# FROM registry.gitlab.com/ndmspc/user/al9:base
RUN dnf install epel-release 'dnf-command(config-manager)' 'dnf-command(copr)' -y
RUN dnf config-manager --set-enabled crb
RUN dnf copr enable ndmspc/stable -y
RUN dnf update -y
COPY --from=builder /builder/build/RPMS/x86_64/ndmspc*.rpm /
RUN dnf install -y ndmspc*.rpm tini
RUN rm -rf *.rpm
RUN dnf clean all
ENV ROOT_INCLUDE_PATH="/usr/include/ndmspc:/usr/include/root"

ARG MY_USER=user
RUN groupadd -g 1000 ${MY_USER} && \
  useradd -u 1000 -g ${MY_USER} -m -s /bin/bash ${MY_USER}

# 2. Set ownership of the application directories
# Replace these paths with the actual locations your app needs to write to
RUN mkdir -p /ndmspc
RUN chown -R ${MY_USER}:${MY_USER} /ndmspc

# 3. If the server writes logs or temp files, ensure permissions are set
# Example: RUN chown -R ndmspcuser:ndmspcuser /var/log/ndmspc

# 4. Switch to the non-root user
USER 1000
WORKDIR /ndmspc

ENTRYPOINT ["/usr/bin/tini", "--"]
CMD [ "/bin/bash" ]
