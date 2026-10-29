"""Keep one distance sample and publish bounded LED commands through MQTT."""

import asyncio
from dataclasses import dataclass, field
import json
import logging
import math
import threading
import time
import uuid

import paho.mqtt.client as mqtt
from paho.mqtt.properties import Properties
from paho.mqtt.reasoncodes import ReasonCode

BROKER_HOST = "broker.hivemq.com"
BROKER_PORT = 1883
DISTANCE_TOPIC = "sis234/grupo2/distance"
COMMAND_TOPIC = "sis234/grupo2/leds/cmd"
MAX_READING_AGE_SECONDS = 3.0
MIN_DISTANCE_CM = 5.0
MAX_DISTANCE_CM = 200.0
MAX_PAYLOAD_BYTES = 128
RECONNECT_INTERVAL_SECONDS = 5.0
CONNECT_TIMEOUT_SECONDS = 5.0
HANDSHAKE_TIMEOUT_SECONDS = 5.0
KEEPALIVE_SECONDS = 10
NETWORK_LOOP_TIMEOUT_SECONDS = 0.1
COMMAND_TIMEOUT_SECONDS = 3.0
SHUTDOWN_TIMEOUT_SECONDS = 2.0
INVALID_LOG_INTERVAL_SECONDS = 1.0

logger = logging.getLogger(__name__)


@dataclass
class _PendingCommand:
    """Track one command until its first terminal result."""

    led: str
    state: str
    deadline: float
    generation: int
    attempted: bool = False
    mid: int | None = None
    result: dict[str, str | bool] | None = None
    done: threading.Event = field(default_factory=threading.Event)


def _unique_object(pairs: list[tuple[str, object]]) -> dict[str, object]:
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError("Duplicate JSON key")
        result[key] = value
    return result


def _command_result(led: str, state: str, status: str) -> dict[str, str | bool]:
    messages = {
        "published": "Broker acknowledged the command; physical LED state is unconfirmed.",
        "disconnected": "MQTT is not ready; no command was accepted.",
        "busy": "Another command is pending; no new command was accepted.",
        "not_sent": "The command expired or was cancelled before sending.",
        "unknown": (
            "Delivery is uncertain. The command may have reached the device; "
            "do not automatically retry."
        ),
    }
    return {
        "status": status,
        "led": led,
        "state": state,
        "broker_acknowledged": status == "published",
        "device_confirmed": False,
        "message": messages[status],
    }


