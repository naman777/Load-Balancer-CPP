"use client";

import { useState, useSyncExternalStore } from "react";
import {
  Braces,
  ChevronsDown,
  Feather,
  Gauge,
  HeartPulse,
  Layers,
  RefreshCw,
  Scale,
  Shuffle,
} from "lucide-react";
import { useStats } from "@/hooks/useStats";
import type { Algorithm } from "@/types/stats";
import { DEFAULT_LB_URL, lbUrlForHost } from "@/lib/backend";
import { ALGO_META } from "@/lib/constants";

import { Backdrop } from "@/components/backdrop";
import { Footer } from "@/components/footer";
import { Navbar } from "@/components/navbar";
import { SectionHead } from "@/components/section-head";
import { StatStrip } from "@/components/stat-strip";
import { Alert } from "@/components/ui/alert";
import { Button } from "@/components/ui/button";
import { Card, CardBody, CardText, CardTitle } from "@/components/ui/card";
import { Chip } from "@/components/ui/chip";
import { Field } from "@/components/ui/field";
import { Highlight } from "@/components/ui/highlight";
import { Input } from "@/components/ui/input";
import { LiveDot } from "@/components/ui/live-dot";
import { Spinner } from "@/components/ui/spinner";
import { Table, TableBody, TableCell, TableRow } from "@/components/ui/table";
import { Tabs, TabsContent, TabsList, TabsTrigger } from "@/components/ui/tabs";

import BackendCard from "@/components/BackendCard";
import AlgoSelector from "@/components/AlgoSelector";
import ConnectionChart from "@/components/ConnectionChart";
import TrafficSplit from "@/components/TrafficSplit";
import EventLog from "@/components/EventLog";
import JsonPanel from "@/components/JsonPanel";
import LoadTestPanel from "@/components/LoadTestPanel";

const REPO_URL = "https://github.com/naman777/Load-Balancer-CPP";

const PAGE_LINKS = [
  { label: "Backends", href: "#backends" },
  { label: "Load test", href: "#load-test" },
  { label: "Algorithms", href: "#algorithms" },
  { label: "Under the hood", href: "#internals" },
];

const FEATURES = [
  { icon: Shuffle, title: "Four algorithms", desc: "Least connections, round robin, IP hash and rendezvous hashing." },
  { icon: Scale, title: "Weighted backends", desc: "Per-backend traffic ratios." },
  { icon: HeartPulse, title: "Health checks", desc: "An HTTP HEAD to every backend, every 5 seconds." },
  { icon: Layers, title: "Thread pool", desc: "A fixed pool of 16 workers, no unbounded spawning." },
  { icon: Gauge, title: "Connection cap", desc: "A hard limit per backend, skipped at capacity." },
  { icon: RefreshCw, title: "SIGHUP reload", desc: "Live config changes with zero downtime." },
  { icon: Braces, title: "Stats endpoint", desc: "JSON served on :8081/stats." },
  { icon: Feather, title: "No libraries", desc: "Pure C++17 on POSIX sockets." },
];

const ARCH_THREADS = [
  { label: "Main thread", desc: "accept() loop on :8080" },
  { label: "Worker pool (16)", desc: "handle_client() for each connection" },
  { label: "Health check", desc: "HTTP HEAD every 5 s" },
  { label: "Stats thread", desc: "JSON on :8081" },
  { label: "SIGHUP thread", desc: "sigwait(), then reload" },
];

const ARCH_DECISIONS = [
  { label: "TOCTOU-free", desc: "One mutex covers select and increment" },
  { label: "atomic<Algorithm>", desc: "Lock-free reads in worker threads" },
  { label: "SO_REUSEADDR", desc: "Instant restart after a crash" },
  { label: "Retry on failure", desc: "Next healthy backend when a connect fails" },
  { label: "sigwait()", desc: "Safe async signal handling" },
];

const subscribeNever = () => () => {};

const wordmark = (
  <a href="#top" aria-label="Load balancer, back to top" className="font-onest text-xl font-medium tracking-tight">
    Load balancer<span className="text-muted-foreground">.cpp</span>
  </a>
);

