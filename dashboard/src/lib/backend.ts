// The load balancer sits behind nginx on EC2 and is reachable on two domains:
//   /stats → stats endpoint (:8081),  everything else → LB frontend (:8080)
// The dashboard is served from load-balancer.<apex>, so each dashboard domain
// talks to the backend on its own apex.
const BACKENDS: Record<string, string> = {
  "namankundra.com": "https://lb-backend.namankundra.com",
  "naman.sbs": "https://lb-backend.naman.sbs",
};

export const DEFAULT_LB_URL = BACKENDS["namankundra.com"];

export function lbUrlForHost(host?: string | null): string {
  const hostname = (host ?? "").split(":")[0].toLowerCase();
  const apex = Object.keys(BACKENDS).find(
    (a) => hostname === a || hostname.endsWith(`.${a}`)
  );
  return apex ? BACKENDS[apex] : DEFAULT_LB_URL;
}
