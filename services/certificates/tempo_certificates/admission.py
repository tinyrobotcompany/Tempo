"""Bound unauthenticated work before storage; no caller-controlled cache keys."""
import threading
import time


class Admission:
    # Per API process: maximum burst 60, sustained 5/s. With maxReplicas=3,
    # scaling cannot create an unbounded storage-admission budget.
    def __init__(self, clock=time.monotonic, capacity=60, rate=5):
        self.clock, self.capacity, self.rate = clock, capacity, rate
        self.tokens, self.updated = capacity, clock()
        self.lock = threading.Lock()

    def accept(self):
        with self.lock:
            now = self.clock()
            self.tokens = min(self.capacity, self.tokens + max(0, now - self.updated) * self.rate)
            self.updated = now
            if self.tokens < 1:
                return False
            self.tokens -= 1
            return True
