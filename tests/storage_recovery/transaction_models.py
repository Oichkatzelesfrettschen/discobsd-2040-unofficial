"""Bounded undo, redo and immutable-delta publication over one byte interface.

These executable logical-storage models assume ordered, persistent byte-prefix
transfers and a publication byte whose interrupted prefix is either absent or
complete. Slots remain reserved, including aborted transactions; exhaustion
rejects the next transaction before mutation. The representation deliberately
excludes recycling, NOR erase/program rules and production migration. SHA-256
detects the tested corruptions; its collision resistance is an assumption.
"""

import hashlib
import struct
from dataclasses import dataclass

from storage_trace import Address, Device, Domain

POLICIES = {"undo": 1, "redo": 2, "copy-on-write": 3}
METADATA = struct.Struct("<8sBIIH32s")
FRAME = struct.Struct("<II32s32s")
RECORD = struct.Struct("<II")
HOME_OFFSET = 1024
MAGIC = b"DBSTOR01"
EMPTY, PREPARED, COMMITTED, ABORTED = range(4)


class RecoveryUnavailable(ValueError):
    """Persistent records cannot establish the required recovered image."""


class CapacityExhausted(ValueError):
    """The declared finite record capacity cannot admit another transaction."""


def require(condition, detail):
    if not condition:
        raise RecoveryUnavailable(detail)


def digest(data):
    return hashlib.sha256(data).digest()


def slot_bytes(max_records):
    size = 1 + FRAME.size + max_records * (RECORD.size + 512) + 32
    return (size + 1023) // 1024 * 1024


@dataclass(frozen=True)
class Transaction:
    slot: int
    status: int
    before_hash: bytes
    after_hash: bytes
    records: list


