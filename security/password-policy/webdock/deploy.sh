#!/bin/bash
set -Eeuo pipefail
export LC_ALL=C
script_dir=$(cd -- "$(dirname -- "$0")" && pwd)
if [ "$EUID" -ne 0 ]; then
    echo 'Run using sudo bash deploy.sh'
    exit 1
fi
exec > >(tee "$script_dir/result.txt") 2>&1
echo "CR 1.7 deployment started: $(date -Is)"
grep -E '^(PRETTY_NAME|VERSION_ID)=' /etc/os-release
dpkg-query -W libpam-pwquality libpwquality1
grep -nE '^password[[:space:]].*pam_pwquality\.so' /etc/pam.d/common-password
if grep -Eq '^[[:space:]]*(minlen|[duol]credit|dictcheck|enforcing|enforce_for_root)([[:space:]]|=|$)' /etc/security/pwquality.conf; then
    echo 'Existing main-file policy overrides require review; nothing changed.'
    exit 1
fi
backup=$(mktemp -d /var/backups/cr17-policy-XXXXXXXX)
target=/etc/security/pwquality.conf.d/99-gruppe7-cr17.conf
cp -a /etc/security/pwquality.conf "$backup/pwquality.conf"
cp -a /etc/pam.d/common-password "$backup/common-password"
if [ -d /etc/security/pwquality.conf.d ]; then
    cp -a /etc/security/pwquality.conf.d "$backup/pwquality.conf.d"
fi
had_target=no
if [ -e "$target" ]; then
    cp -a "$target" "$backup/previous-target.conf"
    had_target=yes
fi
echo "Backup: $backup"
applied=no
rollback() {
    rc=$?
    if [ "$applied" = yes ] && [ "$rc" -ne 0 ]; then
        if [ "$had_target" = yes ]; then
            cp -a "$backup/previous-target.conf" "$target"
        else
            rm -f -- "$target"
        fi
        echo 'DEPLOYMENT FAILED: policy file rolled back.'
    fi
    trap - EXIT
    exit "$rc"
}
trap rollback EXIT
install -d -o root -g root -m 0755 /etc/security/pwquality.conf.d
applied=yes
install -o root -g root -m 0644 "$script_dir/pwquality.conf" "$target"
echo 'Applied policy:'
cat "$target"
python3 "$script_dir/verify_policy.py"
cmp /etc/pam.d/common-password "$backup/common-password"
echo 'PAM stack unchanged; existing account passwords unchanged.'
echo 'DEPLOYMENT SUCCESS: 5/5 tests passed.'
echo "Rollback: remove $target (or restore $backup/previous-target.conf if it existed)."
