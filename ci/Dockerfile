ARG MY_PROJECT_VER=0.0.0-rc0
FROM registry.gitlab.com/ndmspc/ndmspc/base:v${MY_PROJECT_VER}
USER root
RUN dnf update -y && dnf install -y screen nmap salsa munge python3-jupyroot python3-pip ansible && dnf clean all
RUN pip3 install --no-cache-dir jupyter metakernel
ARG NDMSPC_INIT_DIR=/var/tmp/ndmspc-init
COPY . ${NDMSPC_INIT_DIR}
RUN ${NDMSPC_INIT_DIR}/ci/slurm/ndmspc-slurm-init
RUN systemctl enable munge slurmctld slurmd
RUN git clone https://gitlab.cern.ch/eos/ansible.git ${NDMSPC_INIT_DIR}/eos-ansible
COPY ci/eos/ndmspc-eos-init /usr/local/bin/eos_init
COPY ci/eos/custom.target /etc/systemd/system/custom.target
COPY ci/eos/eos-init.service /etc/systemd/system/eos-init.service
RUN ln -sf /etc/systemd/system/custom.target /etc/systemd/system/default.target
RUN ln -sf /etc/systemd/system/eos-init.service /etc/systemd/system/multi-user.target.wants/eos-init.service
RUN ln -sf /usr/lib/systemd/system/ndmspc-http@.service /etc/systemd/system/multi-user.target.wants/ndmspc-http@default.service
CMD [ "/sbin/init" ]
