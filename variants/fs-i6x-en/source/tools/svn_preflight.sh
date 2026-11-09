#!/usr/bin/env bash
set -euo pipefail

REPO_ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$REPO_ROOT"

# Formatting
BOLD='\033[1m'
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[0;33m'
CYAN='\033[0;36m'
NC='\033[0m'

pass() {
    echo -e "  [${GREEN}PASS${NC}] $1"
}

warn() {
    echo -e "  [${YELLOW}WARN${NC}] $1"
}

fail() {
    echo -e "  [${RED}FAIL${NC}] $1" >&2
}

info() {
    echo -e "${BOLD}$1${NC}"
}

TOTAL_ISSUES=0
FATAL_ERRORS=0

echo -e "${BOLD}====================================================${NC}"
echo -e "${BOLD}         SVN Preflight & Hygiene Check              ${NC}"
echo -e "${BOLD}====================================================${NC}"
echo ""

# ---------------------------------------------------------------------------
# Check 1: svn status & conflict detection
# ---------------------------------------------------------------------------
info "[1/5] Checking SVN Working Copy Status..."
if ! command -v svn >/dev/null 2>&1; then
    fail "svn command not available!"
    exit 1
fi

SVN_STATUS=$(svn status || true)
# Match actual conflicts in column 1 (content), column 2 (properties), or column 7 (tree)
CONFLICT_FILES=$(echo "$SVN_STATUS" | grep -E '^C|^.C|^.{6}C' || true)
if [[ -n "$CONFLICT_FILES" ]]; then
    fail "Merge/Tree conflicts detected in SVN working copy:"
    echo "$CONFLICT_FILES" >&2
    FATAL_ERRORS=$((FATAL_ERRORS + 1))
else
    pass "No merge conflicts detected in SVN working copy."
fi

MODIFIED_COUNT=$(echo "$SVN_STATUS" | grep -c '^M' || true)
UNVERSIONED_COUNT=$(echo "$SVN_STATUS" | grep -c '^\?' || true)
echo -e "      Tracked modified files: ${CYAN}${MODIFIED_COUNT}${NC}, Unversioned items: ${CYAN}${UNVERSIONED_COUNT}${NC}"
echo ""

# ---------------------------------------------------------------------------
# Check 2: Temporary & Build Artifacts
# ---------------------------------------------------------------------------
info "[2/5] Checking for Temporary and Build Artifacts..."
ARTIFACT_PATTERNS='(\.o$|\.d$|\.axf$|\.elf$|\.htm$|\.lnp$|\.sct$|\.map\.old$|\.map\.view$|\.bak$|\.crf$|\.stackdump$|build\.log$|\.lock$|/build/mssd_|/build/ticket06-)'

DIRTY_ARTIFACTS=$(echo "$SVN_STATUS" | grep -E "$ARTIFACT_PATTERNS" || true)
if [[ -n "$DIRTY_ARTIFACTS" ]]; then
    ARTIFACT_COUNT=$(echo "$DIRTY_ARTIFACTS" | wc -l)
    warn "Found ${ARTIFACT_COUNT} build/temporary artifacts in working tree (keilkill.bat or clean recommended before commit):"
    echo "$DIRTY_ARTIFACTS" | head -n 10 | sed 's/^/        /'
    if [[ $ARTIFACT_COUNT -gt 10 ]]; then
        echo "        ... and $((ARTIFACT_COUNT - 10)) more items."
    fi
    TOTAL_ISSUES=$((TOTAL_ISSUES + 1))
else
    pass "No stray build or temporary artifacts detected."
fi
echo ""

# ---------------------------------------------------------------------------
# Check 3: CRLF/LF Large-scale Line Ending Shifts
# ---------------------------------------------------------------------------
info "[3/5] Checking Line Ending (CRLF/LF) Shifts in Modified Text Files..."
CRLF_SHIFT_DETECTED=0

MODIFIED_FILES=$(echo "$SVN_STATUS" | grep '^M' | awk '{print $NF}' || true)
for file in $MODIFIED_FILES; do
    # Only inspect source and text documentation files
    if [[ "$file" =~ \.(hex|map.*|log|s19|db.*|params|json)$ ]]; then
        continue
    fi
    if [[ -f "$file" ]] && file "$file" | grep -qv "text"; then
        continue # Skip binary files
    fi

    if [[ -f "$file" ]]; then
        # Check diff statistics: if added + deleted lines roughly equal 2x file length, full conversion happened
        DIFF_STAT=$(svn diff "$file" 2>/dev/null | grep -E '^(\+|-)[^+-]' | wc -l || true)
        FILE_LINES=$(wc -l < "$file" 2>/dev/null || echo 0)
        if [[ $FILE_LINES -gt 20 && $DIFF_STAT -gt $((FILE_LINES * 15 / 10)) ]]; then
            warn "Possible whole-file line ending / whitespace churn in: $file (diff lines: $DIFF_STAT, total lines: $FILE_LINES)"
            CRLF_SHIFT_DETECTED=$((CRLF_SHIFT_DETECTED + 1))
            TOTAL_ISSUES=$((TOTAL_ISSUES + 1))
        fi
    fi
done

if [[ $CRLF_SHIFT_DETECTED -eq 0 ]]; then
    pass "No whole-file CRLF/LF line-ending churn detected in modified files."
fi
echo ""

# ---------------------------------------------------------------------------
# Check 4: High-Risk Core Architecture Files Modification
# ---------------------------------------------------------------------------
info "[4/5] Inspecting Modifications to High-Risk Architecture Files..."
HIGH_RISK_PATTERNS='(Core/Src/main\.c$|Core/Src/freertos\.c$|Core/Src/stm32f4xx_it\.c$|\.ioc$|\.uvprojx$|\.ld$)'
HIGH_RISK_CHANGES=$(echo "$SVN_STATUS" | grep -E '^[MAD]' | grep -E "$HIGH_RISK_PATTERNS" || true)

if [[ -n "$HIGH_RISK_CHANGES" ]]; then
    warn "Modifications detected in high-risk files (BSP core entry, ISR, pinout, or linker):"
    echo "$HIGH_RISK_CHANGES" | sed 's/^/        /'
    echo -e "        ${YELLOW}-> Please ensure changes in these files are intentional and peer-reviewed.${NC}"
    TOTAL_ISSUES=$((TOTAL_ISSUES + 1))
else
    pass "No high-risk architecture/BSP core files modified."
fi
echo ""

# ---------------------------------------------------------------------------
# Check 5: Production .c Manifest Completeness (CMake & Keil)
# ---------------------------------------------------------------------------
info "[5/5] Verifying Production .c Integration in CMake and Keil Manifests..."
CMAKE_LIST="control_chassis/CMakeLists.txt"
KEIL_UVPROJX="control_chassis/MDK-ARM/concrol.uvprojx"

if [[ ! -f "$CMAKE_LIST" || ! -f "$KEIL_UVPROJX" ]]; then
    fail "Project manifest files missing: $CMAKE_LIST or $KEIL_UVPROJX"
    FATAL_ERRORS=$((FATAL_ERRORS + 1))
else
    MISSING_MANIFEST_COUNT=0
    # Collect production .c files in UserApp and UserDev, plus Src (exclude known unused templates: syscalls, sysmem, copy)
    PROD_C_FILES=$(find control_chassis/Core/UserApp control_chassis/Core/UserDev control_chassis/Core/Src -type f -name "*.c" \
        ! -name "*copy*" ! -name "syscalls.c" ! -name "sysmem.c" | sort)

    for c_path in $PROD_C_FILES; do
        c_basename=$(basename "$c_path")
        
        # Check in CMake source list
        if ! grep -q "$c_basename" "$CMAKE_LIST"; then
            fail "Production file $c_path is MISSING from CMake manifest ($CMAKE_LIST)"
            MISSING_MANIFEST_COUNT=$((MISSING_MANIFEST_COUNT + 1))
            FATAL_ERRORS=$((FATAL_ERRORS + 1))
        fi

        # Check in Keil uvprojx
        if ! grep -q "$c_basename" "$KEIL_UVPROJX"; then
            fail "Production file $c_path is MISSING from Keil manifest ($KEIL_UVPROJX)"
            MISSING_MANIFEST_COUNT=$((MISSING_MANIFEST_COUNT + 1))
            FATAL_ERRORS=$((FATAL_ERRORS + 1))
        fi
    done

    if [[ $MISSING_MANIFEST_COUNT -eq 0 ]]; then
        pass "All production .c files are properly registered in both CMake and Keil manifests."
    fi
fi
echo ""

# ---------------------------------------------------------------------------
# Summary
# ---------------------------------------------------------------------------
echo -e "${BOLD}====================================================${NC}"
if [[ $FATAL_ERRORS -gt 0 ]]; then
    echo -e "${RED}${BOLD}PREFLIGHT CHECK FAILED: ${FATAL_ERRORS} fatal error(s), ${TOTAL_ISSUES} warning(s).${NC}"
    exit 1
elif [[ $TOTAL_ISSUES -gt 0 ]]; then
    echo -e "${YELLOW}${BOLD}PREFLIGHT CHECK PASSED WITH WARNINGS: ${TOTAL_ISSUES} warning item(s).${NC}"
    exit 0
else
    echo -e "${GREEN}${BOLD}PREFLIGHT CHECK PASSED PERFECTLY: Clean working tree.${NC}"
    exit 0
fi
