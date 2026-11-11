"""Disposable Gold registrations and own-host participation; no peer/game admission.

Title fields are retained only in memory. Client-supplied principal/member
fields never establish authority. Catalogue publication awaits semantic and
content compatibility qualification.
"""
from dataclasses import dataclass, replace

from gathering_codec import SessionGathering, decode


@dataclass(frozen=True)
class Registration:
    identifier: int
    owner: object
    principal: int
    value: SessionGathering
    participating: bool = False
    host_claimed: bool = False
    released: bool = False


class LocalGatherings:
    def __init__(self):
        self.records: dict[int, Registration] = {}
        self.next_identifier = 1

    def create(self, owner, principal, data, reader_type):
        value = decode(data, reader_type)
        previous = [record for record in self.records.values() if record.owner is owner]
        if (owner is None or type(principal) is not int or not 0 < principal <= 0x7fffffff
                or not isinstance(value, SessionGathering) or value.matchmaking_word is not None
                or value.base.words[0] != 0 or len(self.records) - len(previous) >= 8
                or any(not record.released or record.principal != principal for record in previous)
                or self.next_identifier > 0xffffffff):
            raise ValueError("Registration not eligible")
        identifier = self.next_identifier
        # Native A89360 compares Gathering+0xc with the authenticated backend
        # principal. A client-provided host is never authoritative; only the
        # later owned participation/host-control transition establishes it.
        base = replace(value.base, words=(identifier, value.base.words[1], 0)
            + value.base.words[3:])
        record = Registration(identifier, owner, principal, replace(value, base=base))
        # Gold leaves through 21/12 and may immediately register another map
        # without deleting the old object. Retire only explicitly released own
        # records, after the complete new request and allocation are validated.
        for prior in previous:
            del self.records[prior.identifier]
        self.records[identifier] = record
        self.next_identifier += 1
        return identifier

    def remove(self, owner, identifier):
        record = self.records.get(identifier)
        if record is None or record.owner is not owner:
            return False
        del self.records[identifier]
        return True

    def remove_owner(self, owner):
        for identifier in [key for key, record in self.records.items() if record.owner is owner]:
            del self.records[identifier]

    def participate(self, owner, identifier, present):
        record = self.records.get(identifier)
        if record is None or record.owner is not owner or (present and record.released):
            return False
        value = record.value
        if not present:
            value = replace(value, base=replace(value.base,
                words=value.base.words[:2] + (0,) + value.base.words[3:]))
        self.records[identifier] = replace(record, value=value, participating=present,
            host_claimed=record.host_claimed if present else False, released=not present)
        return True

    def claim_host(self, owner, identifier):
        record = self.records.get(identifier)
        if record is None or record.owner is not owner or not record.participating:
            raise ValueError("Host registration not owned")
        base = replace(record.value.base,
            words=record.value.base.words[:2] + (record.principal,) + record.value.base.words[3:])
        self.records[identifier] = replace(record, value=replace(record.value, base=base),
            host_claimed=True)

    def get_owned(self, owner, identifier):
        record = self.records.get(identifier)
        if record is None or record.owner is not owner:
            raise ValueError("Registration not owned")
        return record.value

    def update(self, owner, data, reader_type):
        value = decode(data, reader_type)
        if not isinstance(value, SessionGathering) or value.matchmaking_word is not None:
            raise ValueError("Unsupported registration update")
        record = self.records.get(value.base.words[0])
        if (record is None or record.owner is not owner or not record.participating
                or not record.host_claimed or value.base != record.value.base):
            raise ValueError("Registration update not owned")
        # Only the mapped title attributes may change. Base authority, record
        # ownership and participation never come from the update payload.
        self.records[record.identifier] = replace(record, value=value)
