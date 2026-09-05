"""Capability-limited companion (desktop puppet).
Watch offloads jobs; allow / confirm / deny. Privileged caps never auto-run.
"""
from __future__ import annotations
from dataclasses import dataclass
from enum import Enum
from urllib.request import urlopen

class Decision(str, Enum):
    ALLOW = "allow"
    CONFIRM = "confirm"
    DENY = "deny"

CAPS = {
    "eval": Decision.ALLOW,
    "dsp": Decision.ALLOW,
    "build": Decision.ALLOW,
    "fetch": Decision.CONFIRM,
    "push": Decision.CONFIRM,
    "fs.delete": Decision.DENY,
    "power.off": Decision.DENY,
    "shell": Decision.DENY,
}

@dataclass
class Job:
    kind: str
    body: str = ""
    dest: str = ""
    url: str = ""

class Denied(Exception):
    pass

class NeedsConfirm(Exception):
    def __init__(self, job: Job):
        super().__init__(job.kind)
        self.job = job

class Companion:
    def __init__(self, caps=None):
        self.caps = dict(CAPS if caps is None else caps)
        self.role = "companion"
        self.mesh = False
    def decide(self, kind: str) -> Decision:
        return Decision(self.caps.get(kind, Decision.DENY))
    def submit(self, job: Job, confirmed: bool = False):
        d = self.decide(job.kind)
        if d is Decision.DENY:
            raise Denied("%s is not in the companion capability set" % job.kind)
        if d is Decision.CONFIRM and not confirmed:
            raise NeedsConfirm(job)
        return self._run(job)
    def _run(self, job: Job) -> str:
        if job.kind == "eval":
            from vulcan_run import run_source
            return run_source(job.body)
        if job.kind == "dsp":
            from vulcan_run import lut_sin_deg, lut_cos_deg, lut_sin_amp
            parts = job.body.split()
            if not parts:
                return "0"
            op = parts[0]
            deg = int(parts[1]) if len(parts) > 1 else 0
            if op == "cos":
                return str(lut_cos_deg(deg))
            if op == "amp":
                amp = int(parts[2]) if len(parts) > 2 else 32767
                return str(lut_sin_amp(deg, amp))
            return str(lut_sin_deg(deg))
        if job.kind == "build":
            return "RSV1 packed %d bytes\n" % len(job.body.encode("utf-8"))
        if job.kind == "fetch":
            if not job.url.startswith(("http://", "https://")):
                raise Denied("fetch only http(s)")
            with urlopen(job.url, timeout=20) as r:
                data = r.read(256 * 1024)
            dest = job.dest or "/tmp/oiria_fetch.bin"
            open(dest, "wb").write(data)
            return "fetched %d bytes -> %s\n" % (len(data), dest)
        if job.kind == "push":
            return "PUSH %d bytes to watch\n" % len(job.body)
        raise Denied("no runner for %s" % job.kind)
