# syntax=docker/dockerfile:1
# SPDX-FileCopyrightText: Copyright (C) 2026 ndmspc
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# Slurm images on top of the ndmspc base image (AlmaLinux 10 + ndmspc + ROOT).
#
# Slurm comes from the ndmspc COPR (the base image enables it ahead of EPEL, and it
# must carry the 26.05 series - EPEL's Slurm only exists for EPEL 10.3/10.4 and the
# Slinky chart wants >= 25.11). The supervisord/entrypoint plumbing mirrors the
# upstream Slinky images (SlinkyProject/containers) and is fetched from the pinned
# ref below, so the chart/operator contract is identical: supervisord runs the
# daemons, the 'slinky' authselect profile provides sssd + libnss_slurm, and
# slurmd-entrypoint.sh consumes SLURMD_OPTIONS/SSHD_OPTIONS/SSSD_OPTIONS/
# PAM_SLURM_ADOPT_OPTIONS and the operator's POD_* variables.
#
# Build one target per component and push it under the name the Slinky chart
# expects (slurmctld, slurmd, slurmdbd, slurmrestd, login), e.g.:
#
#   podman build --format docker -f ci/Dockerfile.slurm --target slurmd -t $REG/slurmd:$TAG .
#
# Then deploy with the devops role:
#   -e slurm_image_registry=$REG -e slurm_component_image_tag=$TAG
# (the slurm-operator/webhook images stay upstream ghcr.io/slinkyproject).

ARG NDMSPC_BASE=registry.gitlab.com/ndmspc/ndmspc/base:next
ARG SLINKY_REF=main
ARG SLINKY_FILES=https://raw.githubusercontent.com/SlinkyProject/containers/${SLINKY_REF}/schedmd/slurm/26.05/rockylinux9/files
# Slurm series the images must provide; the build fails when the repo carries
# something else (e.g. ndmspc/stable only shipping 24.05).
ARG SLURM_SERIES=26.05

################################################################################
FROM ${NDMSPC_BASE} AS base
SHELL ["bash", "-c"]
USER root
WORKDIR /tmp/
ARG SLURM_SERIES

# tini is not packaged in EPEL 10; the ndmspc base ships it from GitHub, so fetch
# it only when absent (the slurmdbd/slurmrestd entrypoints use /usr/bin/tini).
RUN set -euxo pipefail; \
    dnf -y install --setopt='install_weak_deps=False' \
      epel-release 'dnf-command(config-manager)'; \
    dnf config-manager --enable crb; \
    dnf -y install --setopt='install_weak_deps=False' \
      supervisor curl procps-ng iputils ncurses \
      slurm; \
    dnf clean all; \
    command -v tini >/dev/null 2>&1 || { \
      curl -fsSL https://github.com/krallin/tini/releases/download/v0.19.0/tini -o /usr/bin/tini; \
      chmod +x /usr/bin/tini; \
    }; \
    got="$(rpm -q --qf '%{VERSION}' slurm | cut -d. -f1,2)"; \
    if [ "$got" != "${SLURM_SERIES}" ]; then \
      echo "ERROR: slurm ${got} installed, but ${SLURM_SERIES} is required." >&2; \
      echo "Add the ${SLURM_SERIES} series to the ndmspc COPR (or pass --build-arg SLURM_SERIES=${got})." >&2; \
      exit 1; \
    fi; \
    groupadd --system --gid 401 slurm; \
    useradd --system --no-log-init --uid 401 --gid 401 --shell /usr/sbin/nologin slurm; \
    mkdir -p /etc/supervisord.d /usr/local/bin /usr/local/entrypoint.d

################################################################################
# Worker and login images get MPI (like the upstream images).
FROM base AS base-extra
RUN set -euxo pipefail; \
    dnf -y install --setopt='install_weak_deps=False' openmpi; \
    dnf clean all

################################################################################
FROM base AS slurmctld
ARG SLINKY_FILES
RUN set -euxo pipefail; \
    dnf -y install --setopt='install_weak_deps=False' \
      socat \
      authselect sssd sssd-ad sssd-idp sssd-ipa sssd-krb5 sssd-ldap \
      krb5-pkinit \
      slurm-slurmctld; \
    dnf clean all; \
    mkdir -p /etc/authselect; \
    authselect create-profile slinky -b sssd; \
    authselect select custom/slinky --force
