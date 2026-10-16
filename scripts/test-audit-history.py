#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Ensure removed historical files are detected without exposing their contents."""
from pathlib import Path
import subprocess
import tempfile

scanner = Path(__file__).with_name('audit-history.py').resolve()
with tempfile.TemporaryDirectory(prefix='ai-games-audit-') as directory:
    root = Path(directory)

    def git(*args):
        return subprocess.run(['git', '-c', 'user.name=Audit test', '-c',
                               'user.email=audit@users.noreply.github.com', *args],
                              cwd=root, check=True, capture_output=True)

    def scan():
        return subprocess.run(['python3', str(scanner)], cwd=root, text=True,
                              capture_output=True)

    git('init', '-q')
    (root / 'README.md').write_text('A harmless repository.\n')
    git('add', '.')
    git('commit', '-qm', 'Initial')
    assert scan().returncode == 0
    secret = 'AKIA' + 'A' * 16
    (root / 'ollama.log').write_text(secret)
    git('add', '.')
    git('commit', '-qm', 'Accidental private file')
    git('rm', 'ollama.log')
    git('commit', '-qm', 'Remove private file')
    result = scan()
    assert result.returncode == 1
    assert 'private/generated file name' in result.stdout
    assert 'AWS access key' in result.stdout
    assert secret not in result.stdout + result.stderr
    # Check working files in a separate clean history.
with tempfile.TemporaryDirectory(prefix='ai-games-audit-') as directory:
    root = Path(directory)
    git('init', '-q')
    (root / '.gitignore').write_text('.env\n')
    git('add', '.')
    git('commit', '-qm', 'Initial')
    (root / '.env').write_text(secret)
    assert scan().returncode == 0  # ignored local file will not be published
    (root / 'nested').mkdir()
    (root / 'nested/.env.example').write_text('Documented example, no credentials.\n')
    assert scan().returncode == 0
    (root / 'nested/archive.log.1').write_text('Local diagnostic data')
    assert scan().returncode == 1
print('History audit: removed files, secret redaction, local ignores, and archive paths OK')
