<script lang="ts">
	import { resolve } from '$app/paths';
	import { Bot } from 'lucide-svelte';

	let { children } = $props();
</script>

<div class="min-h-[100dvh] px-3 py-3 text-foreground sm:px-5 sm:py-5">
	<header
		class="mx-auto mb-5 flex max-w-[1400px] items-center justify-between gap-4 border-b border-border/70 pb-3"
	>
		<a
			href={resolve('/control')}
			class="group flex min-w-0 items-center gap-3"
			aria-label="Durian Bot control"
		>
			<span
				class="grid size-10 shrink-0 place-items-center rounded-sm border border-primary/45 bg-primary/10 text-primary transition-transform duration-200 group-hover:-rotate-3"
			>
				<Bot class="size-5" strokeWidth={1.8} />
			</span>
			<span class="min-w-0">
				<span class="block truncate text-sm font-black tracking-[0.16em] text-foreground uppercase"
					>Durian Bot</span
				>
				<span
					class="block truncate font-mono text-[10px] tracking-[0.16em] text-muted-foreground uppercase"
					>Orchard field unit</span
				>
			</span>
		</a>
	</header>

	<main class="mx-auto flex max-w-[1400px] flex-col gap-5">
		{@render children?.()}
	</main>
</div>
