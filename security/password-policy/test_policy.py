"""Exercise the real PAM passwd path using disposable lab credentials only."""
import pwd
import subprocess
import sys

USER = "cr17test"
SHORT = "T8v!nQ2#pR6@zK"  # 14 characters
INITIAL = "M7!qV2#rN9@xL4$"  # 15 characters
REPLACEMENT = "B4@tR8!mQ2#zW7%"  # 15 characters

try:
    pwd.getpwnam(USER)
except KeyError:
    subprocess.run(["useradd", "-m", "-s", "/bin/bash", USER], check=True)

failures = 0


def test(label, new_password, accepted, old_password=None):
    global failures
    command = ["passwd", USER]
    inputs = (new_password + "\n") * 8
    if old_password is not None:
        command = ["runuser", "-u", USER, "--", "passwd"]
        inputs = old_password + "\n" + inputs
    result = subprocess.run(command, input=inputs, text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            timeout=20, env={"PATH": "/usr/sbin:/usr/bin:/sbin:/bin", "LC_ALL": "C"})
    passed = (result.returncode == 0) == accepted
    if not accepted:
        passed = passed and "BAD PASSWORD" in result.stdout
    failures += not passed
    print(f"{'PASS' if passed else 'FAIL'} | {label} | exit={result.returncode}")
    print(result.stdout.strip())


test("Root: 14 characters rejected", SHORT, False)
test("Root: long dictionary word rejected", "antidisestablishmentarianism", False)
test("Root: 15-character lab password accepted", INITIAL, True)
test("Ordinary user: 14 characters rejected", SHORT, False, INITIAL)
test("Ordinary user: 15-character replacement accepted", REPLACEMENT, True, INITIAL)
print(f"RESULT: {5 - failures}/5 tests passed")
sys.exit(1 if failures else 0)
