#!/usr/bin/env bash
set -Eeuo pipefail
[[ $EUID -eq 0 ]] || { echo 'Run this script with sudo.'; exit 1; }
SOURCE_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
DEPLOY_DIR=/opt/group7-mqtt
HOST=gruppe7.vps.webdock.cloud
PREFLIGHT=group7-mqtt-preflight
IMAGE=eclipse-mosquitto:2.0.23
SWITCH_STARTED=0
SUCCESS=0
SUB_PID=''
mkdir -p "$DEPLOY_DIR"
exec > >(tee -a "$DEPLOY_DIR/deploy.log") 2>&1
echo "Starting MQTT-only Docker deployment at $(date -u +%FT%TZ)"

cleanup() {
  local result=$?
  trap - EXIT
  if [[ -n $SUB_PID ]]; then kill "$SUB_PID" 2>/dev/null || true; fi
  if command -v docker >/dev/null; then docker rm -f "$PREFLIGHT" >/dev/null 2>&1 || true; fi
  if [[ $SWITCH_STARTED == 1 && $SUCCESS == 0 ]]; then
    echo 'Migration failed: stopping the container and restoring the old broker.'
    docker compose -f "$DEPLOY_DIR/compose.yaml" down || true
    systemctl enable --now mosquitto
  fi
  exit "$result"
}
trap cleanup EXIT

[[ $(id -u mosquitto) == 123 && $(id -g mosquitto) == 122 ]] || {
  echo 'Mosquitto UID/GID changed; review compose.yaml before continuing.'; exit 1;
}
systemctl is-active --quiet mosquitto || { echo 'Original broker must be running before migration.'; exit 1; }
if [[ -f "$DEPLOY_DIR/DEPLOYED" ]]; then echo 'Already deployed; use the Compose commands in README.md.'; exit 1; fi
for cert in ca.crt server.crt server.key; do test -s "/etc/mosquitto/certs/$cert"; done

if ! command -v docker >/dev/null; then
  # Do not remove a pre-existing container runtime belonging to another project.
  for package in docker.io podman-docker containerd runc; do
    if dpkg-query -W -f='${Status}' "$package" 2>/dev/null | grep -q 'install ok installed'; then
      echo "Existing package $package needs review; leaving the broker running."; exit 1
    fi
  done
  export DEBIAN_FRONTEND=noninteractive
  apt-get update
  apt-get install -y ca-certificates curl
  install -m 0755 -d /etc/apt/keyrings
  curl -fsSL https://download.docker.com/linux/ubuntu/gpg -o /etc/apt/keyrings/docker.asc
  chmod a+r /etc/apt/keyrings/docker.asc
  . /etc/os-release
  cat > /etc/apt/sources.list.d/docker.sources <<EOF
Types: deb
URIs: https://download.docker.com/linux/ubuntu
Suites: ${UBUNTU_CODENAME:-$VERSION_CODENAME}
Components: stable
Architectures: $(dpkg --print-architecture)
Signed-By: /etc/apt/keyrings/docker.asc
EOF
  apt-get update
  apt-get install -y docker-ce docker-ce-cli containerd.io docker-buildx-plugin docker-compose-plugin
fi
systemctl enable --now docker
docker compose version
docker pull "$IMAGE"

install -m 0644 "$SOURCE_DIR/compose.yaml" "$DEPLOY_DIR/compose.yaml"
install -m 0644 "$SOURCE_DIR/mosquitto.conf" "$DEPLOY_DIR/mosquitto.conf"
install -m 0644 "$SOURCE_DIR/README.md" "$DEPLOY_DIR/README.md"
install -m 0750 "$SOURCE_DIR/rollback.sh" "$DEPLOY_DIR/rollback.sh"
# Record the pulled digest for repeatability; Compose uses that immutable image.
DIGEST=$(docker image inspect "$IMAGE" --format '{{index .RepoDigests 0}}')
sed -i "s|image: eclipse-mosquitto:2.0.23|image: $DIGEST|" "$DEPLOY_DIR/compose.yaml"
cd "$DEPLOY_DIR"
docker compose config --quiet
install -d -o 123 -g 122 -m 0750 data preflight-data
sed 's/listener 8883 0.0.0.0/listener 18884 127.0.0.1/' mosquitto.conf > preflight.conf
docker run -d --name "$PREFLIGHT" --network host --user 123:122 \
  --read-only --cap-drop ALL --security-opt no-new-privileges:true \
  --mount "type=bind,src=$DEPLOY_DIR/preflight.conf,dst=/mosquitto/config/mosquitto.conf,readonly" \
  --mount type=bind,src=/etc/mosquitto/certs,dst=/mosquitto/certs,readonly \
  --mount "type=bind,src=$DEPLOY_DIR/preflight-data,dst=/mosquitto/data" \
  "$DIGEST"

tls_check() {
  timeout 12 openssl s_client -connect "$1" -servername "$HOST" \
    -verify_hostname "$HOST" -verify_return_error -CAfile /etc/mosquitto/certs/ca.crt \
    </dev/null > "$DEPLOY_DIR/tls-check.log" 2>&1 &&
    grep -q 'Verify return code: 0 (ok)' "$DEPLOY_DIR/tls-check.log"
}
READY=0
for attempt in {1..10}; do
  if tls_check 127.0.0.1:18884; then READY=1; break; fi
  sleep 1
done
[[ $READY == 1 ]] || { docker logs "$PREFLIGHT"; echo 'Preflight TLS failed; old broker is still running.'; exit 1; }
docker rm -f "$PREFLIGHT"

BACKUP="$DEPLOY_DIR/backups/$(date -u +%Y%m%dT%H%M%SZ)"
install -d -m 0700 "$BACKUP"
cp -a /etc/mosquitto "$BACKUP/config"
systemctl cat mosquitto > "$BACKUP/mosquitto.service.txt"
SWITCH_STARTED=1
systemctl stop mosquitto
cp -a /var/lib/mosquitto "$BACKUP/data"
cp -a /var/lib/mosquitto/. "$DEPLOY_DIR/data/"
chown -R 123:122 "$DEPLOY_DIR/data"
docker compose up -d

READY=0
for attempt in {1..15}; do
  if tls_check 127.0.0.1:8883; then READY=1; break; fi
  sleep 1
done
[[ $READY == 1 ]] || { docker compose logs --tail 40; exit 1; }

TOPIC="group7/deployment-check/$(date +%s)"
mosquitto_sub -h "$HOST" -p 8883 --cafile /etc/mosquitto/certs/ca.crt \
  -t "$TOPIC" -C 1 -W 15 > "$DEPLOY_DIR/pubsub-check.log" &
SUB_PID=$!
sleep 2
mosquitto_pub -h "$HOST" -p 8883 --cafile /etc/mosquitto/certs/ca.crt \
  -t "$TOPIC" -m 'docker-mqtt-ok' -q 1
wait "$SUB_PID"
SUB_PID=''
grep -qx docker-mqtt-ok "$DEPLOY_DIR/pubsub-check.log"

systemctl disable mosquitto
printf '%s\n' "$(date -u +%FT%TZ) $DIGEST" > "$DEPLOY_DIR/DEPLOYED"
SUCCESS=1
docker compose ps
echo 'SUCCESS: Docker Mosquitto is serving the existing TLS endpoint on port 8883.'
echo 'Original service is stopped and disabled. Rollback: sudo bash /opt/group7-mqtt/rollback.sh'
