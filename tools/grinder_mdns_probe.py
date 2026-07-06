import argparse
import ipaddress
import socket
import struct
import time


TYPE_A = 1
TYPE_PTR = 12
TYPE_TXT = 16
TYPE_AAAA = 28
TYPE_SRV = 33
CLASS_IN = 1
MDNS_IPV4 = "224.0.0.251"
MDNS_PORT = 5353


class DnsDecodeError(Exception):
    pass


def encode_name(name):
    parts = [part for part in name.rstrip(".").split(".") if part]
    payload = bytearray()
    for part in parts:
        encoded = part.encode("utf-8")
        if len(encoded) > 63:
            raise ValueError(f"DNS label too long: {part}")
        payload.append(len(encoded))
        payload.extend(encoded)
    payload.append(0)
    return bytes(payload)


def decode_name(packet, offset):
    labels = []
    seen = set()
    consumed = None
    cursor = offset
    while True:
      if cursor >= len(packet):
          raise DnsDecodeError("name offset outside packet")
      length = packet[cursor]
      if length == 0:
          cursor += 1
          if consumed is None:
              consumed = cursor
          return ".".join(labels) + ".", consumed
      if (length & 0xC0) == 0xC0:
          if cursor + 1 >= len(packet):
              raise DnsDecodeError("truncated compression pointer")
          pointer = ((length & 0x3F) << 8) | packet[cursor + 1]
          if pointer in seen:
              raise DnsDecodeError("compression pointer loop")
          seen.add(pointer)
          if consumed is None:
              consumed = cursor + 2
          cursor = pointer
          continue
      if length & 0xC0:
          raise DnsDecodeError("invalid name label")
      cursor += 1
      if cursor + length > len(packet):
          raise DnsDecodeError("truncated name label")
      labels.append(packet[cursor:cursor + length].decode("utf-8", errors="replace"))
      cursor += length


def build_query(service, unicast):
    qclass = CLASS_IN | (0x8000 if unicast else 0)
    header = struct.pack("!HHHHHH", 0, 0, 1, 0, 0, 0)
    question = encode_name(service) + struct.pack("!HH", TYPE_PTR, qclass)
    return header + question


def parse_txt(data):
    values = {}
    cursor = 0
    while cursor < len(data):
        length = data[cursor]
        cursor += 1
        if cursor + length > len(data):
            raise DnsDecodeError("truncated TXT")
        item = data[cursor:cursor + length].decode("utf-8", errors="replace")
        cursor += length
        if "=" in item:
            key, value = item.split("=", 1)
            values[key] = value
        else:
            values[item] = ""
    return values


def parse_record(packet, offset):
    name, offset = decode_name(packet, offset)
    if offset + 10 > len(packet):
        raise DnsDecodeError("truncated record header")
    rtype, rclass, ttl, rdlength = struct.unpack("!HHIH", packet[offset:offset + 10])
    offset += 10
    rdata_offset = offset
    offset += rdlength
    if offset > len(packet):
        raise DnsDecodeError("truncated record data")
    data = packet[rdata_offset:offset]
    value = None
    if rtype == TYPE_PTR:
        value = decode_name(packet, rdata_offset)[0]
    elif rtype == TYPE_SRV:
        if len(data) < 6:
            raise DnsDecodeError("truncated SRV")
        priority, weight, port = struct.unpack("!HHH", data[:6])
        target = decode_name(packet, rdata_offset + 6)[0]
        value = {
            "priority": priority,
            "weight": weight,
            "port": port,
            "target": target,
        }
    elif rtype == TYPE_TXT:
        value = parse_txt(data)
    elif rtype == TYPE_A and len(data) == 4:
        value = str(ipaddress.IPv4Address(data))
    elif rtype == TYPE_AAAA and len(data) == 16:
        value = str(ipaddress.IPv6Address(data))
    else:
        value = data.hex()
    return {
        "name": name,
        "type": rtype,
        "class": rclass & 0x7FFF,
        "cache_flush": bool(rclass & 0x8000),
        "ttl": ttl,
        "value": value,
    }, offset


def parse_packet(packet):
    if len(packet) < 12:
        raise DnsDecodeError("packet too short")
    _, flags, qdcount, ancount, nscount, arcount = struct.unpack("!HHHHHH", packet[:12])
    offset = 12
    for _ in range(qdcount):
        _, offset = decode_name(packet, offset)
        if offset + 4 > len(packet):
            raise DnsDecodeError("truncated question")
        offset += 4
    records = []
    for _ in range(ancount + nscount + arcount):
        record, offset = parse_record(packet, offset)
        records.append(record)
    return flags, records


