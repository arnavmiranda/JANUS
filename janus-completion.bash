#!/usr/bin/env bash
# =============================================================================
#  JANUS Bash Completion Script
# =============================================================================
#
#  Setup (choose one):
#
#  1. Source for the current shell session only:
#       source ./janus-completion.bash
#
#  2. Persist for your user account (add to ~/.bashrc or ~/.bash_profile):
#       echo "source /path/to/janus/janus-completion.bash" >> ~/.bashrc
#       source ~/.bashrc
#
#  3. System-wide install (requires root):
#       sudo cp janus-completion.bash /etc/bash_completion.d/janus
#       # Reopen your terminal or run: source /etc/bash_completion.d/janus
#
#  Requirements:
#    - bash >= 4.0
#    - sqlite3 CLI tool  (sudo apt install sqlite3)
#    - janus_meta.db must exist in the directory where you run the janus binary
#
# =============================================================================

_janus_completions() {
    local cur prev db_file hashes

    # cur  = the word currently being typed
    # prev = the word immediately before cur  (i.e. the last complete token)
    cur="${COMP_WORDS[COMP_CWORD]}"
    prev="${COMP_WORDS[COMP_CWORD-1]}"

    # ── Top-level commands ───────────────────────────────────────────────────
    local commands="mount commit log checkout diff stats"

    # ── Locate janus_meta.db ─────────────────────────────────────────────────
    # Prefer the directory the user is currently in (same convention as the
    # janus binary itself: it always opens ./janus_meta.db).
    db_file="$(pwd)/janus_meta.db"

    # ── Level 1: completing the sub-command itself ───────────────────────────
    if [[ ${COMP_CWORD} -eq 1 ]]; then
        COMPREPLY=( $(compgen -W "${commands}" -- "${cur}") )
        return 0
    fi

    # ── Level 2: context-aware completions based on the previous token ───────
    case "${prev}" in

        # -- commit: suggest the -m flag --------------------------------------
        commit)
            COMPREPLY=( $(compgen -W "-m" -- "${cur}") )
            return 0
            ;;

        # -- stats: suggest the --json flag -----------------------------------
        stats)
            COMPREPLY=( $(compgen -W "--json" -- "${cur}") )
            return 0
            ;;

        # -- checkout / diff: query SQLite for all stored commit hashes -------
        checkout|diff)
            if [[ -f "${db_file}" ]]; then
                # Run sqlite3 silently; suppress all errors to avoid polluting
                # the terminal (e.g. if the DB is locked or the table is empty).
                hashes=$(sqlite3 "${db_file}" \
                    "SELECT commit_hash FROM snapshots ORDER BY id DESC;" \
                    2>/dev/null)
                COMPREPLY=( $(compgen -W "${hashes}" -- "${cur}") )
            fi
            return 0
            ;;

        # -- diff second hash: the word two positions back was 'diff' ---------
        # When completing the 2nd hash for `diff <hash1> <TAB>` the prev token
        # is the first hash, not 'diff', so we check position 1 explicitly.
        *)
            if [[ ${COMP_CWORD} -eq 3 && "${COMP_WORDS[1]}" == "diff" ]]; then
                if [[ -f "${db_file}" ]]; then
                    hashes=$(sqlite3 "${db_file}" \
                        "SELECT commit_hash FROM snapshots ORDER BY id DESC;" \
                        2>/dev/null)
                    COMPREPLY=( $(compgen -W "${hashes}" -- "${cur}") )
                fi
                return 0
            fi
            ;;
    esac

    # ── Fallback: no suggestion ───────────────────────────────────────────────
    COMPREPLY=()
    return 0
}

# Register the completion function for the 'janus' command.
# -F  specifies the function to call.
# The binary name used here must match exactly how it appears on your $PATH.
# If you run it as './build/janus', also register that variant:
complete -F _janus_completions janus
complete -F _janus_completions ./build/janus
complete -F _janus_completions ./janus
