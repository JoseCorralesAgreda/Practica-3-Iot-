"""Check MQTT rules with deterministic clocks and local client doubles."""

import asyncio
import json
from pathlib import Path
import sys
import threading
import unittest
from unittest.mock import Mock, patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import mqtt_client as module
from mqtt_client import MqttClient


def message(payload: bytes, *, retained: bool = False, topic: str = module.DISTANCE_TOPIC) -> Mock:
    """Build a received message without opening a network connection."""
    return Mock(payload=payload, retain=retained, topic=topic)


class ClientTests(unittest.IsolatedAsyncioTestCase):
    """Exercise cache, validation and pending-command transitions."""

    def setUp(self) -> None:
        """Create a ready session with a controllable monotonic clock."""
        self.now = 100.0
        clock = patch.object(module, "time")
        self.clock = clock.start()
        self.addCleanup(clock.stop)
        self.clock.monotonic.side_effect = lambda: self.now
        self.client = MqttClient()
        self.network = Mock()
        self.network.subscribe.return_value = (module.mqtt.MQTT_ERR_SUCCESS, 17)
        self.network.publish.return_value = Mock(rc=module.mqtt.MQTT_ERR_SUCCESS, mid=23)
        self.client._client = self.network
        self.client._ready = True
        self.client._phase_deadline = self.now + module.HANDSHAKE_TIMEOUT_SECONDS
        self.success = Mock(is_failure=False)

    async def submit(self, led: str = "red", state: str = "on") -> asyncio.Task:
        """Admit a command and return while it is waiting for delivery."""
        task = asyncio.create_task(self.client.set_led(led, state))
        await asyncio.sleep(0)
        self.assertIsNotNone(self.client._pending)
        return task

    def acknowledge(self, mid: int = 23) -> None:
        """Deliver a broker acknowledgement to the current session."""
        self.client._on_publish(self.network, None, mid, self.success, None)

    def test_valid_samples_and_age_boundary(self) -> None:
        """Accept endpoints and distinguish the exact staleness boundary."""
        self.assertEqual(self.client.get_distance()["status"], "no_data")
        for value in (5, 42.7, 200):
            with self.subTest(value=value):
                self.client._on_message(
                    self.network, None, message(json.dumps({"cm": value}).encode())
                )
                self.assertEqual(self.client.get_distance()["cm"], value)
        self.now += 3.0
        self.assertEqual(self.client.get_distance()["status"], "ok")
        self.now += 0.0001
        result = self.client.get_distance()
        self.assertEqual(result["status"], "stale")
        self.assertEqual(result["cm"], 200)
        self.assertEqual(result["age_seconds"], 3.0)

    def test_invalid_samples_do_not_refresh_cache(self) -> None:
        """Reject malformed, nonnumeric, out-of-range and oversized samples."""
        self.client._on_message(self.network, None, message(b'{"cm":42.7}'))
        self.now += 4
        invalid = (
            b"", b"{", b"[]", b"null", b"{}", b"\xff", b'{"cm":"42.7"}',
            b'{"cm":true}', b'{"cm":null}', b'{"cm":NaN}', b'{"cm":Infinity}',
            b'{"cm":-Infinity}', b'{"cm":1e999}', b'{"cm":4.99}', b'{"cm":200.01}',
            b'{"cm":42,"extra":0}', b'{"cm":42,"cm":43}', b'{"cm":{}}',
            b'{"cm":42}junk', b'{"cm":42}' + b" " * 120,
        )
        for payload in invalid:
            with self.subTest(payload=payload):
                self.client._on_message(self.network, None, message(payload))
                result = self.client.get_distance()
                self.assertEqual(result["cm"], 42.7)
                self.assertEqual(result["age_seconds"], 4)
                self.assertEqual(result["status"], "stale")

    def test_wrong_topic_retained_and_retired_client(self) -> None:
        """Keep unrelated, retained and previous-session data out of the cache."""
        for packet in (message(b'{"cm":50}', retained=True), message(b'{"cm":50}', topic="other")):
            self.client._on_message(self.network, None, packet)
        self.client._on_message(Mock(), None, message(b'{"cm":50}'))
        self.assertEqual(self.client.get_distance()["status"], "no_data")

    def test_disconnect_invalidates_cache(self) -> None:
        """Never label an old session's sample as current after reconnecting."""
        self.client._on_message(self.network, None, message(b'{"cm":50}'))
        self.client._on_disconnect(self.network, None, None, self.success, None)
        result = self.client.get_distance()
        self.assertEqual(result["status"], "disconnected")
        self.assertIsNone(result["cm"])
        self.assertFalse(result["mqtt_connected"])
        self.client._retire = False
        self.client._on_connect(self.network, None, None, self.success, None)
        self.client._on_subscribe(self.network, None, 17, [0], None)
        self.assertEqual(self.client.get_distance()["status"], "no_data")

    def test_suback_validation(self) -> None:
        """Require the matching acknowledgement with the requested QoS."""
        self.client._ready = False
        self.client._on_connect(self.network, None, None, self.success, None)
        self.network.subscribe.assert_called_once_with(module.DISTANCE_TOPIC, qos=0)
        self.client._on_subscribe(self.network, None, 18, [0], None)
        self.assertFalse(self.client._ready)
        self.client._on_subscribe(self.network, None, 17, [128], None)
        self.assertTrue(self.client._retire)
        self.assertFalse(self.client._ready)

    def test_connection_and_subscription_failures(self) -> None:
        """Keep tools unavailable after a rejected handshake or local subscribe failure."""
        self.client._ready = False
        self.client._on_connect(self.network, None, None, Mock(is_failure=True), None)
        self.assertTrue(self.client._retire)
        self.network.subscribe.assert_not_called()
        self.client._retire = False
        self.network.subscribe.return_value = (module.mqtt.MQTT_ERR_NO_CONN, None)
        self.client._on_connect(self.network, None, None, self.success, None)
        self.assertFalse(self.client._ready)
        self.assertTrue(self.client._retire)

    def test_late_handshake_callbacks_do_not_enable_tools(self) -> None:
        """A late CONNACK or SUBACK cannot bypass the worker's phase deadline."""
        self.client._ready = False
        self.now += 5
        self.client._on_connect(self.network, None, None, self.success, None)
        self.assertTrue(self.client._retire)
        self.network.subscribe.assert_not_called()
        self.client._retire = False
        self.client._subscription_mid = 17
        self.client._on_subscribe(self.network, None, 17, [0], None)
        self.assertTrue(self.client._retire)
        self.assertFalse(self.client._ready)

    async def test_all_six_commands(self) -> None:
        """Publish exact payloads and report broker acknowledgement only."""
        for led in ("red", "yellow", "green"):
            for state in ("on", "off"):
                with self.subTest(led=led, state=state):
                    task = await self.submit(led, state)
                    self.client._process_command(self.network)
                    expected_payload = json.dumps(
                        {"led": led, "state": state}, separators=(",", ":")
                    )
                    self.network.publish.assert_called_with(
                        module.COMMAND_TOPIC,
                        expected_payload,
                        qos=1,
                        retain=False,
                    )
                    self.acknowledge()
                    result = await task
                    self.assertEqual(result["status"], "published")
                    self.assertTrue(result["broker_acknowledged"])
                    self.assertFalse(result["device_confirmed"])

    async def test_invalid_parameters(self) -> None:
        """Reject invalid values before any MQTT publication."""
        invalid_commands = (
            ("blue", "on"), ("RED", "on"), ("red", "ON"),
            ("red", True), (None, "on"),
        )
        for led, state in invalid_commands:
            with self.subTest(led=led, state=state):
                with self.assertRaises(ValueError):
                    await self.client.set_led(led, state)
        self.network.publish.assert_not_called()

    async def test_disconnected_and_busy(self) -> None:
        """Reject offline and concurrent commands while allowing reads."""
        self.client._ready = False
        self.assertEqual((await self.client.set_led("red", "on"))["status"], "disconnected")
        self.client._ready = True
        first = await self.submit()
        self.assertEqual((await self.client.set_led("green", "off"))["status"], "busy")
        self.assertEqual(self.client.get_distance()["status"], "no_data")
        self.client._process_command(self.network)
        self.acknowledge()
        await first
        self.assertEqual(self.network.publish.call_count, 1)

    async def test_expired_before_send(self) -> None:
        """Never publish a queued command after its deadline."""
        task = await self.submit()
        self.now += 3
        self.client._process_command(self.network)
        self.assertEqual((await task)["status"], "not_sent")
        self.network.publish.assert_not_called()
        self.assertTrue(self.client._ready)

    async def test_timeout_after_send_and_late_ack(self) -> None:
        """Retire an uncertain session and ignore acknowledgements after the result."""
        task = await self.submit()
        self.client._process_command(self.network)
        command = self.client._pending
        self.now += 3
        self.client._process_command(self.network)
        self.assertEqual((await task)["status"], "unknown")
        self.acknowledge()
        self.assertEqual(command.result["status"], "unknown")
        self.assertTrue(self.client._retire)
        self.assertFalse(self.client._ready)
        self.assertEqual((await self.client.set_led("green", "off"))["status"], "disconnected")

    async def test_ack_at_deadline_is_uncertain(self) -> None:
        """A PUBACK cannot turn an expired command into success."""
        task = await self.submit()
        self.client._process_command(self.network)
        self.now += 3
        self.acknowledge()
        self.assertEqual((await task)["status"], "unknown")

    async def test_wrong_ack_and_duplicate_ack(self) -> None:
        """Only the pending session and packet can complete a command."""
        task = await self.submit()
        self.client._process_command(self.network)
        self.acknowledge(99)
        self.client._on_publish(Mock(), None, 23, self.success, None)
        self.assertIsNotNone(self.client._pending)
        self.acknowledge()
        self.acknowledge()
        self.assertEqual((await task)["status"], "published")

    async def test_disconnect_before_and_after_send(self) -> None:
        """Distinguish unsent from potentially delivered commands."""
        for sent in (False, True):
            self.client._ready = True
            self.client._retire = False
            task = await self.submit()
            if sent:
                self.client._process_command(self.network)
            self.client._invalidate_connection(self.network)
            expected = "unknown" if sent else "not_sent"
            self.assertEqual((await task)["status"], expected)

    async def test_cancel_before_and_after_send(self) -> None:
        """Cancellation never leaves an admissible command waiting to be sent."""
        for sent in (False, True):
            self.client._ready = True
            self.client._retire = False
            task = await self.submit()
            command = self.client._pending
            if sent:
                self.client._process_command(self.network)
            task.cancel()
            with self.assertRaises(asyncio.CancelledError):
                await task
            self.client._process_command(self.network)
            self.assertEqual(command.result["status"], "unknown" if sent else "not_sent")
        self.assertEqual(self.network.publish.call_count, 1)

    async def test_local_publish_error_is_uncertain(self) -> None:
        """A failure after attempting publish is not proof of nondelivery."""
        self.network.publish.return_value.rc = module.mqtt.MQTT_ERR_NO_CONN
        task = await self.submit()
        self.client._process_command(self.network)
        self.assertEqual((await task)["status"], "unknown")
        self.assertTrue(self.client._retire)

    async def test_wait_timeout_without_worker_progress(self) -> None:
        """The tool timeout does not depend on the network worker advancing."""
        with patch.object(module, "COMMAND_TIMEOUT_SECONDS", 0.02):
            result = await asyncio.wait_for(self.client.set_led("red", "on"), timeout=1)
        self.assertEqual(result["status"], "not_sent")
        self.assertIsNone(self.client._pending)

    async def test_stop_completes_pending_and_is_idempotent(self) -> None:
        """Resolve pending work and reject all admissions after shutdown."""
        task = await self.submit()
        self.client.stop()
        self.client.stop()
        self.assertEqual((await task)["status"], "not_sent")
        self.assertEqual((await self.client.set_led("red", "on"))["status"], "disconnected")


