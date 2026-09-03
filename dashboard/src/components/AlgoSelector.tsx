"use client";

import styles from "./AlgoSelector.module.css";
import { ALGO_META } from "@/lib/constants";
import type { Algorithm } from "@/types/stats";

interface Props {
  value: Algorithm;
  onChange: (a: Algorithm) => void;
}

export default function AlgoSelector({ value, onChange }: Props) {
  return (
    <div className={styles.wrapper}>
      {ALGO_META.map((algo) => (
        <button
          key={algo.key}
          className={`${styles.option} ${value === algo.key ? styles.active : ""}`}
          onClick={() => onChange(algo.key)}
          type="button"
          id={`algo-btn-${algo.key}`}
        >
          <div className={styles.radio}>
            <div className={styles.radioDot} />
          </div>
          <div className={styles.content}>
            <div className={styles.label}>
              {algo.label}
              <span className={styles.key}>{algo.key}</span>
            </div>
            <div className={styles.desc}>{algo.description}</div>
            <div className={styles.useCase}>
              <span className={styles.useCaseLabel}>Best for: </span>
              {algo.useCase}
            </div>
            <div className={styles.complexity}>{algo.complexity}</div>
          </div>
        </button>
      ))}
    </div>
  );
}