export default function Home() {
  const [endpoint, setEndpoint] = useState(
    process.env.NEXT_PUBLIC_STATS_URL || "/api/stats"
  );
  const [algo, setAlgo] = useState<Algorithm>("lc");

  // LB frontend URL — used by the load test panel to fire requests
  // Match the backend domain to the domain the dashboard was opened on.
  const hostLbUrl = useSyncExternalStore(
    subscribeNever,
    () => lbUrlForHost(window.location.hostname),
    () => DEFAULT_LB_URL
  );
  const lbUrl = process.env.NEXT_PUBLIC_LB_URL || hostLbUrl;

  const { data, error, logs, connHistory, pollCount } = useStats(endpoint);

  const backends = data?.backends ?? [];
  const totalConn = backends.reduce((s, b) => s + b.connections, 0);
  const healthy = backends.filter((b) => b.healthy).length;
  const algoMeta = ALGO_META.find((a) => a.key === algo)!;

  const isConnected = !!data && !error;
  const isDisconnected = !!error;

  const status = isConnected ? (
    <LiveDot tone="success">Connected</LiveDot>
  ) : isDisconnected ? (
    <Chip tone="danger">Disconnected</Chip>
  ) : (
    <span className="inline-flex items-center gap-1.5 text-xs text-muted-foreground">
      <Spinner label="Connecting" />
      Connecting…
    </span>
  );

  return (
    <div id="top">
      <Backdrop />
      <Navbar
        logo={wordmark}
        links={[...PAGE_LINKS, { label: "Source on GitHub", href: REPO_URL, external: true }]}
        actions={<span className="mr-1 max-sm:hidden">{status}</span>}
      />

      <main className="mx-auto max-w-6xl p-6 sm:p-12">
        {/* ── Hero ──────────────────────────────────────────────── */}
        <section className="flex flex-col gap-y-2.5 sm:gap-y-4">
          <h1 className="text-[42px] font-semibold tracking-tight max-md:leading-11 md:text-6xl lg:text-7xl">
            A load balancer, written from scratch
          </h1>
          <p className="mt-4 max-w-3xl text-base text-neutral-700 sm:mt-6 md:text-xl dark:text-neutral-400">
            TCP and HTTP traffic spread across backends, in C++17 on plain POSIX sockets. This page reads{" "}
            <Highlight>live stats from the running server</Highlight> every two seconds.
          </p>
          <div className="mt-6 flex flex-wrap items-center gap-4">
            <Button asChild variant="solid">
              <a href="#load-test">Run a load test</a>
            </Button>
            <Button asChild variant="outline" iconRight={<ChevronsDown />}>
              <a href="#backends">See the backends</a>
            </Button>
          </div>
        </section>

        <StatStrip
          className="mt-12 sm:mt-16"
          stats={[
            { label: "Listen port", value: data ? String(data.port) : "--" },
            { label: "Healthy backends", value: data ? `${healthy} of ${backends.length}` : "--" },
            { label: "Active connections", value: data ? String(totalConn) : "--" },
            { label: "Polls so far", value: String(pollCount) },
          ]}
        />

        {/* ── Backends ──────────────────────────────────────────── */}
        <section id="backends" aria-labelledby="backends-title" className="mt-16 scroll-mt-24 sm:mt-20">
          <SectionHead id="backends-title" title="Backends">
            Each server gets an HTTP HEAD every five seconds, and{" "}
            <Highlight>unhealthy ones leave the rotation</Highlight> until they answer again.
          </SectionHead>

          <div className="mt-6 flex flex-col gap-4 sm:flex-row sm:items-end sm:justify-between">
            <Field label="Stats endpoint" className="sm:w-80">
              <Input
                className="font-mono"
                value={endpoint}
                onChange={(e) => setEndpoint(e.target.value)}
                placeholder="/api/stats"
              />
            </Field>
            <div className="flex h-9 items-center gap-3 text-sm text-muted-foreground">
              {status}
              <span className="h-4 w-px bg-border" aria-hidden="true" />
              Refreshes every 2 seconds
            </div>
          </div>

          {backends.length === 0 ? (
            <Alert
              className="mt-6"
              tone={error ? "danger" : "info"}
              title={error ? "Cannot reach the stats endpoint" : "Waiting for data"}
            >
              {error ? (
                <>
                  Start the load balancer with <code className="font-mono">./scripts/start.sh</code>, or point the
                  endpoint above at a running instance.
                </>
              ) : (
                "The first poll has not come back yet."
              )}
            </Alert>
          ) : (
            <div className="mt-6 grid gap-6 sm:grid-cols-2 lg:grid-cols-3">
              {backends.map((b, i) => (
                <BackendCard key={b.port} backend={b} index={i} totalConn={totalConn} />
              ))}
            </div>
          )}

          <div className="mt-6 grid gap-6 lg:grid-cols-2">
            <Card className="overflow-visible">
              <CardBody className="gap-4">
                <div>
                  <CardTitle>Traffic split</CardTitle>
                  <CardText>Share of active connections on each backend.</CardText>
                </div>
                <TrafficSplit backends={backends} />
              </CardBody>
            </Card>
            <Card>
              <CardBody className="gap-4">
                <div>
                  <CardTitle>Connections over time</CardTitle>
                  <CardText>Total active connections across all backends.</CardText>
                </div>
                <ConnectionChart history={connHistory} />
              </CardBody>
            </Card>
          </div>
        </section>

        {/* ── Load test ─────────────────────────────────────────── */}
        <section id="load-test" aria-labelledby="load-test-title" className="mt-16 scroll-mt-24 sm:mt-20">
          <SectionHead id="load-test-title" title="Load test">
            Fire concurrent HTTP requests at the load balancer and{" "}
            <Highlight>watch the traffic spread</Highlight> across the backends above.
          </SectionHead>
          <Card className="mt-6">
            <CardBody className="p-5 sm:p-6">
              <LoadTestPanel key={lbUrl} lbUrl={lbUrl} />
            </CardBody>
          </Card>
        </section>

        {/* ── Algorithms ────────────────────────────────────────── */}
        <section id="algorithms" aria-labelledby="algorithms-title" className="mt-16 scroll-mt-24 sm:mt-20">
          <SectionHead id="algorithms-title" title="Algorithms">
            Four ways to pick a backend, <Highlight>switchable without a restart</Highlight> by sending SIGHUP.
          </SectionHead>
          <div className="mt-6 grid items-start gap-6 lg:grid-cols-2">
            <Card>
              <CardBody className="p-5 sm:p-6">
                <AlgoSelector value={algo} onChange={setAlgo} />
              </CardBody>
            </Card>

            <div className="flex flex-col gap-6">
              <Card>
                <CardBody className="gap-3 p-5 sm:p-6">
                  <CardTitle>{algoMeta.label}</CardTitle>
                  <dl className="space-y-3 text-sm">
                    <div>
                      <dt className="text-xs text-muted-foreground">Best for</dt>
                      <dd>{algoMeta.useCase}</dd>
                    </div>
                    <div>
                      <dt className="text-xs text-muted-foreground">Cost</dt>
                      <dd>{algoMeta.complexity}</dd>
                    </div>
                    <div>
                      <dt className="text-xs text-muted-foreground">To run it</dt>
                      <dd>
                        Set <code className="font-mono text-[13px]">algo = {algo}</code> in{" "}
                        <code className="font-mono text-[13px]">lb.conf</code>, then run{" "}
                        <code className="font-mono text-[13px]">kill -HUP $(pgrep load_balancer)</code>.
                      </dd>
                    </div>
                  </dl>
                </CardBody>
              </Card>

              <Table>
                <TableBody>
                  {[
                    ["Listen port", data?.port ?? "--"],
                    ["Stats port", data ? data.port + 1 : "--"],
                    ["Thread pool", "16 workers"],
                    ["Health check", "Every 5 s"],
                    ["Connection: close", "Injected"],
                    ["SIGHUP reload", "Algorithm and weights"],
                  ].map(([k, v]) => (
                    <TableRow key={k}>
                      <TableCell className="text-muted-foreground">{k}</TableCell>
                      <TableCell className="text-right font-mono text-[13px]">{v}</TableCell>
                    </TableRow>
                  ))}
                </TableBody>
              </Table>
            </div>
          </div>
        </section>

        {/* ── Under the hood ────────────────────────────────────── */}
        <section id="internals" aria-labelledby="internals-title" className="mt-16 scroll-mt-24 sm:mt-20">
          <SectionHead id="internals-title" title="Under the hood">
            One process, five kinds of thread, and <Highlight>no third-party libraries</Highlight>.
          </SectionHead>

          <div className="mt-6 grid gap-6 lg:grid-cols-2">
            <Tabs defaultValue="log" className="min-w-0">
              <TabsList>
                <TabsTrigger value="log">Event log</TabsTrigger>
                <TabsTrigger value="json">Stats JSON</TabsTrigger>
              </TabsList>
              <TabsContent value="log">
                <EventLog logs={logs} />
              </TabsContent>
              <TabsContent value="json">
                <JsonPanel data={data} error={error} />
              </TabsContent>
            </Tabs>

            <div className="grid gap-6 sm:grid-cols-2">
              {[
                { title: "Thread model", rows: ARCH_THREADS },
                { title: "Design decisions", rows: ARCH_DECISIONS },
              ].map((group) => (
                <Card key={group.title}>
                  <CardBody className="gap-3">
                    <CardTitle>{group.title}</CardTitle>
                    <dl className="space-y-2.5">
                      {group.rows.map((r) => (
                        <div key={r.label}>
                          <dt className="font-mono text-[13px]">{r.label}</dt>
                          <dd className="text-xs text-muted-foreground">{r.desc}</dd>
                        </div>
                      ))}
                    </dl>
                  </CardBody>
                </Card>
              ))}
            </div>
          </div>

          <ul className="mt-10 grid gap-x-6 gap-y-7 sm:grid-cols-2 lg:grid-cols-4">
            {FEATURES.map((f) => (
              <li key={f.title} className="flex gap-3">
                <f.icon className="mt-0.5 size-4 shrink-0 text-muted-foreground" aria-hidden="true" />
                <div>
                  <h3 className="font-montserrat text-sm font-semibold">{f.title}</h3>
                  <p className="mt-0.5 text-xs leading-relaxed text-muted-foreground">{f.desc}</p>
                </div>
              </li>
            ))}
          </ul>
        </section>
      </main>

      <Footer
        className="mt-10"
        logo={wordmark}
        tagline="A TCP/HTTP load balancer in C++17"
        owner="Naman Kundra"
        sections={[
          { title: "On this page", links: PAGE_LINKS },
          {
            title: "Project",
            links: [
              { label: "Source on GitHub", href: REPO_URL, external: true },
              { label: "Readme", href: `${REPO_URL}#readme`, external: true },
            ],
          },
        ]}
      />
    </div>
  );
}
