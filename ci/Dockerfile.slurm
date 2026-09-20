# syntax=docker/dockerfile:1
# SPDX-FileCopyrightText: Copyright (C) 2026 ndmspc
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# The Slurm worker (compute node) image: the ndmspc base image (AlmaLinux 10 + ndmspc
# + ROOT, with EPEL, CRB, the ndmspc COPR and tini already in place) plus Slurm
# itself, the slurmd daemon and the Slinky supervisord/entrypoint plumbing.
#
# It is the only Slurm image ndmspc builds, so the file is a single stage: nothing is
# shared with another target to justify splitting it, and the stage is named 'slurmd'
# to match the target the CI builds.
#
# The control plane (slurmctld, slurmdbd, slurmrestd, login) comes from the upstream
# Slinky images, which the devops project mirrors into
# registry.gitlab.com/ndmspc/devops.
#
# Slurm itself comes from the ndmspc COPR: it must carry the 26.05 series (EPEL's
# Slurm only exists for EPEL 10.3/10.4 and the Slinky chart wants >= 25.11), which the
# assertion below enforces.
#
# The plumbing mirrors the upstream Slinky images (SlinkyProject/containers) and is
# fetched from the pinned ref below, so the chart/operator contract is identical:
# supervisord runs the daemon, the 'slinky' authselect profile provides sssd +
# libnss_slurm, and slurmd-entrypoint.sh consumes SLURMD_OPTIONS/SSHD_OPTIONS/
# SSSD_OPTIONS/PAM_SLURM_ADOPT_OPTIONS and the operator's POD_* variables.
#
#   podman build --format docker -f ci/Dockerfile.slurm --target slurmd -t $REG/slurmd:$TAG .
#
# Then deploy with the devops role (slurm-worker-image does this on every ndmspc tag):
#   -e slurm_slurmd_image=$REG/slurmd:$TAG

ARG NDMSPC_BASE=registry.gitlab.com/ndmspc/ndmspc/base:next
ARG SLINKY_REF=main
ARG SLINKY_FILES=https://raw.githubusercontent.com/SlinkyProject/containers/${SLINKY_REF}/schedmd/slurm/26.05/rockylinux9/files
# Slurm series the image must provide; the build fails when the repo carries
# something else (e.g. ndmspc/stable only shipping 24.05).
ARG SLURM_SERIES=26.05

FROM ${NDMSPC_BASE} AS slurmd
SHELL ["bash", "-c"]
USER root
WORKDIR /tmp/
ARG SLINKY_FILES
ARG SLURM_SERIES

# The base carries EPEL, CRB, the COPR and tini but no Slurm (a room only needs the
# REST API, see ci/Dockerfile.base), so this stage installs Slurm itself, on top of
# what the worker needs: supervisord (which runs the daemon) and the small tools the
# Slinky scripts use, the slurmd stack with sssd (identity) and sshd (node shell
# access), and openmpi for MPI jobs. No PMI package: upstream dropped slurm-pmi in
# 21.08 in favour of pmix (slurm-libs owns libpmi.so.0/libpmi2.so.0 and pmix is a
# Requires: of slurm). The SlurmUser/SlurmdUser account the jobs run as (401) is
# created here too — the base no longer has it, and the slurm packages the worker
# needs do not create it either.
RUN set -euxo pipefail; \
  dnf -y install --setopt='install_weak_deps=False' \
  supervisor curl procps-ng iputils ncurses \
  gawk socat \
  openssh-server \
  authselect sssd sssd-ad sssd-idp sssd-ipa sssd-krb5 sssd-ldap \
  krb5-pkinit \
  slurm slurm-slurmd slurm-pam_slurm slurm-nss_slurm \
  openmpi; \
  dnf clean all; \
  got="$(rpm -q --qf '%{VERSION}' slurm | cut -d. -f1,2)"; \
  if [ "$got" != "${SLURM_SERIES}" ]; then \
  echo "ERROR: slurm ${got} installed, but ${SLURM_SERIES} is required." >&2; \
  echo "Add the ${SLURM_SERIES} series to the ndmspc COPR (or pass --build-arg SLURM_SERIES=${got})." >&2; \
  exit 1; \
  fi; \
  getent group slurm >/dev/null || groupadd --system --gid 401 slurm; \
  getent passwd slurm >/dev/null || \
  useradd --system --no-log-init --uid 401 --gid 401 --shell /usr/sbin/nologin slurm; \
  mkdir -p /etc/supervisord.d /usr/local/bin /usr/local/entrypoint.d \
  /var/spool/slurmd /etc/authselect; \
  authselect create-profile slinky -b sssd; \
  sed -i -E '/^(passwd|group):/ { /slurm/! s/(files)/\1 slurm/g }' /etc/authselect/custom/slinky/nsswitch.conf; \
  authselect select custom/slinky with-mkhomedir --force; \
  rm -f /etc/ssh/ssh_host_*

# The upstream plumbing, straight to where the chart's supervisord/entrypoint contract
# expects it and with its final modes: the entrypoint is slurmd-entrypoint.sh under its
# installed name (entrypoint.sh), and fakesystemd is what makes systemd-style unit
# files work in the container.
RUN set -euxo pipefail; \
  for f in \
  etc/supervisord.conf \
  etc/supervisord.d/fakesystemd.ini \
  etc/supervisord.d/slurmd.ini \
  etc/supervisord.d/sshd.ini \
  etc/supervisord.d/sssd.ini \
  usr/local/bin/fakesystemd.sh \
  usr/local/bin/slurmd-entrypoint.sh; do \
  curl -fsSL --retry 3 --retry-delay 2 "${SLINKY_FILES}/${f}" -o "/${f}"; \
  done; \
  chmod 0644 /etc/supervisord.conf /etc/supervisord.d/*.ini; \
  chmod 0755 /usr/local/bin/fakesystemd.sh; \
  install -m0755 /usr/local/bin/slurmd-entrypoint.sh /usr/local/bin/entrypoint.sh; \
  rm -f /usr/local/bin/slurmd-entrypoint.sh
EXPOSE 6818/tcp 22/tcp
ENTRYPOINT ["/usr/local/bin/entrypoint.sh"]
