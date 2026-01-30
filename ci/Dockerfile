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
RUN dnf install -y ndmspc*.rpm
RUN rm -rf *.rpm
RUN dnf clean all
CMD [ "/bin/bash" ]
