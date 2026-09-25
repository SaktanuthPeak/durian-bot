import { sendRobotCommand } from './api';
import type { RobotCommand } from './schema';

/**
 * Firmware drops a serial command after 1000 ms unless it is repeated, so a held
 * button re-sends its command well inside that window. Releasing sends STOP at once
 * instead of waiting for the timeout.
 */
export const HOLD_REPEAT_MS = 200;

export class HoldCommandSender {
	private timer: ReturnType<typeof setInterval> | null = null;
	private active: RobotCommand | null = null;
	private inFlight = false;

	constructor(private readonly onError: (message: string | null) => void) {}

	get holding(): RobotCommand | null {
		return this.active;
	}

	start(command: RobotCommand) {
		this.clear();
		this.active = command;
		void this.fire(command);
		this.timer = setInterval(() => void this.fire(command), HOLD_REPEAT_MS);
	}

	/** Stop repeating and send the channel's STOP. */
	release() {
		const command = this.active;
		this.clear();
		if (command) void this.fire({ channel: command.channel, code: 0 }, true);
	}

	/** One-shot command, e.g. pump on/off. */
	tap(command: RobotCommand) {
		void this.fire(command, true);
	}

	dispose() {
		this.clear();
	}

	private clear() {
		if (this.timer) clearInterval(this.timer);
		this.timer = null;
		this.active = null;
	}

	// Skip a repeat while the previous request is still pending so a slow link does
	// not queue up stale commands; STOP and taps always go out.
	private async fire(command: RobotCommand, force = false) {
		if (this.inFlight && !force) return;
		this.inFlight = true;
		try {
			await sendRobotCommand(command);
			this.onError(null);
		} catch (cause) {
			this.onError(cause instanceof Error ? cause.message : 'Command failed');
		} finally {
			this.inFlight = false;
		}
	}
}
