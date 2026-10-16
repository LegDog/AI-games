#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Review reachable Git history and publishable working files without printing secrets."""
import os
import pathlib
import re
import subprocess
import sys


def git(*args):
    return subprocess.check_output(['git', *args])


BAD_PATH = re.compile(
    r'(^|/)(?:\.aws|\.ssh|build(?:-[^/]*)?|node_modules|__pycache__)(?:/|$)'
    r'|(^|/)(?:\.env(?:\.[^/]*)?|id_rsa|id_ed25519|credentials)(?:$)'
    r'|(?:\.log(?:\.[^/]*)?|\.(?:o|a|so|rpm|deb|pyc)|histórico[^/]*\.md|[^/]*-history\.md)$', re.I)
PATTERNS = {
    'private key': re.compile(rb'-----BEGIN (?:[A-Z0-9 ]+ )?PRIVATE KEY-----'),
    'AWS access key': re.compile(rb'\b(?:AKIA|ASIA)[A-Z0-9]{16}\b'),
    'GitHub token': re.compile(rb'\b(?:gh[pousr]_[A-Za-z0-9]{30,}|github_pat_[A-Za-z0-9_]{40,})\b'),
    'OpenAI-style key': re.compile(rb'\bsk-(?:proj-)?[A-Za-z0-9_-]{32,}\b'),
    'credential in URL': re.compile(rb'https?://[^\s/:@]+:[^\s/@]+@[^\s/]+'),
    'assigned secret': re.compile(rb'''(?im)^\s*(?:export\s+)?(?:AWS_SECRET_ACCESS_KEY|API_KEY|ACCESS_TOKEN|PASSWORD)\s*=\s*["']?[A-Za-z0-9/+_-]{16,}'''),
}


def inspect(path, data, origin):
    findings = []
    if BAD_PATH.search(path) and not path.endswith('.env.example'):
        findings.append('private/generated file name')
    if data.startswith((b'\x7fELF', b'PK\x03\x04', b'\x1f\x8b', b'!<arch>')):
        findings.append('compiled binary or archive')
    for label, pattern in PATTERNS.items():
        if pattern.search(data):
            findings.append(label)
    for label in findings:
        print(f'REVIEW {origin}: {path}: {label}')
    return len(findings)


def main():
    root = pathlib.Path(git('rev-parse', '--show-toplevel').decode().strip())
    commits = git('rev-list', '--all', '--reflog').splitlines()
    objects = {}
    for commit in commits:
        for entry in git('ls-tree', '-rz', '--full-tree', commit.decode()).split(b'\0'):
            if not entry:
                continue
            metadata, path = entry.split(b'\t', 1)
            _, kind, oid = metadata.split()
            if kind == b'blob':
                objects.setdefault(oid, set()).add(path.decode(errors='replace'))
    count = 0
    for oid, paths in objects.items():
        data = git('cat-file', 'blob', oid.decode())
        for path in sorted(paths):
            count += inspect(path, data, f'blob {oid.decode()[:12]}')
    files = git('ls-files', '-z', '--cached', '--others', '--exclude-standard').split(b'\0')
    for name in files:
        if name:
            path = name.decode()
            source = root / path
            if source.is_symlink():
                count += inspect(path, os.fsencode(os.readlink(source)), 'working tree symlink')
            elif source.is_file():
                count += inspect(path, source.read_bytes(), 'working tree')
    emails = set(git('log', '--all', '--format=%ae%n%ce').decode().splitlines())
    reflog_emails = set(git('log', '--all', '--reflog', '--format=%ae%n%ce').decode().splitlines()) - emails
    public_emails = sum(not email.endswith('@users.noreply.github.com') for email in emails)
    print(f'Checked {len(commits)} commits, {len(objects)} unique historical blobs, and publishable working files.')
    print(f'Branch/ref identities: {len(emails)} unique email(s); {public_emails} outside GitHub noreply.')
    print(f'Additional reflog-only email(s): {len(reflog_emails)} (not published by a normal branch push).')
    print(f'Content/path findings: {count}. Pattern scanning cannot prove absence of all secrets.')
    return 1 if count else 0


if __name__ == '__main__':
    sys.exit(main())
