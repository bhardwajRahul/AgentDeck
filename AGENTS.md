# AGENTS.md

## 프로젝트 지침 (Codex · OpenCode · Antigravity · Claude Code)

이 프로젝트(AgentDeck)에서 작업할 때 모든 AI 에이전트는 다음 지침을 **반드시** 따르십시오. 이 파일은 진입점이며 규칙의 본문을 담지 않습니다. 규칙은 한 곳에만 있습니다.

### 0. 지침 파일의 3층 구조 (읽는 순서)

| 층 | 파일 | 내용 | 누가 자동 로드하나 |
|---|---|---|---|
| 1 | `AGENTS.md` (이 파일) | 진입점, 에이전트별 차이 | Codex · OpenCode · Antigravity (관례) |
| 2 | `CLAUDE.md` | **지도**: 모노레포 구성, 빌드/테스트, 교차 규약, 도메인 규칙 파일 색인 | Claude Code (매 세션). 다른 에이전트는 `cat CLAUDE.md` 로 읽음 |
| 3 | `.claude/rules/<domain>.md` | **도메인 불변식의 정본** (APME, OpenClaw, 훅/PERM, usage, ESP32 플래시, 데몬 수명주기, Swift 데몬, 와이어/기기, 관리 세션, 디자인 시스템, App Store/릴리스) | Claude Code 는 파일 첫머리 `paths:` glob 에 맞는 파일을 건드릴 때 자동 로드. **다른 에이전트는 해당 영역을 편집하기 전에 직접 읽어야 함** |

- `CLAUDE.md` 상단 표가 `paths → 규칙 파일` 지도입니다. 작업 대상 경로에 맞는 규칙 파일을 **첫 편집 전에** 읽으십시오. 규칙 파일은 git 에 추적됩니다(`.gitignore` 의 `!.claude/rules/`). `.claude/` 의 나머지(`skills/`, `settings*`, `worktrees/`)는 개발자 로컬입니다.
- 규칙 본문은 옮기되 다듬지 않습니다: 굵은 첫 문장이 규칙이고 나머지는 근거입니다. 새 도메인 규칙은 해당 규칙 파일에, 둘 이상의 도메인이 필요한 규칙만 `CLAUDE.md` 의 Key Conventions 에 추가합니다.
- `esp32/CLAUDE.md` 는 Claude Code 가 `esp32/` 아래 파일을 읽을 때 자동으로 붙는 중첩 지침입니다. Codex 는 cwd 가 `esp32/` 일 때 `esp32/AGENTS.md` 를 통해 같은 파일에 도달하고, 저장소 루트에서 작업할 때는 `.claude/rules/esp32-flash.md` 와 함께 직접 읽으십시오.
- `DEVELOPMENT_LOG.md` 는 통독하지 마십시오. 상단 최신 항목만 보고, 작업 키워드/파일명으로 `rg` 검색하십시오. 오래된 달은 `docs/devlog/<YYYY-MM>.md` 로 아카이브됩니다(인덱스 `docs/devlog/README.md`) — 활성 로그에 어느 달까지 남아 있는지는 `grep -n '^## 2026-' DEVELOPMENT_LOG.md | head -1` 로 확인하고, 아카이브가 필요하면 **해당 월 파일만** 검색하십시오.

### 1. 에이전트별 차이 (실측 근거는 `docs/agent-harness.md`)

- **Claude Code** — `CLAUDE.md` 매 세션 주입, `.claude/rules/` 경로 조건부 로드, `esp32/CLAUDE.md` 중첩 로드. skill 은 `.claude/skills/*.md` 포인터(로컬) → `.agents/skills/` 정본.
- **Codex** (현재 `gpt-6-astra`) — 루트→cwd 경로의 `AGENTS.md` 만 자동 주입되며 **합계 32 KiB(`project_doc_max_bytes`)를 넘는 파일은 소리 없이 버려집니다**. 이 파일을 짧게 유지하는 이유입니다. 경로 조건부 지침 파일은 없으므로(`.codex/rules` 는 실행 정책 전용) 규칙 파일은 직접 읽습니다. 셸 출력은 모델에 **약 10,000 토큰(≈40 KB)까지만 head+tail 로 전달**되고 중간이 잘립니다(`…N tokens truncated…`) — `CLAUDE.md` 는 그 안에 들어가도록 유지하고(현재 ≈30 KB), 그보다 큰 파일은 `sed -n` 으로 나누어 읽으십시오. skill 은 `.agents/skills/` 를 자동 발견합니다. Astra 계열은 모순되거나 불명확한 지침에 민감해 멈출 수 있으므로, 이 파일과 `CLAUDE.md` 가 서로 다른 말을 하면 `CLAUDE.md` 를 따르고 이 파일을 고치십시오.
- **OpenCode** — 제품 세션 타입으로는 완전 지원(observer plugin)이지만 저작 도구로서는 hook/skill 자동 발견이 없습니다. `AGENTS.md` → `CLAUDE.md` → 해당 규칙 파일을 읽고, 절차는 `.agents/workflows/<name>.md` 경로를 직접 지정하십시오.
- **Antigravity** — 지침 파일만 읽습니다. 세션 관측·hook·skill 자동 발견이 없고, Apple 앱은 사용량/크레딧 통계만 읽습니다. 워크플로우 파일 경로를 직접 인용하십시오.

