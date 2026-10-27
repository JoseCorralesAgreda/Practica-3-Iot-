# MCP server

Minimal Python server using the official [MCP Python SDK v1](https://py.sdk.modelcontextprotocol.io/v1/) and stdio transport. Requires Python 3.11 or newer. Dependencies are pinned in `uv.lock`.

## Install and run

From this folder:

```powershell
uv sync --locked
uv run python -m mqtt_mcp_server
```

The server waits for MCP input; an idle terminal is expected. Use an MCP client to interact with it, or Ctrl+C to stop it. The installed `mqtt-mcp-server` command is an alternative entry point.

## Verify

```powershell
uv run python tests/smoke_test.py
```

The test launches a real server subprocess, initializes a session, discovers tools, calls `get_status`, validates its response, and closes the subprocess. It has a 30-second timeout.

## Tool contract

`get_status` takes no arguments and returns:

```json
{
  "status": "ok",
  "service": "mqtt-mcp-server",
  "mqtt_connected": false,
  "hardware_connected": false
}
```

This reports server availability only; it does not communicate with hardware.

## Layout

- `src/mqtt_mcp_server/server.py`: MCP tool and entry point.
- `src/mqtt_mcp_server/__main__.py`: module entry point.
- `src/mqtt_mcp_server/mqtt_client.py`: reserved for MQTT.
- `src/mqtt_mcp_server/settings.py`: reserved for configuration loading.
- `src/mqtt_mcp_server/tools/`: future sensor and actuator tools.
- `tests/smoke_test.py`: integration check.
- `config/`: future AI client configuration.

`.env.example` is reserved for future MQTT integration and is not loaded yet. Local `.env` and `.venv/` are ignored by Git.
