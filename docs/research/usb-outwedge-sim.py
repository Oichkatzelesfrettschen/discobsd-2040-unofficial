#!/usr/bin/env python3
"""Model the RP2040 bulk-OUT buffer-control handshake and the self-recovery.

The model carries one buffer control word with the datasheet's bits (4.1.2.7.4:
AVAILABLE bit 10, FULL bit 15) and the endpoint's USB_BUFF_STATUS bit. The
controller half follows 4.1.2.8.3: an OUT token is accepted into its data phase
only while AVAILABLE is set and FULL is clear, and the completion clears
AVAILABLE, sets FULL, and raises the BUFF_STATUS bit together in the status
phase. The driver half is usb_rx_done (drain, re-arm, advance the toggle) and
usb_rx_rearm_if_idle (re-arm only an idle un-armed buffer).

Run: PYTHON=${PYTHON:-python3}; $PYTHON usb-outwedge-sim.py
"""

AVAIL = 1 << 10
FULL = 1 << 15
DATA1 = 1 << 13
LEN_MASK = 0x3ff
PACKET_MAX = 64


class Endpoint:
    def __init__(self):
        self.bc = 0            # buffer control word
        self.buff_status = 0   # the endpoint's USB_BUFF_STATUS bit
        self.data_out_pid = 0
        self.configured = 0
        self.received = []     # payloads the driver drained

    # Driver: usb_buf_arm -- write control word, then set AVAILABLE.
    def arm(self, length, data1):
        self.bc = (length & LEN_MASK) | (DATA1 if data1 else 0)
        self.bc |= AVAIL

    # Driver: usb_configure(1) arms buffer 0 as DATA0, then data_out_pid = 1.
    def configure(self):
        self.configured = 1
        self.data_out_pid = 0
        self.arm(PACKET_MAX, False)
        self.data_out_pid = 1

    # Controller: accept one OUT packet if the datasheet's token phase allows.
    # Returns "ACK" or "NAK". A NAK is what the host retries until it times out
    # to EIO.
    def host_out(self, payload, pid):
        if not (self.bc & AVAIL) or (self.bc & FULL):
            return "NAK"
        self.bc &= ~AVAIL
        self.bc |= FULL | (len(payload) & LEN_MASK)
        self.buff_status = 1
        return "ACK"

    # Driver: usb_rx_done -- drain, re-arm with the current pid, advance it.
    def rx_done(self):
        n = self.bc & LEN_MASK
        self.received.append(n)
        self.buff_status = 0
        self.arm(PACKET_MAX, self.data_out_pid)
        self.data_out_pid ^= 1

    # Driver: usb_rx_rearm_if_idle -- re-arm only an idle un-armed buffer.
    def rearm_if_idle(self):
        if not self.configured:
            return False
        if (self.bc & (AVAIL | FULL)) == 0 and self.buff_status == 0:
            self.arm(PACKET_MAX, self.data_out_pid)
            return True
        return False


def bits(ep):
    return "AVAIL=%d FULL=%d BUFF_STATUS=%d pid=%d" % (
        1 if ep.bc & AVAIL else 0, 1 if ep.bc & FULL else 0,
        ep.buff_status, ep.data_out_pid)


def main():
    ep = Endpoint()
    ep.configure()
    print("after configure:               ", bits(ep))
    assert ep.bc & AVAIL, "configure must leave the buffer armed"

    # Steady state: five host OUT packets, each drained and re-armed. The guard
    # never fires here, because the buffer is armed whenever it is idle.
    fired = 0
    for _i in range(5):
        r = ep.host_out(b"x" * 3, ep.data_out_pid)
        assert r == "ACK", "armed buffer must ACK, got %s" % r
        ep.rx_done()
        if ep.rearm_if_idle():
            fired += 1
    print("after 5 OUT packets:           ", bits(ep), "guard fired", fired)
    assert fired == 0, "guard must not fire in steady state"
    assert ep.bc & AVAIL, "steady state leaves the buffer armed"

    # Wedge: the buffer loses its armed state with nothing pending -- AVAILABLE
    # clear, FULL clear, no BUFF_STATUS bit -- the state a lost re-arm leaves.
    ep.bc &= ~(AVAIL | FULL)
    ep.buff_status = 0
    print("wedged (un-armed, idle):       ", bits(ep))
    assert ep.host_out(b"y", ep.data_out_pid) == "NAK", \
        "un-armed buffer must NAK (host write -> EIO)"

    # Recovery: the guard fires exactly once and restores the armed state; a
    # second call does not arm again, so no double-arm.
    assert ep.rearm_if_idle(), "guard must fire on the wedge"
    print("after rearm_if_idle:           ", bits(ep))
    assert ep.bc & AVAIL, "recovery must re-arm the buffer"
    assert not ep.rearm_if_idle(), "no double-arm once armed"
    assert ep.host_out(b"y", ep.data_out_pid) == "ACK", \
        "recovered buffer must ACK the next OUT"
    ep.rx_done()
    print("after recovered OUT + rx_done: ", bits(ep))

    # Safety: with a received-but-undrained packet (FULL set, BUFF_STATUS set),
    # the guard refuses to arm, so no received data is dropped.
    ep.host_out(b"z" * 8, ep.data_out_pid)
    assert not ep.rearm_if_idle(), "guard must not arm over a full buffer"
    print("full buffer, guard refuses:    ", bits(ep), "(data preserved)")

    # Safety: with the controller owning the buffer (AVAILABLE set), the guard
    # refuses, so it never races a transaction in flight.
    ep.rx_done()
    assert not ep.rearm_if_idle(), "guard must not touch an armed buffer"
    print("armed buffer, guard refuses:   ", bits(ep))

    print("OK: recovery restores the armed state without a double-arm,")
    print("    and refuses to arm over an in-flight or full buffer.")


if __name__ == "__main__":
    main()