class WorkerTests(unittest.TestCase):
    """Verify lifecycle and reconnect behavior without contacting a broker."""

    def test_real_paho_configuration(self) -> None:
        """Create distinct clean-session clients with the selected callback API."""
        bridge = MqttClient()
        first = bridge._create_client()
        second = bridge._create_client()
        self.assertIsNot(first, second)
        self.assertEqual(first.connect_timeout, 5)
        self.assertEqual(first.callback_api_version, module.mqtt.CallbackAPIVersion.VERSION2)
        self.assertTrue(bridge._client_id.startswith("sis234/grupo2/mcp/"))
        self.assertNotEqual(bridge._client_id, MqttClient()._client_id)

    def test_start_once_and_bounded_dns_shutdown(self) -> None:
        """A stalled connect cannot hold the MCP shutdown indefinitely."""
        bridge = MqttClient()
        entered = threading.Event()
        release = threading.Event()
        network = Mock()

        def connect(*args: object) -> int:
            entered.set()
            release.wait(2)
            return module.mqtt.MQTT_ERR_SUCCESS

        network.connect.side_effect = connect
        with patch.object(bridge, "_create_client", return_value=network) as factory:
            try:
                bridge.start()
                bridge.start()
                self.assertTrue(entered.wait(1))
                with patch.object(module, "SHUTDOWN_TIMEOUT_SECONDS", 0.02):
                    bridge.stop()
                self.assertTrue(bridge._thread.daemon)
                self.assertTrue(bridge._thread.is_alive())
                factory.assert_called_once()
            finally:
                release.set()
                bridge._thread.join(2)
        self.assertFalse(bridge._thread.is_alive())

    def test_failed_session_is_replaced_and_closed_before_disconnect(self) -> None:
        """Retries use a fresh client and never flush an expired output queue."""
        bridge = MqttClient()
        first = Mock()
        second = Mock()
        first.connect.return_value = module.mqtt.MQTT_ERR_SUCCESS
        first.loop.return_value = module.mqtt.MQTT_ERR_CONN_LOST

        def finish(*args: object) -> int:
            bridge._stop_event.set()
            return module.mqtt.MQTT_ERR_SUCCESS

        second.connect.side_effect = finish
        with patch.object(bridge, "_create_client", side_effect=[first, second]) as factory:
            with patch.object(module, "RECONNECT_INTERVAL_SECONDS", 0):
                bridge._run()
        self.assertEqual(factory.call_count, 2)
        first.connect.assert_called_once()
        second.connect.assert_called_once()
        calls = [str(call) for call in first.mock_calls]
        self.assertLess(calls.index("call.socket().close()"), calls.index("call.disconnect()"))
        self.assertIsNone(bridge._client)
        self.assertFalse(bridge._ready)

    def test_connack_and_suback_timeout(self) -> None:
        """Both handshake phases have independent deadlines."""
        for connack in (False, True):
            with self.subTest(connack=connack):
                bridge = MqttClient()
                network = Mock()
                network.connect.return_value = module.mqtt.MQTT_ERR_SUCCESS
                network.subscribe.return_value = (module.mqtt.MQTT_ERR_SUCCESS, 17)
                now = [100.0]

                def loop(timeout: float) -> int:
                    if connack:
                        bridge._on_connect(network, None, None, Mock(is_failure=False), None)
                    now[0] += 6
                    return module.mqtt.MQTT_ERR_SUCCESS

                network.loop.side_effect = loop
                network.disconnect.side_effect = bridge._stop_event.set
                with patch.object(bridge, "_create_client", return_value=network):
                    with patch.object(module, "time") as clock:
                        clock.monotonic.side_effect = lambda: now[0]
                        bridge._run()
                self.assertEqual(network.loop.call_count, 1)
                self.assertFalse(bridge._ready)


if __name__ == "__main__":
    unittest.main()
