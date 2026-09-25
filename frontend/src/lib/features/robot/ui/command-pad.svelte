<script lang="ts">
	import { onDestroy } from 'svelte';

	import { HoldCommandSender } from '../hold-command';
	import type { RobotCommand } from '../schema';

	type HoldButton = { code: number; label: string; hint: string };
	type TapButton = { code: number; label: string; tone: 'on' | 'off' };

	interface Props {
		channel: 'motor' | 'arm';
		/** 3x3 grid, row by row; null leaves the cell empty. */
		grid: (HoldButton | null)[];
		extra?: HoldButton[];
		taps?: TapButton[];
		disabled?: boolean;
		onError: (message: string | null) => void;
	}

	let { channel, grid, extra = [], taps = [], disabled = false, onError }: Props = $props();

	const sender = new HoldCommandSender((message) => onError(message));
	let holding = $state<number | null>(null);

	function press(event: PointerEvent, code: number) {
		if (disabled) return;
		// Keep receiving pointerup even if the finger slides off the button.
		(event.currentTarget as HTMLElement).setPointerCapture(event.pointerId);
		holding = code;
		sender.start({ channel, code } satisfies RobotCommand);
	}

	function release() {
		if (holding === null) return;
		holding = null;
		sender.release();
	}

	function tap(code: number) {
		if (!disabled) sender.tap({ channel, code });
	}

	// A hidden tab stops firing timers reliably; never leave the robot driving.
	function onVisibility() {
		if (document.hidden) release();
	}

	onDestroy(() => {
		release();
		sender.dispose();
	});
</script>

<svelte:window onblur={release} />
<svelte:document onvisibilitychange={onVisibility} />

<div class="pad" class:disabled>
	<div class="grid">
		{#each grid as cell, index (index)}
			{#if cell}
				<button
					type="button"
					class="hold"
					class:held={holding === cell.code}
					class:stop={cell.code === 0}
					aria-label={cell.hint}
					title={cell.hint}
					{disabled}
					onpointerdown={(event) => press(event, cell.code)}
					onpointerup={release}
					onpointercancel={release}
					oncontextmenu={(event) => event.preventDefault()}>{cell.label}</button
				>
			{:else}
				<span></span>
			{/if}
		{/each}
	</div>

	{#if extra.length || taps.length}
		<div class="row">
			{#each extra as button (button.code)}
				<button
					type="button"
					class="hold"
					class:held={holding === button.code}
					aria-label={button.hint}
					title={button.hint}
					{disabled}
					onpointerdown={(event) => press(event, button.code)}
					onpointerup={release}
					onpointercancel={release}
					oncontextmenu={(event) => event.preventDefault()}>{button.label}</button
				>
			{/each}
			{#each taps as button (button.code)}
				<button
					type="button"
					class={`tap ${button.tone}`}
					{disabled}
					onclick={() => tap(button.code)}>{button.label}</button
				>
			{/each}
		</div>
	{/if}

	<p class="hint">กดค้างเพื่อสั่ง ปล่อยแล้วหยุดทันที</p>
</div>

<style>
	.pad {
		display: flex;
		flex-direction: column;
		gap: 0.5rem;
	}
	.grid {
		display: grid;
		grid-template-columns: repeat(3, minmax(0, 1fr));
		gap: 0.4rem;
	}
	.row {
		display: flex;
		flex-wrap: wrap;
		gap: 0.4rem;
	}
	.row > button {
		flex: 1 1 5.5rem;
	}
	button {
		min-height: 3rem;
		border: 1px solid var(--border);
		background: var(--card);
		color: var(--foreground);
		border-radius: 0.35rem;
		font-size: 0.8rem;
		font-weight: 700;
		user-select: none;
		-webkit-user-select: none;
		touch-action: none;
		transition:
			background 0.12s ease,
			border-color 0.12s ease;
	}
	button:disabled {
		opacity: 0.45;
	}
	.hold.held {
		background: color-mix(in oklab, var(--primary), transparent 70%);
		border-color: var(--primary);
	}
	.hold.stop {
		color: var(--muted-foreground);
	}
	.tap.on {
		border-color: color-mix(in oklab, var(--primary), transparent 40%);
		color: var(--primary);
	}
	.tap.off {
		color: var(--muted-foreground);
	}
	.hint {
		margin: 0;
		font-size: 0.7rem;
		color: var(--muted-foreground);
	}
</style>
