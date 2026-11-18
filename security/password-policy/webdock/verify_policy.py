"""Test real PAM password changes with a temporary local account; always remove it."""
import os
import pwd
import secrets
import string
import subprocess
import sys

if os.geteuid() != 0:
    raise SystemExit("Run this verifier as root")
name = "cr17probe" + secrets.token_hex(3)
try:
    pwd.getpwnam(name)
except KeyError:
    pass
else:
    raise SystemExit("Temporary account already exists; aborting")


def secret(length):
    return "Q7!b" + "".join(secrets.choice(string.ascii_letters + string.digits + "!@#$%")
                           for _ in range(length - 4))


def check(label, password, accept, old=None):
    cmd = ["passwd", name] if old is None else ["runuser", "-u", name, "--", "passwd"]
    inputs = ("" if old is None else old + "\n") + (password + "\n") * 8
    p = subprocess.run(cmd, input=inputs, text=True, stdout=subprocess.PIPE,
                       stderr=subprocess.STDOUT, timeout=25,
                       env={"PATH": "/usr/sbin:/usr/bin:/sbin:/bin", "LC_ALL": "C"})
    output = p.stdout.replace(password, "[REDACTED]")
    if old:
        output = output.replace(old, "[REDACTED]")
    passed = (p.returncode == 0) == accept
    if not accept:
        passed = passed and "BAD PASSWORD" in output
    print(f"{'PASS' if passed else 'FAIL'} | {label} | exit={p.returncode}", flush=True)
    print(output.strip(), flush=True)
    return passed


subprocess.run(["useradd", "--no-create-home", "--shell", "/bin/bash", name], check=True)
try:
    initial, replacement = secret(15), secret(15)
    results = [
        check("Root rejects 14 characters", secret(14), False),
        check("Root rejects long dictionary word", "antidisestablishmentarianism", False),
        check("Root accepts 15-character random password", initial, True),
        check("Ordinary user rejects 14 characters", secret(14), False, initial),
        check("Ordinary user accepts 15-character replacement", replacement, True, initial),
    ]
    print(f"RESULT: {sum(results)}/5 tests passed", flush=True)
    if not all(results):
        sys.exit(1)
finally:
    subprocess.run(["userdel", name], check=True)
    print("Temporary test account removed", flush=True)