class MqttClient:
    """Own one MQTT worker, a recent-reading cache and one pending command."""

    def __init__(self) -> None:
        """Prepare local state without connecting or starting a thread."""
        self._client_id = f"sis234/grupo2/mcp/{uuid.uuid4()}"
        self._lock = threading.Lock()
        self._stop_event = threading.Event()
        self._thread: threading.Thread | None = None
        self._client: mqtt.Client | None = None
        self._ready = False
        self._retire = False
        self._generation = 0
        self._subscription_mid: int | None = None
        self._phase_deadline = 0.0
        self._distance_cm: float | None = None
        self._received_at: float | None = None
        self._pending: _PendingCommand | None = None
        self._invalid_count = 0
        self._last_invalid_log = 0.0

    def start(self) -> None:
        """Start the single daemon worker once, without waiting for a broker."""
        with self._lock:
            if self._thread is not None or self._stop_event.is_set():
                return
            self._thread = threading.Thread(
                target=self._run, name="mqtt-worker", daemon=True
            )
            self._thread.start()

    def stop(self) -> None:
        """Reject new commands and wait at most two seconds for shutdown."""
        with self._lock:
            self._stop_event.set()
            self._invalidate_locked()
            worker = self._thread
        if worker is not None:
            worker.join(timeout=SHUTDOWN_TIMEOUT_SECONDS)
            if worker.is_alive():
                logger.warning("MQTT worker shutdown deadline reached")

    def get_distance(self) -> dict[str, str | float | bool | None]:
        """Return the latest sample and age without waiting for new data."""
        with self._lock:
            ready = self._ready
            distance = self._distance_cm
            received_at = self._received_at
            now = time.monotonic()
        age = None
        if not ready:
            status = "disconnected"
            distance = None
            message = "MQTT is not ready; current distance is unavailable."
        elif received_at is None:
            status = "no_data"
            message = "No valid measurement has arrived yet."
        else:
            age = max(0.0, now - received_at)
            if age > MAX_READING_AGE_SECONDS:
                status = "stale"
                message = (
                    "The last measurement is stale; it does not describe the current distance."
                )
            else:
                status = "ok"
                message = "Recent distance measurement."
        return {
            "status": status,
            "cm": distance,
            "age_seconds": None if age is None else round(age, 3),
            "mqtt_connected": ready,
            "message": message,
        }

    async def set_led(self, led: str, state: str) -> dict[str, str | bool]:
        """Submit one explicit LED state and wait for a bounded broker result."""
        if led not in ("red", "yellow", "green") or state not in ("on", "off"):
            raise ValueError("Expected led red/yellow/green and state on/off")
        with self._lock:
            if not self._ready or self._stop_event.is_set():
                return _command_result(led, state, "disconnected")
            if self._pending is not None:
                return _command_result(led, state, "busy")
            command = _PendingCommand(
                led, state, time.monotonic() + COMMAND_TIMEOUT_SECONDS, self._generation
            )
            self._pending = command
        try:
            remaining = max(0.0, command.deadline - time.monotonic())
            await asyncio.to_thread(command.done.wait, remaining)
        except asyncio.CancelledError:
            with self._lock:
                self._expire_locked(command)
                uncertain = command.result["status"] == "unknown"
            if uncertain:
                logger.warning("Cancelled command has uncertain delivery")
            raise
        with self._lock:
            self._expire_locked(command)
            return dict(command.result)

    def _finish_locked(self, command: _PendingCommand, status: str) -> None:
        if command.result is not None:
            return
        command.result = _command_result(command.led, command.state, status)
        if self._pending is command:
            self._pending = None
        command.done.set()

    def _expire_locked(self, command: _PendingCommand) -> None:
        if command.result is not None:
            return
        if command.attempted:
            self._invalidate_locked()
        else:
            self._finish_locked(command, "not_sent")

    def _invalidate_locked(self) -> None:
        self._ready = False
        self._retire = True
        self._distance_cm = None
        self._received_at = None
        self._subscription_mid = None
        self._generation += 1
        if self._pending is not None:
            status = "unknown" if self._pending.attempted else "not_sent"
            self._finish_locked(self._pending, status)

    def _invalidate_connection(self, client: mqtt.Client) -> None:
        with self._lock:
            if client is self._client:
                self._invalidate_locked()

    def _create_client(self) -> mqtt.Client:
        client = mqtt.Client(
            callback_api_version=mqtt.CallbackAPIVersion.VERSION2,
            client_id=self._client_id,
            clean_session=True,
            protocol=mqtt.MQTTv311,
            transport="tcp",
            reconnect_on_failure=False,
        )
        client.connect_timeout = CONNECT_TIMEOUT_SECONDS
        client.max_inflight_messages_set(1)
        client.max_queued_messages_set(1)
        client.on_connect = self._on_connect
        client.on_subscribe = self._on_subscribe
        client.on_disconnect = self._on_disconnect
        client.on_message = self._on_message
        client.on_publish = self._on_publish
        return client

    def _run(self) -> None:
        while not self._stop_event.is_set():
            attempt_started = time.monotonic()
            client = self._create_client()
            with self._lock:
                if self._stop_event.is_set():
                    return
                self._client = client
                self._retire = False
                self._generation += 1
            try:
                result = client.connect(BROKER_HOST, BROKER_PORT, KEEPALIVE_SECONDS)
                if result != mqtt.MQTT_ERR_SUCCESS:
                    raise OSError("MQTT connection request failed")
                with self._lock:
                    self._phase_deadline = time.monotonic() + HANDSHAKE_TIMEOUT_SECONDS
                while not self._stop_event.is_set():
                    with self._lock:
                        if self._retire:
                            break
                        if not self._ready and time.monotonic() >= self._phase_deadline:
                            self._invalidate_locked()
                            break
                    self._process_command(client)
                    with self._lock:
                        if self._retire:
                            break
                    if client.loop(timeout=NETWORK_LOOP_TIMEOUT_SECONDS) != mqtt.MQTT_ERR_SUCCESS:
                        break
            except (OSError, ValueError) as error:
                logger.warning("MQTT connection failed: %s", error)
            except Exception:
                logger.exception("Unexpected MQTT worker failure")
            finally:
                self._invalidate_connection(client)
                try:
                    # disconnect() may flush Paho's packet queue. Close the
                    # public socket first so expired commands cannot be flushed.
                    connection = client.socket()
                    if connection is not None:
                        connection.close()
                    client.disconnect()
                except Exception:
                    logger.exception("MQTT client retirement failed")
                finally:
                    # Never reconnect this instance: Paho retains outgoing QoS 1 data.
                    with self._lock:
                        if self._client is client:
                            self._client = None
            remaining = RECONNECT_INTERVAL_SECONDS - (time.monotonic() - attempt_started)
            self._stop_event.wait(max(0.0, remaining))

    def _on_connect(
        self, client: mqtt.Client, userdata: object, flags: mqtt.ConnectFlags,
        reason_code: ReasonCode, properties: Properties | None,
    ) -> None:
        with self._lock:
            if client is not self._client or self._retire or self._stop_event.is_set():
                return
            if time.monotonic() >= self._phase_deadline:
                self._invalidate_locked()
                return
        if reason_code.is_failure:
            self._invalidate_connection(client)
            return
        result, mid = client.subscribe(DISTANCE_TOPIC, qos=0)
        with self._lock:
            if client is not self._client or self._retire:
                return
            if result != mqtt.MQTT_ERR_SUCCESS:
                self._invalidate_locked()
            else:
                self._subscription_mid = mid
                self._phase_deadline = time.monotonic() + HANDSHAKE_TIMEOUT_SECONDS

    def _on_subscribe(
        self, client: mqtt.Client, userdata: object, mid: int,
        reason_code_list: list[ReasonCode], properties: Properties | None,
    ) -> None:
        with self._lock:
            if client is not self._client or self._retire or mid != self._subscription_mid:
                return
            if (time.monotonic() >= self._phase_deadline
                    or len(reason_code_list) != 1 or reason_code_list[0] != 0):
                self._invalidate_locked()
                return
            self._ready = True
        logger.info("MQTT distance subscription ready")

    def _on_disconnect(
        self, client: mqtt.Client, userdata: object, disconnect_flags: mqtt.DisconnectFlags,
        reason_code: ReasonCode, properties: Properties | None,
    ) -> None:
        self._invalidate_connection(client)
        logger.info("MQTT disconnected")

    @staticmethod
    def _parse_distance(payload: bytes) -> float:
        if len(payload) > MAX_PAYLOAD_BYTES:
            raise ValueError("Distance payload is too large")
        data = json.loads(payload.decode("utf-8"), object_pairs_hook=_unique_object)
        if not isinstance(data, dict) or set(data) != {"cm"}:
            raise ValueError("Expected only the cm field")
        value = data["cm"]
        if isinstance(value, bool) or not isinstance(value, (int, float)):
            raise ValueError("Distance must be numeric")
        if not math.isfinite(value) or not MIN_DISTANCE_CM <= value <= MAX_DISTANCE_CM:
            raise ValueError("Distance is outside the working range")
        return float(value)

    def _on_message(
        self, client: mqtt.Client, userdata: object, message: mqtt.MQTTMessage
    ) -> None:
        with self._lock:
            if client is not self._client or not self._ready:
                return
            generation = self._generation
        try:
            if message.topic != DISTANCE_TOPIC or message.retain:
                raise ValueError("Unexpected or retained message")
            distance = self._parse_distance(message.payload)
        except (ValueError, UnicodeError, RecursionError):
            self._invalid_count += 1
            now = time.monotonic()
            if now - self._last_invalid_log >= INVALID_LOG_INTERVAL_SECONDS:
                logger.warning("Discarded MQTT samples: %d", self._invalid_count)
                self._last_invalid_log = now
                self._invalid_count = 0
            return
        with self._lock:
            if client is self._client and self._ready and generation == self._generation:
                self._distance_cm = distance
                self._received_at = time.monotonic()

    def _process_command(self, client: mqtt.Client) -> None:
        with self._lock:
            command = self._pending
            if command is None or client is not self._client or not self._ready:
                return
            if time.monotonic() >= command.deadline:
                self._expire_locked(command)
                return
            if command.generation != self._generation:
                self._finish_locked(command, "not_sent")
                return
            if command.attempted:
                return
            command.attempted = True
        payload = json.dumps(
            {"led": command.led, "state": command.state}, separators=(",", ":")
        )
        info = client.publish(COMMAND_TOPIC, payload, qos=1, retain=False)
        with self._lock:
            if command.result is not None:
                return
            if info.rc != mqtt.MQTT_ERR_SUCCESS:
                self._invalidate_locked()
            else:
                command.mid = info.mid

    def _on_publish(
        self, client: mqtt.Client, userdata: object, mid: int,
        reason_code: ReasonCode, properties: Properties | None,
    ) -> None:
        with self._lock:
            command = self._pending
            if (client is not self._client or not self._ready or command is None
                    or command.generation != self._generation or command.mid != mid):
                return
            if time.monotonic() >= command.deadline or reason_code.is_failure:
                self._invalidate_locked()
            else:
                self._finish_locked(command, "published")