ADD ${SLINKY_FILES}/etc/supervisord.conf /etc/supervisord.conf
ADD ${SLINKY_FILES}/etc/supervisord.d/slurmctld.ini /etc/supervisord.d/slurmctld.ini
ADD ${SLINKY_FILES}/etc/supervisord.d/fakesystemd.ini /etc/supervisord.d/fakesystemd.ini
ADD ${SLINKY_FILES}/etc/supervisord.d/sssd.ini /etc/supervisord.d/sssd.ini
ADD ${SLINKY_FILES}/usr/local/bin/fakesystemd.sh /usr/local/bin/fakesystemd.sh
ADD ${SLINKY_FILES}/usr/local/bin/slurmctld-entrypoint.sh /usr/local/bin/entrypoint.sh
# The chart runs the pods as uid/gid 401 (runAsNonRoot), and ADD-from-URL gives
# 0600 - so make the scripts/t (0755) and configs (0644) usable by that user.
RUN chmod 0755 /usr/local/bin/fakesystemd.sh /usr/local/bin/entrypoint.sh; \
    chmod 0644 /etc/supervisord.conf /etc/supervisord.d/*.ini
EXPOSE 6817/tcp
ENTRYPOINT ["/usr/local/bin/entrypoint.sh"]

################################################################################
FROM base-extra AS slurmd
ARG SLINKY_FILES
RUN set -euxo pipefail; \
    dnf -y install --setopt='install_weak_deps=False' \
      gawk socat \
      openssh-server \
      authselect sssd sssd-ad sssd-idp sssd-ipa sssd-krb5 sssd-ldap \
      krb5-pkinit \
      slurm-slurmd slurm-pam_slurm slurm-nss_slurm; \
    dnf -y install --setopt='install_weak_deps=False' --setopt=strict=0 slurm-pmi \
      || echo "WARNING: slurm-pmi not packaged - libpmi/libpmi2 will be absent (PMIx is still available)"; \
    dnf clean all; \
    mkdir -p /var/spool/slurmd /etc/authselect /usr/local/entrypoint.d; \
    authselect create-profile slinky -b sssd; \
    sed -i -E '/^(passwd|group):/ { /slurm/! s/(files)/\1 slurm/g }' /etc/authselect/custom/slinky/nsswitch.conf; \
    authselect select custom/slinky with-mkhomedir --force; \
    rm -f /etc/ssh/ssh_host_*
ADD ${SLINKY_FILES}/etc/supervisord.conf /etc/supervisord.conf
ADD ${SLINKY_FILES}/etc/supervisord.d/fakesystemd.ini /etc/supervisord.d/fakesystemd.ini
ADD ${SLINKY_FILES}/etc/supervisord.d/slurmd.ini /etc/supervisord.d/slurmd.ini
ADD ${SLINKY_FILES}/etc/supervisord.d/sshd.ini /etc/supervisord.d/sshd.ini
ADD ${SLINKY_FILES}/etc/supervisord.d/sssd.ini /etc/supervisord.d/sssd.ini
ADD ${SLINKY_FILES}/usr/local/bin/fakesystemd.sh /usr/local/bin/fakesystemd.sh
ADD ${SLINKY_FILES}/usr/local/bin/slurmd-entrypoint.sh /usr/local/bin/entrypoint.sh
# The chart runs the pods as uid/gid 401 (runAsNonRoot), and ADD-from-URL gives
# 0600 - so make the scripts/t (0755) and configs (0644) usable by that user.
RUN chmod 0755 /usr/local/bin/fakesystemd.sh /usr/local/bin/entrypoint.sh; \
    chmod 0644 /etc/supervisord.conf /etc/supervisord.d/*.ini
EXPOSE 6818/tcp 22/tcp
ENTRYPOINT ["/usr/local/bin/entrypoint.sh"]

################################################################################
FROM base AS slurmdbd
RUN set -euxo pipefail; \
    dnf -y install --setopt='install_weak_deps=False' slurm-slurmdbd; \
    dnf clean all
EXPOSE 6819/tcp
ENTRYPOINT ["/usr/bin/tini", "-g", "--", "/usr/sbin/slurmdbd", "-D"]

################################################################################
FROM base AS slurmrestd
RUN set -euxo pipefail; \
    dnf -y install --setopt='install_weak_deps=False' slurm-slurmrestd; \
    dnf clean all
EXPOSE 6820/tcp
ENTRYPOINT ["/usr/bin/tini", "-g", "--", "/usr/sbin/slurmrestd"]
CMD ["0.0.0.0:6820"]

################################################################################
FROM base-extra AS sackd
ARG SLINKY_FILES
RUN set -euxo pipefail; \
    K8S_VERSION="$(curl -fsSL https://dl.k8s.io/release/stable.txt | grep -Eo '^v[0-9]+\.[0-9]+')"; \
    printf '[kubernetes]\nname=Kubernetes\nbaseurl=https://pkgs.k8s.io/core:/stable:/%s/rpm/\nenabled=1\ngpgcheck=1\ngpgkey=https://pkgs.k8s.io/core:/stable:/%s/rpm/repodata/repomd.xml.key\n' \
      "$K8S_VERSION" "$K8S_VERSION" > /etc/yum.repos.d/kubernetes.repo; \
    dnf -y install --setopt='install_weak_deps=False' kubectl socat slurm-sackd; \
    dnf clean all; \
    mkdir -p /run/slurm /etc/supervisord.d
ADD ${SLINKY_FILES}/etc/supervisord.conf /etc/supervisord.conf
ADD ${SLINKY_FILES}/etc/supervisord.d/sackd.ini /etc/supervisord.d/sackd.ini
ADD ${SLINKY_FILES}/etc/supervisord.d/fakesystemd.ini /etc/supervisord.d/fakesystemd.ini
ADD ${SLINKY_FILES}/usr/local/bin/fakesystemd.sh /usr/local/bin/fakesystemd.sh
ADD ${SLINKY_FILES}/usr/local/bin/sackd-entrypoint.sh /usr/local/bin/entrypoint.sh
# The chart runs the pods as uid/gid 401 (runAsNonRoot), and ADD-from-URL gives
# 0600 - so make the scripts/t (0755) and configs (0644) usable by that user.
RUN chmod 0755 /usr/local/bin/fakesystemd.sh /usr/local/bin/entrypoint.sh; \
    chmod 0644 /etc/supervisord.conf /etc/supervisord.d/*.ini
ENTRYPOINT ["/usr/local/bin/entrypoint.sh"]

################################################################################
FROM sackd AS login
ARG SLINKY_FILES
RUN set -euxo pipefail; \
    dnf -y install --setopt='install_weak_deps=False' \
      socat openssh-server \
      authselect sssd sssd-ad sssd-idp sssd-ipa sssd-krb5 sssd-ldap krb5-pkinit; \
    dnf clean all; \
    mkdir -p /etc/authselect /usr/local/entrypoint.d; \
    authselect create-profile slinky -b sssd; \
    authselect select custom/slinky with-mkhomedir --force; \
    rm -f /etc/ssh/ssh_host_*
ADD ${SLINKY_FILES}/etc/supervisord.conf /etc/supervisord.conf
ADD ${SLINKY_FILES}/etc/supervisord.d/fakesystemd.ini /etc/supervisord.d/fakesystemd.ini
ADD ${SLINKY_FILES}/etc/supervisord.d/sackd.ini /etc/supervisord.d/sackd.ini
ADD ${SLINKY_FILES}/etc/supervisord.d/sshd.ini /etc/supervisord.d/sshd.ini
ADD ${SLINKY_FILES}/etc/supervisord.d/sssd.ini /etc/supervisord.d/sssd.ini
ADD ${SLINKY_FILES}/usr/local/bin/fakesystemd.sh /usr/local/bin/fakesystemd.sh
ADD ${SLINKY_FILES}/usr/local/bin/login-entrypoint.sh /usr/local/bin/entrypoint.sh
# The chart runs the pods as uid/gid 401 (runAsNonRoot), and ADD-from-URL gives
# 0600 - so make the scripts/t (0755) and configs (0644) usable by that user.
RUN chmod 0755 /usr/local/bin/fakesystemd.sh /usr/local/bin/entrypoint.sh; \
    chmod 0644 /etc/supervisord.conf /etc/supervisord.d/*.ini
EXPOSE 22/tcp
ENTRYPOINT ["/usr/local/bin/entrypoint.sh"]
