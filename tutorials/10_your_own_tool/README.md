# Writing your own tool

> **Where this copy lives.** This is `tutorials/10_your_own_tool/` in the ndmspc repository —
> the copy that ships with the installation and the container image. The tutorial repository
> keeps the same files at `05_your_own_tool/`; the content is identical. The numbers differ
> because `tutorials/05_` is already `05_single_bining` here.

A tool is an **annotated ROOT macro**. The server loads it with `-m`, reads the
annotations, and derives the HTTP route, the WebSocket action, the MCP tool and the form in
the web interface from that one declaration. There is no plugin build step and no C++ to
recompile.

The full call is `Ndmspc::RegisterMcpTool(...)`:

| what you declare | what the server does with it |
| --- | --- |
| key `<namespace>/<group>/<action>` | the tool name, the `/api/...` route and the group in the UI |
| description, title, HTTP methods | what an agent and the interface show |
| `dependsOn` | an ordered pipeline — a step that runs after another |
| `inputSchema` | the form the UI renders, and the arguments an agent may pass |

Register the handler once; the transports are derived.

## Which ndmspc do I need?

**These macros require ndmspc v1.4.24 or newer.**

The Gaussian tools use `Ndmspc::Number<T>()` and `Ndmspc::ParentParams()`, two helpers that
ship in `ndmspc/http/NRouteContext.h` from v1.4.24 on. On an older build they do not exist
and the macro fails to compile:

```
error: no member named 'Number' in namespace 'Ndmspc'
```

Check your build before starting:

```bash
ndmspc-server --version    # needs v1.4.24 or newer
```

| what needs which release | |
| --- | --- |
| `Ndmspc::Number<T>()`, `Ndmspc::ParentParams()` | v1.4.24+ |
| `.root = true` on an action (`toolMyCustom.C`) | v1.4.21+ |
| `ndmspc-cli record` pacing and card flags | v1.4.23+ |
| the ngnt `spectra` action | v1.4.18+ |

The hello-world tool comes in two files because of that second line: on v1.4.18–v1.4.20 the
Explorer lists only groups whose actions declare a dependency, so a single action cannot be
the first step. `toolMyCustomNoRoot.C` keeps that explicit `start` step for those builds.

## One macro → three interfaces

The same handlers the UI calls are reachable three ways:

| transport | how |
| --- | --- |
| **HTTP** | `POST /api/<group>/<action>` |
| **WebSocket** | `/ws/root.websocket`, `message` / `message_reply` |
| **MCP** | `POST /api/mcp` — JSON-RPC 2.0 |

Authentication is an OIDC bearer token or X509 mTLS, plus room tokens (`rw` / `ro`). All
three share the same history, workspace and broadcasts as the browser.

```bash
curl -s localhost:8080/api/ndmspc/ngnt/open \
  -H 'Content-Type: application/json' \
  -d '{"file":"NSingleBinning01Gaus.root"}'
```

**MCP** implements `initialize`, `tools/list`, `tools/call` and `ping`, over stdio
(`ndmspc-mcp`) or Streamable HTTP at `POST /api/mcp`. An LLM with an MCP client can reach
your session directly — it sees only the tools you registered, and it works on the live
session you already have open.

---

## The hello world tool

Two versions of the same tool. **With v1.4.24 use `toolMyCustom.C`** — the second file is
there only for older builds.

| ndmspc | file | shape |
| --- | --- | --- |
| v1.4.21 or newer — **use this one** | `toolMyCustom.C` | one action, `hello/world/say`, with `.root = true` |
| v1.4.18–v1.4.20 only | `toolMyCustomNoRoot.C` | two actions: `hello/world/start` takes the Name, `hello/world/say` (`dependsOn` it) draws the markdown |

Before v1.4.21 the Explorer lists only groups whose actions declare a dependency, so a single
action cannot be its first step — hence the explicit `start` step. With `root`, the action says
it starts the combination itself, and the same tool is one form.

Run it:

```bash
cd "$(dirname "$(readlink -f "$(which ndmspc-server)")")/.."
ndmspc-server --no-default-tools -m 10_your_own_tool/toolMyCustom.C
# only on v1.4.18-v1.4.20:
# ndmspc-server --no-default-tools -m 10_your_own_tool/toolMyCustomNoRoot.C
```

Then open <http://localhost:8080>, go to the Explorer (group `hello/world`), type a name and press
**Save & Run**; pad1 shows `Hello <name>`.

`--no-default-tools` loads only your macro, so the server starts without waiting on the built-in
tools — that is the point of it here.

---

## The Gaussian tool

The second exercise runs a real ROOT function: `Gaus(entries, mean, sigma)` fills a histogram with
Gaussian random numbers and fits it.

```cpp
void Gaus(int nEntries = 10000, double mean = 0.0, double sigma = 1.0)
{
  TH1D *h = new TH1D("h", "Gaussian", 200, -10, 10);
  for (int i = 0; i < nEntries; i++) h->Fill(gRandom->Gaus(mean, sigma));
  TFitResultPtr fit = h->Fit("gaus", "QS");
  Printf("mean = %f ± %f, sigma = %f ± %f",
         fit->Parameter(1), fit->Error(1), fit->Parameter(2), fit->Error(2));
}
```

Three files, in the order you work through them:

| file | what it is |
| --- | --- |
| `toolGaus.C` | the skeleton — `Gaus(...)` is there, the tool around it is empty |
| `toolGausSolution.C` | the worked tool: two steps, `generate` → `draw` |
| `toolGausSolutionWithTour.C` | the same, with a guided tour declared on the first step |

