# NDMPSC Monitoring Server

## Start the server

Run the monitoring HTTP server with:

```
ndmspc-server start ngnt -m macros/builtin/mon/httpNgntMon.C
```

By default, it runs on:

```
http://localhost:8080
```

---

## Submit a job

Use the helper script:

```
scripts/mon/ndmspc-sbatch.sh sleep 10
```

This will:

* submit a job to Slurm via `sbatch`
* register the job in the monitoring server (`POST /api/jobs`)
* automatically track task state (pending → running → done)

---

## Multiple tasks (Slurm arrays)

You can use standard Slurm array syntax:

```
scripts/mon/ndmspc-sbatch.sh -a 1-3 sleep 10
```

or:

```
scripts/mon/ndmspc-sbatch.sh --array 1-3,5 sleep 10
```

This results in:

* tasks `[1,2,3,5]`
* each task tracked separately in the server

---

## Passing Slurm options

Any `sbatch` options can be passed through:

```
scripts/mon/ndmspc-sbatch.sh -t 00:01:00 -p debug sleep 10
```

Example with everything:

```
scripts/mon/ndmspc-sbatch.sh -a 1-4 -t 00:02:00 sleep 20
```