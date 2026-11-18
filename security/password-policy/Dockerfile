FROM ubuntu:24.04
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y \
    libpam-pwquality passwd nano cracklib-runtime wamerican python3-minimal \
    && rm -rf /var/lib/apt/lists/*
COPY pwquality.conf /etc/security/pwquality.conf.d/99-cr17-lab.conf
COPY test_policy.py /opt/cr17/test_policy.py
CMD ["sleep", "infinity"]