### 2. 워크플로우 · 스킬 · 인계

- 빌드, 환경 설정 등 반복 작업은 명령을 유추하지 말고 `.agents/workflows/` 의 워크플로우를 사용하십시오(예: `build-android.md`).
- `.agents/skills/<name>/SKILL.md` 가 절차의 **정본**입니다. `.claude/skills/*.md` 는 얇은 포인터이므로 절차 내용을 거기에 복제·편집하지 마십시오.
- 세션 인계: `/clear`·`/new`·작업 전환·다른 에이전트로 넘기기 전에 `session-end` skill 을 실행하십시오.

### 3. 주요 개발 원칙 요약 (본문은 `CLAUDE.md` / 규칙 파일)

- **Monorepo**: `pnpm workspaces`. `bridge`, `plugin`, `plugin-ulanzi`, `shared`, `hooks`, `setup`, `android`, `apple`, `esp32` 패키지 디렉토리를 확인하고 작업하십시오. 빌드 순서와 생성기(`pnpm generate-*`)는 `CLAUDE.md` § Build.
- **Hook 포맷 (CRITICAL)**: Claude Code v2.1+ hook 은 3단계 중첩 포맷이 필수이며 구 flat 포맷은 조용히 실패합니다. 요약은 `CLAUDE.md` § Key Conventions, 설치기·마이그레이션·Codex/Kiro 훅은 `.claude/rules/observed-sessions.md`.
- **교차 플랫폼 규칙은 SSOT-first**: 둘 이상의 표면(TS/Swift/Kotlin/C++)이 합의해야 하는 수치·계약은 정본에서 생성/미러하고 드리프트 게이트를 둡니다. 손 미러 신설 금지 — `CLAUDE.md` § Key Conventions, 카탈로그는 `docs/architecture.md`.
- **Android / E-ink UX**: Jetpack Compose 수정 시 E-ink 기기(Crema/Onyx/Kobo) 특성(그레이스케일, 부분 새로고침)을 지키십시오. 규칙은 `docs/android-ui.md` 와 `docs/android.md`.
- **명령어 안전성**: 데몬·시스템 환경에 영향을 주는 코드를 시험할 때는 `agentdeck daemon …` 수명주기 명령을 쓰고 `.claude/rules/daemon-lifecycle.md` 를 먼저 읽으십시오. 이 머신의 9120 포트는 다른 세션·다른 데몬과 공유됩니다.

### 4. App Store 심사 invariants (필수)

macOS 앱은 App Store 로 배포되며 Apple Review Guidelines 2.5.2 / 4.2 / 4.2.3 을 모든 변경에서 유지해야 합니다. **정본은 `.claude/rules/apple-release.md` 와 `apple/APP_REVIEW_NOTES.md` 이며, 규칙을 여기서 확장하지 말고 그쪽에 추가하십시오.** 하드 룰 요약:

- subprocess 경로(`Process()`/`/bin/sh`/`osascript`/`.command` 생성/외부 CLI 호출) 를 `#if !AGENTDECK_APP_STORE` 뒤에도 **재도입 금지**.
- App Store UI 문구는 companion executable 설치/기동을 **유도 금지**(외부 도구 존재 여부와 무관하게 동일 문구).
- 기능 추가/이동 시 `docs/appstore-feature-matrix.md` 표에 **행을 먼저 추가**한 뒤 구현.
- 제출 전 `bash apple/scripts/verify-appstore-archive.sh <.app>` 를 **Release 빌드로 통과**시킨 뒤 커밋(Debug 실패는 정상).

### 5. 커밋 전 (모든 에이전트)

```bash
pnpm build && pnpm typecheck && pnpm test
pnpm generate-protocol            # no-op 이어야 함 (CI 가 드리프트에 실패)
bash design/lint.sh               # 디자인 규칙 baseline
python3 design/verify-tokens-sync.py   # 토큰 미러 드리프트
pnpm docs:check                   # Markdown 링크·H1 (docs 를 건드렸을 때)
```
