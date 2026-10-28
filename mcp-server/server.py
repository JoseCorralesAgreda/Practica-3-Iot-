"""Minimal MCP server using the standard input/output transport."""

from mcp.server.fastmcp import FastMCP

server = FastMCP("MQTT MCP Server")


@server.tool()
def get_status() -> dict[str, str | bool]:
    """Return this server's status; no hardware connection is configured yet."""
    return {
        "status": "ok",
        "service": "mqtt-mcp-server",
        "mqtt_connected": False,
        "hardware_connected": False,
    }


def main() -> None:
    """Serve MCP requests until the client disconnects."""
    server.run(transport="stdio")


if __name__ == "__main__":
    main()
