#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Exercise the opt-in comparison executables with a deterministic fake Ollama."""
import json
import os
from pathlib import Path
import re
import subprocess
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

ROOT = Path(__file__).resolve().parents[1]
ALL_GAMES = (ROOT / "Makefile").read_text().split("GAMES := ")[1].splitlines()[0].split()
# Some newer apps have Ollama/harness controls but no standalone public fixtures.
# Keep every declared fixture suite mandatory, including newly added games.
GAMES = [game for game in ALL_GAMES if (ROOT / game / "tests/harness-fixture.h").is_file()]
REQUESTS = []
INVALID = False


class Handler(BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def do_POST(self):
        request = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
        REQUESTS.append((self.path, request))
        if self.path == "/api/show":
            response = {"thinking": {"values": [False, True], "default": False}}
        else:
            assert self.path == "/api/generate"
            assert request["stream"] is False
            assert request["options"] == {"seed": 42, "temperature": 0.0}
            if "format" not in request:
                answer = "<think>PRIVATE</think>My short proposal: check the legal choices."
            elif INVALID:
                answer = "{}"
            else:
                properties = request["format"]["properties"]
                prompt = request["prompt"]
                if "destination" in properties:
                    move = re.search(r"^(\w+) -> \w+ -> (\w+)", prompt, re.MULTILINE)
                    assert move, prompt
                    source = "source" if "source" in properties else "entry"
                    answer = json.dumps({source: move[1], "destination": move[2]})
                elif "direction" in properties:
                    move = re.search(r"^(\d+),(\d+) (up|down|left|right)$", prompt, re.MULTILINE)
                    assert move, prompt
                    answer = json.dumps({"x": int(move[1]), "y": int(move[2]), "direction": move[3]})
                else:
                    answer = json.dumps({key: value["enum"][0] for key, value in properties.items()})
                assert "PRIVATE" not in prompt
            response = {"done": True, "response": answer, "prompt_eval_count": 7, "eval_count": 5}
        payload = json.dumps(response).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)


def execute(binary, *args, expected=0):
    env = dict(os.environ, OLLAMA_LOG_FILE="/dev/null", OLLAMA_ERROR_LOG_FILE="/dev/null")
    completed = subprocess.run([str(binary), *args], cwd=ROOT, env=env, text=True, capture_output=True, timeout=30)
    assert completed.returncode == expected, (binary, completed.stdout, completed.stderr)
    return [json.loads(line) for line in completed.stdout.splitlines()]


def main():
    global INVALID
    without_fixtures = [game for game in ALL_GAMES if game not in GAMES]
    if without_fixtures:
        print("No standalone harness fixtures: " + ", ".join(without_fixtures))
    server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    rows = 0
    try:
        for game in GAMES:
            binary = ROOT / game / "build" / "ollama-harness-eval"
            assert binary.exists(), f"Build first: make GAME={game}"
            before = len(REQUESTS)
            listed = execute(binary, "--list")
            assert [row["fixture"] for row in listed] == [0, 1, 2]
            assert len(REQUESTS) == before
            execute(binary, expected=2)
            result = execute(binary, "--model=mock", f"--url=http://127.0.0.1:{server.server_port}", "--thinking", "--revision=test")
            assert len(result) == 6, result
            for row in result:
                assert row["game"] == game and row["revision"] == "test"
                assert row["ok"] and row["legal"] and row["choice"], row
                assert row["generations"] == row["completed_generations"]
                assert row["token_counts_available"]
                assert row["prompt_tokens"] == row["generations"] * 7
                assert row["output_tokens"] == row["generations"] * 5
                if row["mode"] == "propose-review":
                    assert row["generations"] >= 2 and row["generations"] % 2 == 0
                rows += 1
        INVALID = True
        binary = ROOT / "gomoku/build/ollama-harness-eval"
        failed = execute(binary, "--model=mock", f"--url=http://127.0.0.1:{server.server_port}", "--fixture=0", expected=1)
        assert len(failed) == 2 and all(not row["ok"] and not row["legal"] and row["error"] for row in failed)
    finally:
        server.shutdown()
        server.server_close()
        thread.join()
    print(f"Ollama harness evaluation: {rows} matched mock comparisons across {len(GAMES)} games; settings, metrics, fixture listing, and failures OK")


if __name__ == "__main__":
    main()
