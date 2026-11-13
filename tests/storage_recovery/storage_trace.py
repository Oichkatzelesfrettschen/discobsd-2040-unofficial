"""Typed, byte-addressed transfer traces for host fault and recovery tests.

The persistent image records exactly the transferred prefix. Interruption
freezes that image before callers can compensate. The model makes a prefix
assumption explicitly; arbitrary programmed-bit subsets and electrical erase
behavior require separate models and measurements.
"""

import json
from dataclasses import asdict, dataclass, field
from enum import Enum


class Domain(str, Enum):
    IMAGE_BYTE = "image-byte"
    V7_BLOCK = "v7-block"
    KERNEL_BLOCK = "kernel-block"
    DHARA_PAGE = "dhara-page"
    DHARA_SECTOR = "dhara-logical-sector"
    FLASH_BYTE = "flash-byte"
    PROGRAM_PAGE = "physical-program-page"
    ERASE_SECTOR = "physical-erase-sector"
    DHARA_BLOCK = "dhara-erase-block"


UNIT_BYTES = {
    Domain.IMAGE_BYTE: 1, Domain.V7_BLOCK: 512, Domain.KERNEL_BLOCK: 1024,
    Domain.DHARA_PAGE: 1024, Domain.DHARA_SECTOR: 1024,
    Domain.FLASH_BYTE: 1, Domain.PROGRAM_PAGE: 256,
    Domain.ERASE_SECTOR: 4096, Domain.DHARA_BLOCK: 8192,
}


@dataclass(frozen=True)
class Address:
    domain: Domain
    offset: int

    def __post_init__(self):
        if not isinstance(self.domain, Domain) or type(self.offset) is not int or self.offset < 0:
            raise ValueError("address requires an explicit domain and nonnegative integer")


def checked_range(offset, length, limit):
    if any(type(value) is not int for value in (offset, length, limit)):
        raise ValueError("byte range requires integers")
    if limit < 0 or offset < 0 or offset > limit or length < 0 or length > limit - offset:
        raise ValueError(f"range {offset}+{length} exceeds {limit}")


def byte_address(address, limit, origin=0):
    checked_range(origin, 0, limit)
    unit = UNIT_BYTES[address.domain]
    if address.offset > (limit - origin) // unit:
        raise ValueError("unit multiplication exceeds byte bound")
    domain = Domain.FLASH_BYTE if address.domain in {
        Domain.FLASH_BYTE, Domain.DHARA_PAGE, Domain.PROGRAM_PAGE,
        Domain.ERASE_SECTOR, Domain.DHARA_BLOCK,
    } else Domain.IMAGE_BYTE
    return Address(domain, origin + address.offset * unit)


def split_address(address, domain):
    expected = byte_address(Address(domain, 0), 0).domain
    if address.domain != expected:
        raise ValueError("split requires the matching byte address domain")
    unit = UNIT_BYTES[domain]
    number, displacement = divmod(address.offset, unit)
    return Address(domain, number), displacement


def encode(value, width, order="little", signed=False):
    if width not in (1, 2, 3, 4) or order not in ("little", "pdp"):
        raise ValueError("unsupported persistent codec")
    lower = -(1 << (width * 8 - 1)) if signed else 0
    upper = (1 << (width * 8 - int(signed))) - 1
    if type(value) is not int or value < lower or value > upper:
        raise ValueError("persistent value is outside its admitted range")
    result = value.to_bytes(width, "little", signed=signed)
    if order == "pdp" and width == 3:
        result = result[2:] + result[:2]
    elif order == "pdp" and width == 4:
        result = result[2:] + result[:2]
    return result


def decode(data, offset, width, order="little", signed=False):
    checked_range(offset, width, len(data))
    if width not in (1, 2, 3, 4) or order not in ("little", "pdp"):
        raise ValueError("unsupported persistent codec")
    raw = data[offset:offset + width]
    if order == "pdp" and width == 3:
        raw = raw[1:] + raw[:1]
    elif order == "pdp" and width == 4:
        raw = raw[2:] + raw[:2]
    return int.from_bytes(raw, "little", signed=signed)


@dataclass
class Fault:
    sequence: int
    mode: str
    transferred: int
    hits: int = field(default=0, init=False)

    def __post_init__(self):
        if type(self.sequence) is not int or self.sequence < 1 or self.mode not in {
            "before_error", "after_error", "short", "interrupt",
        } or type(self.transferred) is not int or self.transferred < 0:
            raise ValueError("invalid transfer fault")
        if self.mode == "before_error" and self.transferred != 0:
            raise ValueError("failure before transfer must transfer zero bytes")


@dataclass(frozen=True)
class Event:
    sequence: int
    layer: str
    kind: str
    domain: str
    offset: int
    length: int
    phase: str
    transferred: int
    result: str


class TransferError(Exception):
    """A transfer reports failure after its recorded persistent effect."""


class Interruption(Exception):
    """Execution stops with the recorded persistent prefix frozen."""


class Device:
    def __init__(self, image, faults=(), domain=Domain.IMAGE_BYTE, layer="v7fs-descriptor"):
        if domain not in (Domain.IMAGE_BYTE, Domain.FLASH_BYTE):
            raise ValueError("device requires a byte domain")
        self._persistent = bytearray(image)
        self.domain = domain
        self.layer = layer
        self.phase = "operation"
        self.events = []
        self.faults = {fault.sequence: fault for fault in faults}
        if len(self.faults) != len(faults):
            raise ValueError("duplicate injection sequence")
        self.frozen = False

    def export(self):
        return bytes(self._persistent)

    def transfer(self, kind, address, length, payload=None):
        if self.frozen:
            raise Interruption("persistent image is frozen")
        if address.domain != self.domain:
            raise ValueError("transfer address domain differs from device")
        checked_range(address.offset, length, len(self._persistent))
        if kind not in {"read", "write", "program", "erase", "checkpoint", "publish"}:
            raise ValueError("unsupported operation kind")
        if payload is not None and len(payload) != length:
            raise ValueError("payload length differs from transfer")
        sequence = len(self.events) + 1
        fault = self.faults.get(sequence)
        amount = length
        result = "complete"
        if fault:
            if fault.transferred > length or (fault.mode == "short" and
                                              not 0 < fault.transferred < length):
                raise ValueError("injection transfer count is outside the requested operation")
            fault.hits += 1
            amount, result = fault.transferred, fault.mode
        offset = address.offset
        if payload is not None:
            self._persistent[offset:offset + amount] = payload[:amount]
        data = bytes(self._persistent[offset:offset + amount])
        self.events.append(Event(sequence, self.layer, kind, self.domain.value, offset, length,
                                 self.phase, amount, result))
        if result == "interrupt":
            self.frozen = True
            raise Interruption(f"interrupted transfer {sequence}")
        if result in ("before_error", "after_error"):
            raise TransferError(f"failed transfer {sequence}: {result}, {amount} bytes")
        return data if payload is None else amount

    def read(self, address, length):
        return self.transfer("read", address, length)

    def write(self, address, payload, kind="write"):
        return self.transfer(kind, address, len(payload), payload)

    def verify_injections(self):
        inactive = [sequence for sequence, fault in self.faults.items() if fault.hits != 1]
        if inactive:
            raise ValueError(f"inactive injection or repeated activation: {inactive}")

    def json_lines(self):
        return "".join(json.dumps(asdict(event), sort_keys=True) + "\n" for event in self.events)
