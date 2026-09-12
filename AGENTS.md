# AGENTS.md

## AgentDeck 작업 진입점

1. 먼저 [CLAUDE.md](CLAUDE.md)를 읽으십시오. 저장소 지도, 공통 작업 규칙, 검증 범위의 정본입니다.
2. 해당 문서 상단의 경로 표에서 작업 영역에 맞는 `.claude/rules/<domain>.md`를 찾아 **첫 편집 전에** 읽으십시오. 도메인 불변식은 그 파일이 정본입니다.
3. ESP32 작업은 루트에서 시작해도 [esp32/CLAUDE.md](esp32/CLAUDE.md)와 `.claude/rules/esp32-flash.md`를 읽으십시오.
4. 빌드·배포·진단 절차는 `.agents/skills/`와 `.agents/workflows/`에서 선택하십시오. 스킬의 정본은 `.agents/skills/<name>/SKILL.md`, `.claude/skills/<name>`은 git에 추적되는 심볼릭 링크입니다.

## Codex 로딩과 읽기

- Codex는 전역 지침과 프로젝트 루트→cwd의 지침을 조합합니다. 각 디렉터리에서는 `AGENTS.override.md`, `AGENTS.md`, 설정된 fallback 순으로 최대 한 파일을 선택합니다. 프로젝트 지침 합계의 기본 한도는 32 KiB이며 `project_doc_max_bytes`로 설정할 수 있습니다.
- `CLAUDE.md`와 `.claude/rules/`의 경로 조건부 로딩은 Codex에서 자동으로 이루어지지 않으므로 위 순서대로 직접 읽으십시오. `.codex/rules`는 셸 실행 정책용입니다.
- 셸 출력 한도는 도구와 호출 설정에 따라 다릅니다. 출력에 잘림 표시가 있으면 해당 부분을 `sed -n` 등으로 나누어 다시 읽으십시오. 바이트 수를 고정 토큰 수로 환산하여 완독 여부를 판단하지 마십시오.
- 다른 에이전트의 로딩·스킬 발견 방식과 공식 출처는 [docs/agent-harness.md](docs/agent-harness.md)를 참조하십시오.

## 지식과 인계

- 공통 작업 규칙·메모리 우선순위·협업 worktree·검증 범위는 [CLAUDE.md](CLAUDE.md)의 **Agent working agreements**와 **Verification scope**를 따르십시오. 이 파일에 규칙 본문을 복제하지 마십시오.
- 과거 작업은 `docs/devlog/entries/`에서 키워드로 검색하십시오. `DEVELOPMENT_LOG.md`와 월별 집계는 생성물이므로 직접 수정하지 마십시오. 새 항목을 쓴 뒤 `pnpm devlog:build`로 재생성합니다.
- `/clear`, `/new`, 작업 전환 또는 다른 세션으로 인계할 때는 `.agents/skills/session-end/SKILL.md`를 사용하십시오.
