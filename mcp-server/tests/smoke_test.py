"""Start a real server process and exercise MCP over standard input/output."""

import asyncio
import json
import sys
from pathlib import Path

from mcp import ClientSession, StdioServerParameters
from mcp.client.stdio import stdio_client


async def check_server() -> None:
    parameters = StdioServerParameters(
        command=sys.executable,
        args=[str(Path(__file__).resolve().parents[1] / "server.py")],
    )
    async with stdio_client(parameters) as (read, write):
        async with ClientSession(read, write) as session:
            await session.initialize()
            tools = await session.list_tools()
            by_name = {tool.name: tool for tool in tools.tools}
            assert set(by_name) == {"get_distance", "set_led"}
            distance_schema = by_name["get_distance"].inputSchema
            assert not distance_schema.get("properties")
            assert not distance_schema.get("required")
            led_schema = by_name["set_led"].inputSchema
            assert set(led_schema["properties"]) == {"led", "state"}
            assert set(led_schema["required"]) == {"led", "state"}
            assert led_schema["properties"]["led"]["enum"] == ["red", "yellow", "green"]
            assert led_schema["properties"]["state"]["enum"] == ["on", "off"]
            result = await session.call_tool("get_distance", {})
            assert not result.isError, result
            payload = json.loads(result.content[0].text)
            assert set(payload) == {"status", "cm", "age_seconds", "mqtt_connected", "message"}
            assert payload["status"] in {"ok", "stale", "no_data", "disconnected"}
            if payload["status"] in {"ok", "stale"}:
                assert 5 <= payload["cm"] <= 200
                assert payload["age_seconds"] >= 0
            else:
                assert payload["cm"] is None and payload["age_seconds"] is None
            for arguments in (
                {"led": "blue", "state": "on"},
                {"led": "red", "state": "ON"},
                {"led": "red", "state": True},
                {"led": "red"},
            ):
                rejected = await session.call_tool("set_led", arguments)
                assert rejected.isError, rejected
            print("PASS: MCP initialization, tool schemas, distance and invalid arguments")
            print(json.dumps(payload))


async def main() -> None:
    await asyncio.wait_for(check_server(), timeout=30)


if __name__ == "__main__":
    asyncio.run(main())