Start from the skeleton. It registers nothing:

```cpp
void toolGaus()
{
  auto & handlers = *(Ndmspc::gNdmspcHttpHandlers);
  // TODO: register the action(s) here, then attach the handler(s).
}
```

Run it as it is — the server starts and offers **no tool**. That is the check that the
macro loads at all, before you add anything.

### Two steps, not one

Making the histogram and looking at it are different things, so they are different steps:

- **generate** — fills and fits, then writes the settings and the fitted values to a pad as
  markdown (`pad1` by default).
- **draw** — draws the histogram generate made into a pad of its own (`pad2` by default), so the
  log and the picture sit side by side.

`draw` declares `dependsOn: generate`. That makes the pair a **combination** — a tree — and
it needs no `root` flag: a dependency is what makes a group a tree, and `root` is only for a
tool with a single action.

Both worked versions take these same two steps, so nothing here depends on a particular
release.

| step | fields | pad |
| --- | --- | --- |
| `generate` | `entries`, `mean`, `sigma`, `pad` | `pad1` |
| `draw` | `pad` (reads what generate produced) | `pad2` |

What you get:

- **generate** → pad1 holds a sentence with your settings, then a table of
  `parameter | value | error`
- **draw** → pad2 holds the histogram, fit on top
- the Explorer shows the pair as a tree: generate, then draw under it
- change the numbers and press **Save & Run** again → a new step, a new histogram

```bash
cd "$(dirname "$(readlink -f "$(which ndmspc-server)")")/.."
ndmspc-server --no-default-tools -m 10_your_own_tool/toolGaus.C            # the skeleton
ndmspc-server --no-default-tools -m 10_your_own_tool/toolGausSolution.C    # the worked tool
ndmspc-server --no-default-tools -m 10_your_own_tool/toolGausSolutionWithTour.C   # with the tour
```

Open <http://localhost:8080>, go to the Explorer (group `myname/gaus`), set entries, mean and sigma,
press **Save & Run**, then run the **DRAW** step.

### A tool that explains itself

`toolGausSolutionWithTour.C` adds `.tutorial = …` to the first step — **data, not code**, an
ordered list of what to show:

```cpp
.tutorial = json{
  {"name", "Make a Gaussian and draw it"},
  {"description", "Generate a Gaussian, then draw it."},
  {"steps", json::array({
    json{{"show", "group"},      {"body", "…"}},
    json{{"show", "stepBlock"},  {"action", "myname/gaus/generate"},
         {"params", {{"entries", 10000}}}},
    json{{"show", "saveAndRun"}, {"body", "…"}},
    json{{"show", "pads"},       {"body", "…"}},
    // … then the draw step the same way
  })}};
```

With the tour version, **Help** (the `?` beside the room's title) lists *Make a Gaussian and
draw it* — walking it configures both steps, runs them, and ends on the result.

---

## Recording the tour

The tour is metadata, so a video of it can be made without anyone driving the page.

```bash
# once: the browser recorder (node 20+ first)
npm install -g agent-browser

# terminal 1: a server must be running — it declares the tours
ndmspc-server --no-default-tools -m 10_your_own_tool/toolGausSolutionWithTour.C

# terminal 2: what that server declares
ndmspc-cli record --list

# record one tour, 1080p, in a clean browser session
ndmspc-cli record \
  --tours "Make a Gaussian and draw it" \
  --url http://localhost:8080 \
  --session gaus-record \
  --out ./recordings
```

The tour decides **what** happens; the flags decide how it reads:

| flag | what it sets |
| --- | --- |
| `--speed` | scales every pause (2 = twice as fast) |
| `--step-pause` | rest after each stage (900 ms) |
| `--click-pause` | rest after a click (700 ms) |
| `--start-delay` | hold before the first stage (2500 ms) |
| `--tail` | keep recording at the end (2500 ms) |
| `--cursor-start` | where the cursor starts (`center`, or a point) |
| `--cursor-travel` | how long the pointer takes to reach a target |
| `--title`, `--subtitle` | the intro card |
| `--outro`, `--credit` | the closing card and its credit line |
| `--no-cards` | skip the intro and outro cards |

This needs **v1.4.23 or newer** — the pacing, card and cursor flags all ship in that
release. You also need `agent-browser` installed and a server running: the tours are
declared by the macro, so there is nothing to record until the server that loaded it is up.

---

## Using the tool from an AI agent

Because the tools are MCP, an agent with an MCP client can drive the same session.

```bash
cmd mcp add --transport http ndmspc http://localhost:8080/api/mcp
cmd mcp list
```

Then ask in words, not in JSON:

```
Generate a Gaussian with 20000 entries, mean 2.5
and sigma 1.5, then draw it into pad2.

Use only the tools you actually have — do not invent
tool names, arguments or results.

Tell me the fitted mean and sigma.
```

The pads refresh while it works: pad1 gets the markdown log, pad2 the fitted histogram, and
the Explorer gains one combination named after the settings. That last line is worth keeping
in any prompt you write — an agent that cannot find a tool will otherwise describe what it
would have done, and the guard turns that into an error you can see.

---

## What to remember

- A tool is an annotated ROOT macro; `RegisterMcpTool` is the whole declaration.
- One declaration → HTTP route, WebSocket action, MCP tool and UI form.
- `dependsOn` makes a group a combination; `root` is only for a single action.
- The pads are where a tool reports — markdown for numbers, jsroot for pictures.
- An MCP client can drive the same live session, so the tool is usable by an agent as well
  as by a person.
