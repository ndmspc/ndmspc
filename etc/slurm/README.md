## Prerequisites
ansible-galaxy install requirements.yml

## Generate munge key
```
dd if=/dev/urandom bs=1 count=1024 > munge.key
```

## Install/Update slurm
ansible-playbook -i hosts slurm.yaml