def infer_interface_ip(target_ip):
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        sock.connect((target_ip, 80))
        return sock.getsockname()[0]
    finally:
        sock.close()


def configure_multicast(sock, interface_ip):
    sock.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_TTL, 255)
    if interface_ip:
        sock.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_IF, socket.inet_aton(interface_ip))


def make_socket(bind_multicast, interface_ip):
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
    sock.settimeout(0.25)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    if hasattr(socket, "SO_REUSEPORT"):
        try:
            sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEPORT, 1)
        except OSError:
            pass
    configure_multicast(sock, interface_ip)
    if bind_multicast:
        try:
            sock.bind(("", MDNS_PORT))
            membership = socket.inet_aton(MDNS_IPV4) + socket.inet_aton(interface_ip or "0.0.0.0")
            sock.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP, membership)
        except OSError:
            sock.close()
            return None
    else:
        sock.bind(("", 0))
    return sock


def collect_records(service, timeout, interface_ip):
    sockets = [sock for sock in [make_socket(False, interface_ip), make_socket(True, interface_ip)] if sock is not None]
    packets = [build_query(service, True), build_query(service, False)]
    for sock in sockets:
        for packet in packets:
            sock.sendto(packet, (MDNS_IPV4, MDNS_PORT))
    records = []
    errors = []
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        for sock in sockets:
            try:
                packet, remote = sock.recvfrom(9000)
            except socket.timeout:
                continue
            except OSError as exc:
                errors.append(str(exc))
                continue
            try:
                _, parsed = parse_packet(packet)
                for record in parsed:
                    enriched = dict(record)
                    enriched["remote"] = f"{remote[0]}:{remote[1]}"
                    records.append(enriched)
            except DnsDecodeError as exc:
                errors.append(str(exc))
    for sock in sockets:
        sock.close()
    return records, errors


def normalize_name(name):
    return name.rstrip(".").lower()


def unique_records(records):
    seen = set()
    unique = []
    for record in records:
        marker = (record["name"], record["type"], repr(record["value"]))
        if marker in seen:
            continue
        seen.add(marker)
        unique.append(record)
    return unique


def type_name(rtype):
    names = {
        TYPE_A: "A",
        TYPE_PTR: "PTR",
        TYPE_TXT: "TXT",
        TYPE_AAAA: "AAAA",
        TYPE_SRV: "SRV",
    }
    return names.get(rtype, str(rtype))


def format_value(value):
    if isinstance(value, dict):
        parts = []
        for key in sorted(value.keys()):
            parts.append(f"{key}={value[key]}")
        return " ".join(parts)
    return str(value)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--service", default="_grinderplug._tcp.local")
    parser.add_argument("--timeout", type=float, default=5.0)
    parser.add_argument("--expected-mac", default="1C:69:20:0B:54:20")
    parser.add_argument("--expected-model", default="")
    parser.add_argument("--expected-proto", default="1")
    parser.add_argument("--target-ip", default="192.168.178.30")
    parser.add_argument("--interface-ip", default="")
    args = parser.parse_args()

    interface_ip = args.interface_ip or infer_interface_ip(args.target_ip)
    print(f"INFO\tinterface={interface_ip} service={args.service}")

    records, errors = collect_records(args.service, args.timeout, interface_ip)
    records = unique_records(records)
    service = normalize_name(args.service)
    ptr_targets = [record["value"] for record in records if record["type"] == TYPE_PTR and normalize_name(record["name"]) == service]
    txt_records = [record for record in records if record["type"] == TYPE_TXT and any(normalize_name(record["name"]) == normalize_name(target) for target in ptr_targets)]
    srv_records = [record for record in records if record["type"] == TYPE_SRV and any(normalize_name(record["name"]) == normalize_name(target) for target in ptr_targets)]
    txt_values = {}
    for record in txt_records:
        if isinstance(record["value"], dict):
            txt_values.update(record["value"])

    checks = {
        "service": bool(ptr_targets),
        "srv": bool(srv_records),
        "txt": bool(txt_records),
        "mac": txt_values.get("mac") == args.expected_mac,
        "model": not args.expected_model or txt_values.get("model") == args.expected_model,
        "proto": txt_values.get("proto") == args.expected_proto,
    }

    for record in records:
        print(f"{type_name(record['type'])}\t{record['name']}\t{format_value(record['value'])}\tfrom={record['remote']}")

    if errors:
        for error in sorted(set(errors)):
            print(f"WARN\t{error}")

    print("CHECKS\t" + " ".join(f"{key}={value}" for key, value in checks.items()))

    if not all(checks.values()):
        raise SystemExit(1)


if __name__ == "__main__":
    main()
