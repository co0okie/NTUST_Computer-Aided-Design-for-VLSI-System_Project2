# Tester collaboration

Tester creates `opened/<issue>/README.md` and adds any reproduction data.
Replies go in `comments/YYYY-MM-DD HH:MM:SS coder.md` or `tester.md` using local
time. Coder commits fixes and records verification evidence in replies.
Only tester decides acceptance and moves issues from `opened/` to `closed/`.

Issue directory names begin with a shared three-digit sequence and a space, e.g. `001 issue_name`. Numbering starts at 001 across both opened/ and closed/; new issues use the next unused number, and closing preserves the original number. Existing comment history is preserved.
