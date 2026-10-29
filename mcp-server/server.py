"""Expose distance and LED tools over MCP stdio, using MQTT exclusively."""

import asyncio
from collections.abc import AsyncIterator
from contextlib import asynccontextmanager
import logging
import sys
from typing import Literal

from mcp.server.fastmcp import Context, FastMCP

from mqtt_client import MqttClient


@asynccontextmanager
async def lifespan(server: FastMCP) -> AsyncIterator[MqttClient]:
    """Start MQTT without waiting for connectivity and bound its shutdown."""
    client = MqttClient()
    client.start()
    try:
        yield client
    finally:
        await asyncio.to_thread(client.stop)


server = FastMCP("MQTT MCP Server", lifespan=lifespan)


@server.tool()
def get_distance(ctx: Context) -> dict[str, str | float | bool | None]:
    """Return the latest distance in centimeters and its age.

    Report missing, stale, or disconnected data without waiting for a new
    measurement. A stale value does not describe the current distance.
    """
    client: MqttClient = ctx.request_context.lifespan_context
    return client.get_distance()


@server.tool()
async def set_led(
    led: Literal["red", "yellow", "green"],
    state: Literal["on", "off"],
    ctx: Context,
) -> dict[str, str | bool]:
    """Publish an explicit on/off command for one LED through MQTT.

    Broker acknowledgement does not confirm the physical LED state. Change
    several LEDs with sequential calls, waiting for each result. Stop on an
    uncertain result and do not automatically retry it. Report partial results.
    """
    client: MqttClient = ctx.request_context.lifespan_context
    return await client.set_led(led, state)


def main() -> None:
    """Serve MCP requests until the client disconnects."""
    logging.basicConfig(
        level=logging.INFO,
        handlers=[logging.StreamHandler(sys.stderr)],
        format="%(levelname)s %(name)s: %(message)s",
    )
    server.run(transport="stdio")


if __name__ == "__main__":
    main()
