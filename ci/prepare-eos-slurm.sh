#!/bin/bash
set -euo pipefail

NDMSPC_INIT_DIR=${NDMSPC_INIT_DIR-/var/tmp/ndmspc-init}
dnf update -y && dnf install -y git screen nmap munge python3-pip
pip3 install --no-cache-dir ansible
# dnf install -y  python3-jupyroot
# pip3 install --no-cache-dir jupyter metakernel
${NDMSPC_INIT_DIR}/ci/slurm/ndmspc-slurm-init && systemctl enable munge slurmctld slurmd
git clone https://gitlab.cern.ch/eos/ansible.git ${NDMSPC_INIT_DIR}/eos-ansible
cp ${NDMSPC_INIT_DIR}/ci/eos/ndmspc-eos-init /usr/local/bin/eos_init
cp ${NDMSPC_INIT_DIR}/ci/eos/custom.target /etc/systemd/system/custom.target
cp ${NDMSPC_INIT_DIR}/ci/eos/eos-init.service /etc/systemd/system/eos-init.service
ln -sf /etc/systemd/system/custom.target /etc/systemd/system/default.target
ln -sf /etc/systemd/system/eos-init.service /etc/systemd/system/multi-user.target.wants/eos-init.service
ln -sf /usr/lib/systemd/system/ndmspc-http@.service /etc/systemd/system/multi-user.target.wants/ndmspc-http@ngnt.service
rm -rf ${NDMSPC_INIT_DIR}/eos-ansible
dnf clean all