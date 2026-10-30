import io
import unittest
from contextlib import redirect_stdout
from unittest.mock import patch

import grinder_mdns_probe as probe


SERVICE = "_grinderplug._tcp.local."
EXPECTED_MAC = "AA:BB:CC:DD:EE:01"
EXPECTED_IP = "192.0.2.10"


def record(name, rtype, value):
    return {"name": name, "type": rtype, "value": value, "remote": "192.0.2.1:5353", "ttl": 120}


def plug_records(label, mac, address, model="NOUS_A6T", port=31980):
    instance = f"{label}.{SERVICE}"
    hostname = f"{label}.local."
    return (
        record(SERVICE, probe.TYPE_PTR, instance),
        record(instance, probe.TYPE_SRV, {"port": port, "target": hostname}),
        record(instance, probe.TYPE_TXT, {"mac": mac, "model": model, "proto": "1"}),
        record(hostname, probe.TYPE_A, address),
    )


def probe_result(records, expected_model="NOUS_A6T"):
    argv = [
        "grinder_mdns_probe.py", "--expected-mac", EXPECTED_MAC,
        "--expected-model", expected_model, "--target-ip", EXPECTED_IP,
        "--interface-ip", "192.0.2.100",
    ]
    output = io.StringIO()
    with (
        patch("sys.argv", argv),
        patch.object(probe, "collect_records", return_value=(records, [])),
        patch.object(probe.socket, "socket", side_effect=AssertionError("Offline test opened a socket")),
        redirect_stdout(output),
    ):
        try:
            probe.main()
        except SystemExit as error:
            return error.code, output.getvalue()
    return 0, output.getvalue()


class DiscoveryQualificationTests(unittest.TestCase):
    def setUp(self):
        self.expected = plug_records("plug-a", EXPECTED_MAC, EXPECTED_IP)
        self.other = plug_records("plug-b", "AA:BB:CC:DD:EE:02", "192.0.2.20", "TASMOTA32_GRINDER")

    def test_two_plugs_pass_in_different_record_orders(self):
        orders = (self.expected + self.other, self.other + self.expected, tuple(reversed(self.expected + self.other)))
        for records in orders:
            with self.subTest(records=records):
                status, output = probe_result(records)
                self.assertEqual(0, status, output)
                self.assertIn("INFO\t", output)
                self.assertIn("CHECKS\t", output)

    def test_missing_expected_srv_cannot_borrow_other_plug_srv(self):
        records = self.other + tuple(item for item in self.expected if item["type"] != probe.TYPE_SRV)
        self.assertEqual(1, probe_result(records)[0])

    def test_wrong_expected_port_cannot_borrow_other_plug_port(self):
        records = self.other + plug_records("plug-a", EXPECTED_MAC, EXPECTED_IP, port=31981)
        self.assertEqual(1, probe_result(records)[0])

    def test_missing_expected_address_cannot_borrow_unrelated_address(self):
        records = self.other + tuple(item for item in self.expected if item["type"] != probe.TYPE_A)
        records = records + (record("unrelated.local.", probe.TYPE_A, EXPECTED_IP),)
        self.assertEqual(1, probe_result(records)[0])

    def test_wrong_target_address_fails(self):
        records = self.other + plug_records("plug-a", EXPECTED_MAC, "192.0.2.99")
        self.assertEqual(1, probe_result(records)[0])

    def test_port_and_address_must_belong_to_the_same_srv(self):
        records = self.other + plug_records("plug-a", EXPECTED_MAC, EXPECTED_IP, port=31981)
        records = records + (record(f"plug-a.{SERVICE}", probe.TYPE_SRV, {"port": 31980, "target": "plug-b.local."}),)
        self.assertEqual(1, probe_result(records)[0])

    def test_txt_fields_cannot_be_borrowed_from_another_instance(self):
        records = tuple(item for item in self.expected if item["type"] != probe.TYPE_TXT)
        records = records + (
            record(f"plug-a.{SERVICE}", probe.TYPE_TXT, {"mac": EXPECTED_MAC}),
            record(SERVICE, probe.TYPE_PTR, f"plug-b.{SERVICE}"),
            record(f"plug-b.{SERVICE}", probe.TYPE_TXT, {"model": "NOUS_A6T", "proto": "1"}),
        )
        self.assertEqual(1, probe_result(records)[0])

    def test_complete_txt_after_partial_registration_passes(self):
        records = (record(f"plug-a.{SERVICE}", probe.TYPE_TXT, {"mac": EXPECTED_MAC}),) + self.expected
        self.assertEqual(0, probe_result(records)[0])

    def test_optional_model_and_dns_name_normalization_are_preserved(self):
        records = tuple({**item, "name": item["name"].upper().rstrip(".")} for item in self.expected)
        self.assertEqual(0, probe_result(records, expected_model="")[0])


if __name__ == "__main__":
    unittest.main()
