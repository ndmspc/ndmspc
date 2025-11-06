FROM registry.gitlab.com/ndmspc/user/al9:dep AS builder
ARG MY_PROJECT_VER=0.0.0-rc0
COPY . /builder/
WORKDIR /builder
RUN dnf update -y
RUN scripts/make.sh clean rpm


#FROM docker.io/library/almalinux:9
FROM registry.gitlab.com/ndmspc/user/al9:base
#RUN dnf install epel-release 'dnf-command(config-manager)' 'dnf-command(copr)' -y
#RUN dnf copr enable ndmspc/stable -y
RUN dnf update -y
RUN dnf install -y eos-server eos-quarkdb jemalloc-devel screen nmap salsa munge
COPY . /ndmspc/
RUN /ndmspc/scripts/ndmspc-slurm-init
RUN systemctl enable munge slurmctld slurmd
COPY ci/eos/ndmspc-eos-init /usr/local/bin/eos_init
COPY ci/eos/etc /etc
COPY ci/eos/custom.target /etc/systemd/system/custom.target
COPY ci/eos/eos-init.service /etc/systemd/system/eos-init.service
RUN ln -sf /etc/systemd/system/custom.target /etc/systemd/system/default.target
RUN ln -sf /etc/systemd/system/eos-init.service /etc/systemd/system/multi-user.target.wants/eos-init.service
COPY --from=builder /builder/build/RPMS/x86_64/ndmspc*.rpm /
RUN dnf install -y ndmspc*.rpm
RUN rm -rf *.rpm
RUN dnf clean all
ENV ROOT_INCLUDE_PATH="/usr/include/ndmspc:/usr/include/root"
RUN ln -sf /usr/lib/systemd/system/ndmspc-http@.service /etc/systemd/system/multi-user.target.wants/ndmspc-http@default.service
# CMD [ "/bin/bash" ]
CMD [ "/sbin/init" ]
