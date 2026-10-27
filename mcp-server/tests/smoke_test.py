"""Start a real server process and exercise MCP over standard input/output."""

import asyncio
import json
import sys

from mcp import ClientSession, StdioServerParameters
from mcp.client.stdio import stdio_client


async def check_server() -> None:
    parameters = StdioServerParameters(
        command=sys.executable, args=["-m", "mqtt_mcp_server"]
    )
    async with stdio_client(parameters) as (read, write):
        async with ClientSession(read, write) as session:
            await session.initialize()
            tools = await session.list_tools()
            assert "get_status" in {tool.name for tool in tools.tools}
            result = await session.call_tool("get_status", {})
            assert not result.isError, result
            payload = json.loads(result.content[0].text)
            assert payload == {
                "status": "ok",
                "service": "mqtt-mcp-server",
                "mqtt_connected": False,
                "hardware_connected": False,
            }, payload
            print("PASS: MCP initialization, tool discovery, and get_status")
            print(json.dumps(payload))


async def main() -> None:
    await asyncio.wait_for(check_server(), timeout=30)


if __name__ == "__main__":
    asyncio.run(main())
