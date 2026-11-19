#!/usr/bin/env python3
"""Push this PC's current Wi-Fi (SSID + saved key) and MQTT_HOST to INT4089-ESP32 over BLE.

Never prints the Wi-Fi password.
"""
from __future__ import annotations

import asyncio
import re
import subprocess
import sys
from typing import Optional

NUS_RX = "6e400002-b5a3-f393-e0a9-e50e24dcca9e"
NUS_TX = "6e400003-b5a3-f393-e0a9-e50e24dcca9e"
BLE_PREFIX = "INT4089"


def run(cmd: list[str]) -> str:
    return subprocess.check_output(cmd, encoding="utf-8", errors="replace")


def current_ssid() -> str:
    out = run(["netsh", "wlan", "show", "interfaces"])
    m = re.search(r"^\s*SSID\s*:\s*(.+)\s*$", out, re.M)
    if not m:
        raise RuntimeError("找不到而家連住嘅 Wi-Fi SSID")
    ssid = m.group(1).strip()
    if not ssid:
        raise RuntimeError("Wi-Fi 未連線")
    return ssid


def profile_password(ssid: str) -> str:
    # Do not print this output; it contains the key.
    out = run(["netsh", "wlan", "show", "profile", f"name={ssid}", "key=clear"])
    m = re.search(r"^\s*Key Content\s*:\s*(.*)\s*$", out, re.M | re.I)
    if not m:
        # Some Windows locales use 關鍵內容 / 密钥内容
        m = re.search(r"^\s*(Key Content|關鍵內容|关键内容)\s*:\s*(.*)\s*$", out, re.M | re.I)
        if not m:
            raise RuntimeError(
                f"讀唔到「{ssid}」嘅 Wi-Fi 密碼（企業網或未保存金鑰）。請改用 dashboard 手動填密碼。"
            )
        return m.group(2)
    return m.group(1)


def wifi_ipv4() -> str:
    # Prefer Get-NetIPAddress; ipconfig splits the adapter title and IPv4
    # into different blank-line blocks, which a naive parse can miss.
    try:
        out = run([
            "powershell",
            "-NoProfile",
            "-Command",
            "(Get-NetIPAddress -AddressFamily IPv4 -InterfaceAlias 'Wi-Fi' -ErrorAction SilentlyContinue "
            "| Where-Object { $_.IPAddress -notlike '127.*' -and $_.IPAddress -notlike '169.254.*' } "
            "| Select-Object -First 1 -ExpandProperty IPAddress)",
        ])
        found = out.strip().splitlines()[-1].strip() if out.strip() else ""
        if re.fullmatch(r"\d+\.\d+\.\d+\.\d+", found) and found not in ("127.0.0.1", "0.0.0.0"):
            return found
    except Exception:
        pass

    ipcfg = run(["ipconfig"])
    found = None
    in_wifi = False
    disconnected = False
    ip = None

    def flush() -> None:
        nonlocal found, ip, disconnected
        if in_wifi and ip and not disconnected:
            found = ip

    for line in ipcfg.splitlines():
        stripped = line.strip()
        is_header = bool(stripped.endswith(":")) and not line.startswith((" ", "\t"))
        if is_header:
            flush()
            in_wifi = bool(re.search(r"(Wireless LAN adapter|無線區域網路介面卡)\s+Wi-Fi\s*:", stripped, re.I))
            disconnected = False
            ip = None
            continue
        if not in_wifi:
            continue
        if re.search(r"Media disconnected|媒體已中斷", line, re.I):
            disconnected = True
        m = re.search(r"(IPv4|位址|地址)[^\d]*(\d+\.\d+\.\d+\.\d+)", line, re.I)
        if m:
            ip = m.group(2)
    flush()
    if not found:
        raise RuntimeError("找不到電腦 Wi-Fi IPv4")
    if found in ("127.0.0.1", "0.0.0.0"):
        raise RuntimeError(f"唔能用呢個 IP 做 MQTT_HOST: {found}")
    return found


async def sync(ssid: str, password: str, mqtt_host: str, addr: Optional[str] = None) -> None:
    from bleak import BleakClient, BleakScanner

    print(f"Scanning for {BLE_PREFIX}-ESP32 ...")
    target = addr
    if not target:
        devices = await BleakScanner.discover(timeout=12.0, return_adv=True)
        for a, (dev, adv) in devices.items():
            name = dev.name or getattr(adv, "local_name", None) or ""
            if name.upper().startswith(BLE_PREFIX):
                target = a
                print(f"Found {name} at {a}")
                break
    if not target:
        raise RuntimeError("掃唔到 INT4089-ESP32。請確認已上載新程式而且藍牙已開。")

    replies: list[str] = []

    def on_notify(_s, data: bytes) -> None:
        text = data.decode("utf-8", errors="replace")
        for line in re.split(r"[\r\n]+", text):
            line = line.strip()
            if not line:
                continue
            # never print raw password if a bug echoed it
            low = line.lower()
            if "wifi_pass=" in low and "****" not in line:
                line = line.split("=", 1)[0] + "=****"
            print("ESP32:", line)
            replies.append(line)

    async with BleakClient(target, timeout=20.0) as client:
        print("Bluetooth connected")
        await client.start_notify(NUS_TX, on_notify)
        await asyncio.sleep(0.6)

        async def write(msg: str) -> None:
            payload = (msg if msg.endswith("\n") else msg + "\n").encode("utf-8")
            try:
                await client.write_gatt_char(NUS_RX, payload, response=False)
            except Exception:
                await client.write_gatt_char(NUS_RX, payload, response=True)

        await write("GET")
        await asyncio.sleep(0.4)
        print(f"Sending WIFI_SSID={ssid}")
        await write("WIFI_SSID=" + ssid)
        await asyncio.sleep(0.3)
        print("Sending WIFI_PASS=****")
        await write("WIFI_PASS=" + password)
        await asyncio.sleep(0.3)
        print(f"Sending MQTT_HOST={mqtt_host}")
        await write("MQTT_HOST=" + mqtt_host)
        # wait for join result (ESP32 may take up to ~20s)
        print("Waiting for ESP32 to join Wi-Fi ...")
        await asyncio.sleep(22)

    joined = any(x.startswith("OK WIFI_JOIN=") or x.startswith("ESP32_IP=") for x in replies)
    failed = any(x.startswith("ERR WIFI_JOIN=") for x in replies)
    if failed:
        raise RuntimeError("ESP32 加入 Wi-Fi 失敗，請檢查名稱同密碼。")
    if not joined:
        print("No join confirmation in time; check Serial or retry.")
    else:
        print("Done.")


def main() -> int:
    if sys.platform == "win32":
        try:
            sys.stdout.reconfigure(encoding="utf-8")
            sys.stderr.reconfigure(encoding="utf-8")
        except Exception:
            pass
    try:
        ssid = current_ssid()
        mqtt_host = wifi_ipv4()
        print(f"PC Wi-Fi SSID: {ssid}")
        print(f"PC Wi-Fi IPv4 (MQTT_HOST): {mqtt_host}")
        password = profile_password(ssid)
        asyncio.run(sync(ssid, password, mqtt_host))
        return 0
    except Exception as ex:
        print(f"ERROR: {ex}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
