'use client'

import * as RadioGroupPrimitive from '@radix-ui/react-radio-group'
import * as React from 'react'

import { cn } from '@/lib/utils'

/**
 * One choice from a few. Arrow keys move between options. The chosen dot is
 * the neutral primary, never orange. Pass `options` for the common case, or
 * compose RadioGroupItem yourself.
 */
function RadioGroup({
  options,
  className,
  children,
  ...props
}: React.ComponentProps<typeof RadioGroupPrimitive.Root> & { options?: { value: string; label: React.ReactNode; hint?: React.ReactNode; disabled?: boolean }[] }) {
  return (
    <RadioGroupPrimitive.Root data-slot="radio-group" className={cn('grid gap-2.5', className)} {...props}>
      {options?.map((o) => (
        <label key={o.value} className="flex cursor-pointer items-start gap-3 text-sm has-[[data-disabled]]:cursor-not-allowed has-[[data-disabled]]:opacity-50">
          <RadioGroupItem value={o.value} disabled={o.disabled} className="mt-0.5" />
          <span>
            {o.label}
            {o.hint && <span className="mt-0.5 block text-xs text-muted-foreground">{o.hint}</span>}
          </span>
        </label>
      ))}
      {children}
    </RadioGroupPrimitive.Root>
  )
}

function RadioGroupItem({ className, ...props }: React.ComponentProps<typeof RadioGroupPrimitive.Item>) {
  return (
    <RadioGroupPrimitive.Item
      data-slot="radio-group-item"
      className={cn(
        'grid size-4 shrink-0 cursor-pointer place-items-center rounded-full border border-gray-300 transition-colors outline-none focus-visible:ring-[3px] focus-visible:ring-ring/50 disabled:cursor-not-allowed data-[state=checked]:border-primary dark:border-gray-700 dark:bg-black/50',
        className,
      )}
      {...props}
    >
      <RadioGroupPrimitive.Indicator className="size-2 rounded-full bg-primary" />
    </RadioGroupPrimitive.Item>
  )
}

export { RadioGroup, RadioGroupItem }