class Model:
    def __init__(self, kind, device, early_publication=False):
        require(kind in POLICIES, "unknown policy identity")
        self.kind = kind
        self.device = device
        self.early_publication = early_publication
        metadata = self.read(0, HOME_OFFSET)
        values = METADATA.unpack(metadata[:METADATA.size])
        magic, identity, self.image_size, self.max_records, self.capacity, self.seed_hash = values
        require(magic == MAGIC and identity == POLICIES[kind], "persistent policy identity differs")
        require(digest(metadata[:METADATA.size]) == metadata[METADATA.size:METADATA.size + 32],
                "metadata integrity differs")
        require(0 < self.image_size <= 16 * 1024 * 1024 and self.image_size % 512 == 0,
                "image size exceeds bounded model")
        require(1 <= self.max_records <= 64 and 1 <= self.capacity <= 4,
                "record capacity exceeds bounded model")
        self.slot_size = slot_bytes(self.max_records)
        self.journal_offset = HOME_OFFSET + self.image_size
        require(len(device.export()) == self.journal_offset + self.capacity * self.slot_size,
                "persistent medium length differs")

    def read(self, offset, length):
        result = bytearray()
        while len(result) < length:
            chunk = self.device.read(Address(Domain.IMAGE_BYTE, offset + len(result)),
                                     length - len(result))
            require(bool(chunk), "zero-length read before record completion")
            result.extend(chunk)
        return bytes(result)

    def write(self, offset, data, kind="write"):
        done = 0
        while done < len(data):
            amount = self.device.write(Address(Domain.IMAGE_BYTE, offset + done), data[done:], kind)
            require(amount > 0, "zero-length write before record completion")
            done += amount

    def publish(self, slot, status):
        self.write(self.journal_offset + slot * self.slot_size, bytes([status]), "publish")

    def frame(self, slot, status):
        offset = self.journal_offset + slot * self.slot_size + 1
        header = self.read(offset, FRAME.size)
        generation, length, before_hash, after_hash = FRAME.unpack(header)
        require(generation == slot + 1, "stale or misidentified generation")
        require(length <= self.max_records * 520 and length % 520 == 0,
                "transaction payload bounds differ")
        payload = self.read(offset + FRAME.size, length + 32)
        require(digest(header + payload[:length]) == payload[length:],
                "transaction record integrity differs")
        records = []
        previous = -1
        for position in range(0, length, 520):
            address, count = RECORD.unpack(payload[position:position + RECORD.size])
            require(count == 512 and address % 512 == 0 and address <= self.image_size - count,
                    "transaction address bounds differ")
            require(address > previous, "duplicate or reordered transaction address")
            records.append((address, payload[position + RECORD.size:position + 520]))
            previous = address
        return Transaction(slot, status, before_hash, after_hash, records)

    def scan(self):
        frames = []
        expected = self.seed_hash
        gap = False
        pending = False
        for slot in range(self.capacity):
            status = self.read(self.journal_offset + slot * self.slot_size, 1)[0]
            if status == EMPTY:
                gap = True
                continue
            require(not gap and not pending, "publication follows an empty or pending generation")
            require(status in (PREPARED, COMMITTED, ABORTED), "unknown publication marker")
            require(self.kind == "undo" or status == COMMITTED, "invalid policy publication state")
            frame = self.frame(slot, status)
            require(frame.before_hash == expected, "transaction predecessor differs")
            if status == COMMITTED:
                expected = frame.after_hash
            elif status == PREPARED:
                pending = True
            frames.append(frame)
        return frames, expected

    def immutable_image(self, frames):
        image = bytearray(self.read(HOME_OFFSET, self.image_size))
        require(digest(image) == self.seed_hash, "immutable base integrity differs")
        for frame in frames:
            if frame.status != COMMITTED:
                continue
            require(digest(image) == frame.before_hash, "immutable predecessor differs")
            for offset, data in frame.records:
                image[offset:offset + len(data)] = data
            require(digest(image) == frame.after_hash, "immutable payload integrity differs")
        return bytes(image)

    def transaction(self, after):
        self.device.phase = "operation"
        require(len(after) == self.image_size, "transaction image length differs")
        frames, expected = self.scan()
        if len(frames) == self.capacity:
            raise CapacityExhausted("reserved transaction slots exhausted")
        require(all(frame.status != PREPARED for frame in frames),
                "recovery required before mutation")
        before = (self.immutable_image(frames) if self.kind == "copy-on-write" else
                  self.read(HOME_OFFSET, self.image_size))
        require(digest(before) == expected, "recovery required before mutation")
        changed = [(offset, before[offset:offset + 512], after[offset:offset + 512])
                   for offset in range(0, self.image_size, 512)
                   if before[offset:offset + 512] != after[offset:offset + 512]]
        if len(changed) > self.max_records:
            raise CapacityExhausted("transaction exceeds reserved record capacity")
        slot = len(frames)
        payload = b"".join(RECORD.pack(offset, 512) + (old if self.kind == "undo" else new)
                           for offset, old, new in changed)
        header = FRAME.pack(slot + 1, len(payload), digest(before), digest(after))
        record = header + payload + digest(header + payload)
        marker = PREPARED if self.kind == "undo" else COMMITTED
        if self.early_publication:
            self.publish(slot, marker)
        self.write(self.journal_offset + slot * self.slot_size + 1, record)
        self.frame(slot, marker)
        if not self.early_publication:
            self.publish(slot, marker)
        if self.kind != "copy-on-write":
            for offset, _old, new in changed:
                self.write(HOME_OFFSET + offset, new)
        if self.kind == "undo":
            self.publish(slot, COMMITTED)
        return slot + 1

    def recover(self):
        self.device.phase = "recovery"
        frames, expected = self.scan()
        if self.kind == "copy-on-write":
            return self.immutable_image(frames)
        if self.kind == "undo":
            pending = [frame for frame in frames if frame.status == PREPARED]
            if pending:
                frame = pending[0]
                for offset, old in frame.records:
                    self.write(HOME_OFFSET + offset, old)
                require(digest(self.read(HOME_OFFSET, self.image_size)) == frame.before_hash,
                        "undo restoration integrity differs")
                self.publish(frame.slot, ABORTED)
        else:
            for frame in frames:
                if frame.status == COMMITTED:
                    for offset, new in frame.records:
                        self.write(HOME_OFFSET + offset, new)
        image = self.read(HOME_OFFSET, self.image_size)
        require(digest(image) == expected, "recovered image integrity differs")
        return image

    def costs(self):
        writes = [event for event in self.device.events if event.kind != "read"]
        reads = [event for event in self.device.events if event.kind == "read"]
        return {
            "logical_write_calls": len(writes),
            "logical_written_bytes": sum(event.transferred for event in writes),
            "logical_read_calls": len(reads),
            "logical_read_bytes": sum(event.transferred for event in reads),
            "publication_records": sum(event.kind == "publish" for event in writes),
            "reserved_metadata_bytes": HOME_OFFSET + self.capacity * self.slot_size,
            "physical_program_pages": None,
            "physical_erase_sectors": None,
            "dhara_checkpoint_operations": None,
            "loaded_firmware_code_bytes": None,
        }


def new_model(kind, image, capacity=2, max_records=8, early_publication=False):
    require(kind in POLICIES, "unknown policy identity")
    require(1 <= capacity <= 4 and 1 <= max_records <= 64, "invalid finite model capacity")
    metadata = METADATA.pack(MAGIC, POLICIES[kind], len(image), max_records, capacity,
                             digest(image))
    prefix = metadata + digest(metadata)
    medium = prefix + bytes(HOME_OFFSET - len(prefix)) + image
    medium += bytes(capacity * slot_bytes(max_records))
    return Model(kind, Device(medium, layer="transaction-model"), early_publication)


def open_model(kind, device, early_publication=False):
    return Model(kind, device, early_publication)
