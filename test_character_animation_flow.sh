#!/usr/bin/env bash
set -euo pipefail

# Character animation REST regression test.
# Validates:
# 1) movement release passes through ComeToRest before Idle
# 2) jump transitions Start -> Fall -> Land and returns to Idle/None
#
# Usage:
#   ./test_character_animation_flow.sh [port]

PORT="${1:-40132}"
BASE="http://127.0.0.1:${PORT}"

need_bin() {
    if ! command -v "$1" >/dev/null 2>&1; then
        echo "FAIL: required command not found: $1" >&2
        exit 1
    fi
}

need_bin curl
need_bin jq

api_get() {
    curl -sS "${BASE}$1"
}

api_post() {
    local endpoint="$1"
    local payload="$2"
    curl -sS -X POST "${BASE}${endpoint}" \
        -H 'Content-Type: application/json' \
        -d "${payload}"
}

scene0() {
    api_get "/profile/scene" | jq '.sceneItems[0]'
}

state() {
    local s
    s="$(scene0)"
    jq -r '.currentAnimState' <<<"${s}"
}

jump_phase() {
    local s
    s="$(scene0)"
    jq -r '.jumpPhase // "None"' <<<"${s}"
}

wait_until() {
    local timeout_ms="$1"
    local step_ms="$2"
    local predicate="$3"
    local elapsed=0
    while (( elapsed < timeout_ms )); do
        if eval "${predicate}"; then
            return 0
        fi
        sleep "$(awk "BEGIN { printf \"%.3f\", ${step_ms}/1000 }")"
        elapsed=$((elapsed + step_ms))
    done
    return 1
}

if ! api_get "/health" | jq -e '.ok == true' >/dev/null; then
    echo "FAIL: health check failed on ${BASE}" >&2
    exit 1
fi

# Enable control and settle at grounded non-jump baseline.
api_post "/controls/character" '{"sceneIndex":0,"controllable":true}' >/dev/null
if ! wait_until 6000 50 '[[ "$(scene0 | jq -r ".isGrounded")" == "true" && "$(jump_phase)" == "None" ]]'; then
    echo "FAIL: did not reach grounded baseline" >&2
    exit 1
fi

# Trigger movement.
api_post "/controls/character" '{"sceneIndex":0,"move":"W","durationMs":320}' >/dev/null
sleep 0.2

# After release we should see ComeToRest, then Idle.
saw_rest=0
saw_idle_after_rest=0
for _ in $(seq 1 60); do
    st="$(state)"
    if [[ "${st}" == "ComeToRest" ]]; then
        saw_rest=1
    fi
    if (( saw_rest == 1 )) && [[ "${st}" == "Idle" ]]; then
        saw_idle_after_rest=1
        break
    fi
    sleep 0.05
done
if (( saw_rest == 0 )); then
    echo "FAIL: ComeToRest was not observed after movement release" >&2
    exit 1
fi
if (( saw_idle_after_rest == 0 )); then
    echo "FAIL: Idle was not observed after ComeToRest" >&2
    exit 1
fi

# Trigger jump and verify phase flow.
api_post "/controls/character" '{"sceneIndex":0,"jump":true,"durationMs":120}' >/dev/null
saw_start=0
saw_fall=0
saw_land=0
saw_end_idle=0
for _ in $(seq 1 180); do
    st="$(state)"
    jp="$(jump_phase)"
    if [[ "${st}" == "Jump" && "${jp}" == "Start" ]]; then
        saw_start=1
    fi
    if [[ "${st}" == "Jump" && "${jp}" == "Fall" ]]; then
        saw_fall=1
    fi
    if [[ "${jp}" == "Land" ]]; then
        saw_land=1
    fi
    if (( saw_start == 1 && saw_fall == 1 && saw_land == 1 )) && [[ "${st}" == "Idle" && "${jp}" == "None" ]]; then
        saw_end_idle=1
        break
    fi
    sleep 0.03
done

if (( saw_start == 0 )); then
    echo "FAIL: Jump phase Start not observed" >&2
    exit 1
fi
if (( saw_fall == 0 )); then
    echo "FAIL: Jump phase Fall not observed" >&2
    exit 1
fi
if (( saw_land == 0 )); then
    echo "FAIL: Jump phase Land not observed" >&2
    exit 1
fi
if (( saw_end_idle == 0 )); then
    echo "FAIL: Did not settle back to Idle/None after jump" >&2
    exit 1
fi

echo "PASS: return-to-rest and jump phase flow validated on port ${PORT}"
