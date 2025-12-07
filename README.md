# ndmspc

# Running ndmspc via docker/podman

Run ndmspc with host networking

```bash
docker run -d --name ndmspc --net host registry.gitlab.com/ndmspc/ndmspc:latest
```

Run ndmspc with port mapping

```bash
docker run -d --name ndmspc -p 8080:8080 -p 8888:8888 -p 1094:1094 -p 1095:1095 registry.gitlab.com/ndmspc/ndmspc:latest
```

One can log into the container via

```bash
docker exec -it ndmspc /bin/bash
```

One can access

- the web interface at <http://localhost:8080>
- noteook at <http://localhost:8888>
- EOS can be accessed via root://localhost:1094

## Running http stress

Via docker:

```bash
docker run -d --name ndmspc -p 8080:8080 registry.gitlab.com/ndmspc/ndmspc:latest ndmspc-server start stress
```

To get help:

```bash
docker run --rm registry.gitlab.com/ndmspc/ndmspc:latest ndmspc-server stress --help
```
