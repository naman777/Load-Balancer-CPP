import * as React from 'react'

import { cn } from '@/lib/utils'

/**
 * A labelled form control with an optional hint and its own error line. It
 * links them for screen readers: the child gets the id, aria-describedby
 * and aria-invalid, so any input, textarea or select works inside it.
 */
function Label({ className, ...props }: React.ComponentProps<'label'>) {
  return <label data-slot="label" className={cn('text-sm font-medium', className)} {...props} />
}

function Field({
  label,
  hint,
  error,
  aside,
  className,
  children,
}: {
  label: React.ReactNode
  hint?: React.ReactNode
  error?: React.ReactNode
  /** On the label's right, e.g. "Forgot password?" or "Optional". */
  aside?: React.ReactNode
  className?: string
  children: React.ReactElement<React.HTMLAttributes<HTMLElement> & { id?: string }>
}) {
  const id = React.useId()
  const hintId = hint ? `${id}-hint` : undefined
  const errorId = error ? `${id}-error` : undefined
  return (
    <div data-slot="field" className={cn('space-y-1.5', className)}>
      <div className="flex items-baseline justify-between gap-3">
        <Label htmlFor={id}>{label}</Label>
        {aside && <span className="text-xs text-muted-foreground">{aside}</span>}
      </div>
      {React.cloneElement(children, {
        id,
        'aria-describedby': [hintId, errorId].filter(Boolean).join(' ') || undefined,
        'aria-invalid': error ? true : undefined,
      })}
      {hint && !error && (
        <p id={hintId} className="text-xs text-muted-foreground">
          {hint}
        </p>
      )}
      {error && (
        <p id={errorId} className="text-xs text-red-600 dark:text-red-400">
          {error}
        </p>
      )}
    </div>
  )
}

/** A group of related controls with a legend, such as a radio or checkbox set. */
function Fieldset({ legend, hint, error, className, children }: { legend: React.ReactNode; hint?: React.ReactNode; error?: React.ReactNode; className?: string; children: React.ReactNode }) {
  return (
    <fieldset data-slot="fieldset" className={cn('space-y-2.5', className)} aria-invalid={error ? true : undefined}>
      <legend className="mb-2.5 text-sm font-medium">{legend}</legend>
      {hint && !error && <p className="-mt-1.5 text-xs text-muted-foreground">{hint}</p>}
      {children}
      {error && <p className="text-xs text-red-600 dark:text-red-400">{error}</p>}
    </fieldset>
  )
}

export { Field, Fieldset, Label }
