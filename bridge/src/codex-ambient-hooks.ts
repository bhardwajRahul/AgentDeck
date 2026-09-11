/**
 * Codex Desktop's ambient-suggestions threads are not the user's work.
 *
 * When the desktop app refreshes `~/.codex/ambient-suggestions/<project-hash>/
 * ambient-suggestions.json` it runs two internal prompts ("Generate 0 to 3
 * hyperpersonalized suggestions…" and a "safety and compliance" review of the
 * result) on throw-away threads: no rollout file, no row in Codex's own
 * `threads` table, hook `cwd` of `/`. The user-global lifecycle hooks still fire
 * for them, so the daemon minted a `codex-cli` session row, a `chat_start` /
 * `chat_response` timeline pair and a two-second APME turn for every refresh —
 * 27 such turns between 2026-07-06 and 2026-09-11, 10 of them in one night.
 *
 * The prompt text is the only durable signature: the hook payload carries no
 * "background" flag and `cwd: "/"` alone would also match a user who really
 * opened Codex at the filesystem root. A thread is classified on its
 * `codex_user_prompt_submit`, then every later hook on that thread id is
 * treated as background too (the tool/stop hooks carry no prompt). The
 * `codex_session_start` that precedes the prompt by ~90 ms has already opened
 * an APME run and a hook-derived session row — the caller retracts both.
 *
 * Vectors: `shared/codex-ambient-vectors.json`, replayed by this suite and the
 * Swift `CodexAmbientHookRules` tests.
 */

export const CODEX_AMBIENT_PROMPT_PATTERNS: readonly RegExp[] = [
  /^\s*Overview\s+Generate 0 to 3 hyperpersonalized suggestions\b/i,
  /^\s*You are an expert at upholding safety and compliance standards for Codex ambient suggestions\b/i,
];

/** Prompt text as Codex hooks carry it (`prompt`, some builds `user_prompt`). */
export function codexHookPromptText(json: Record<string, unknown>): string {
  if (typeof json.prompt === 'string') return json.prompt;
  if (typeof json.user_prompt === 'string') return json.user_prompt;
  const message = json.message as Record<string, unknown> | undefined;
  if (message && typeof message.content === 'string') return message.content;
  return '';
}

export function isCodexAmbientPrompt(prompt: unknown): boolean {
  if (typeof prompt !== 'string' || !prompt.trim()) return false;
  return CODEX_AMBIENT_PROMPT_PATTERNS.some((re) => re.test(prompt));
}

export interface CodexAmbientVerdict {
  /** This hook belongs to a background thread: drop it from every pipeline. */
  ambient: boolean;
  /** First hook that identified the thread — the caller retracts what the
   *  thread's `codex_session_start` already created. */
  firstSeen: boolean;
  sessionId?: string;
}

const NOT_AMBIENT: CodexAmbientVerdict = { ambient: false, firstSeen: false };

/** Default TTL: a background thread that fell silent for this long is forgotten. */
export const CODEX_AMBIENT_TTL_MS = 30 * 60_000;

export class CodexAmbientSessions {
  private readonly lastSeenAt = new Map<string, number>();

  constructor(private readonly ttlMs: number = CODEX_AMBIENT_TTL_MS) {}

  classify(eventName: string, json: Record<string, unknown>, now = Date.now()): CodexAmbientVerdict {
    if (!eventName.startsWith('codex_')) return NOT_AMBIENT;
    const sessionId = typeof json.session_id === 'string' && json.session_id ? json.session_id : '';
    if (!sessionId) return NOT_AMBIENT;
    this.reap(now);
    if (this.lastSeenAt.has(sessionId)) {
      this.lastSeenAt.set(sessionId, now);
      return { ambient: true, firstSeen: false, sessionId };
    }
    if (eventName === 'codex_user_prompt_submit' && isCodexAmbientPrompt(codexHookPromptText(json))) {
      this.lastSeenAt.set(sessionId, now);
      return { ambient: true, firstSeen: true, sessionId };
    }
    return NOT_AMBIENT;
  }

  isAmbient(sessionId: string): boolean {
    return this.lastSeenAt.has(sessionId);
  }

  size(): number {
    return this.lastSeenAt.size;
  }

  private reap(now: number): void {
    for (const [sessionId, at] of this.lastSeenAt) {
      if (now - at > this.ttlMs) this.lastSeenAt.delete(sessionId);
    }
  }
}
