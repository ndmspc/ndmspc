# ndmspc

## Running http stress

Via docker:

```bash
docker run -d --name ndmspc -p 8080:8080 registry.gitlab.com/ndmspc/ndmspc:latest ndmspc-cli serve stress
```

To get help:

```bash
docker run -d --name ndmspc -p 8080:8080 registry.gitlab.com/ndmspc/ndmspc:latest ndmspc-cli serve stress --help
```