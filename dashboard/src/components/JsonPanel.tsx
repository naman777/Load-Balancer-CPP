"use client";

import styles from "./JsonPanel.module.css";

interface Props {
  data: unknown;
  error: unknown;
}

function syntaxHighlight(json: string): string {
  return json
    .replace(/&/g, "&amp;")
    .replace(/</g, "&lt;")
    .replace(/>/g, "&gt;")
    .replace(
      /("(\\u[a-zA-Z0-9]{4}|\\[^u]|[^\\"])*"(\s*:)?|\b(true|false|null)\b|-?\d+\.?\d*(?:[eE][+-]?\d+)?)/g,
      (match) => {
        if (/^"/.test(match)) {
          if (/:$/.test(match)) return `<span class="jKey">${match}</span>`;
          return `<span class="jStr">${match}</span>`;
        }
        if (/true/.test(match)) return `<span class="jTrue">${match}</span>`;
        if (/false/.test(match)) return `<span class="jFalse">${match}</span>`;
        return `<span class="jNum">${match}</span>`;
      }
    );
}

export default function JsonPanel({ data, error }: Props) {
  if (error) {
    return (
      <div className={styles.block}>
        <span className={styles.err}>
          {`Cannot reach stats endpoint\n\nStart the LB:\n  ./scripts/start.sh\n\nOr manually:\n  ./load-balancer/load_balancer --config lb.conf`}
        </span>
      </div>
    );
  }

  if (!data) {
    return (
      <div className={styles.block}>
        <span className={styles.muted}>Waiting for data…</span>
      </div>
    );
  }

  const highlighted = syntaxHighlight(JSON.stringify(data, null, 2));

  return (
    <div
      className={styles.block}
      dangerouslySetInnerHTML={{ __html: highlighted }}
    />
  );
}
