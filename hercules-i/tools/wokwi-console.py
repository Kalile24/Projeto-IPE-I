#!/usr/bin/env python3
"""Console Telnet simples para a serial exposta pelo Wokwi VS Code.

O `wokwi.toml` habilita `rfc2217ServerPort = 4000`. A extensao aceita
clientes Telnet nessa porta; este script faz a negociacao minima e deixa o
uso parecido com um monitor serial normal.
"""

from __future__ import annotations

import argparse
import select
import socket
import sys
import termios
import threading
import time
import tty


HOST = "127.0.0.1"
PORT = 4000
CONNECT_TIMEOUT_S = 3
READ_TIMEOUT_S = 0.1

IAC = 255
WILL = 251
WONT = 252
DO = 253
DONT = 254
SB = 250
SE = 240


def reply_negotiation(sock: socket.socket, command: int, option: int) -> None:
    if command == DO:
        sock.sendall(bytes([IAC, WONT, option]))
    elif command == WILL:
        sock.sendall(bytes([IAC, DONT, option]))


def filter_telnet(sock: socket.socket, data: bytes) -> bytes:
    out = bytearray()
    i = 0
    while i < len(data):
        byte = data[i]
        if byte != IAC:
            out.append(byte)
            i += 1
            continue

        if i + 1 >= len(data):
            break

        command = data[i + 1]
        if command in (DO, DONT, WILL, WONT):
            if i + 2 < len(data):
                reply_negotiation(sock, command, data[i + 2])
            i += 3
        elif command == SB:
            i += 2
            while i + 1 < len(data) and not (data[i] == IAC and data[i + 1] == SE):
                i += 1
            i += 2
        elif command == IAC:
            out.append(IAC)
            i += 2
        else:
            i += 2
    return bytes(out)


def read_loop(sock: socket.socket, stop: threading.Event) -> None:
    last_rx = time.monotonic()
    while not stop.is_set():
        readable, _, _ = select.select([sock], [], [], READ_TIMEOUT_S)
        if not readable:
            continue
        try:
            data = sock.recv(4096)
        except OSError:
            stop.set()
            return
        if not data:
            print("\n[console] Conexao fechada pelo Wokwi.", file=sys.stderr)
            stop.set()
            return
        payload = filter_telnet(sock, data)
        if payload:
            last_rx = time.monotonic()
            sys.stdout.buffer.write(payload)
            sys.stdout.buffer.flush()


def send_line(sock: socket.socket, line: str) -> None:
    sock.sendall(line.rstrip("\r\n").encode("utf-8") + b"\n")


def run_interactive(sock: socket.socket) -> int:
    stop = threading.Event()
    reader = threading.Thread(target=read_loop, args=(sock, stop), daemon=True)
    reader.start()

    old_tty = termios.tcgetattr(sys.stdin)
    try:
        tty.setcbreak(sys.stdin.fileno())
        print("Conectado em 127.0.0.1:4000.")
        print("Digite STATUS e pressione Enter. Use Ctrl+C para sair.\n")
        while not stop.is_set():
            readable, _, _ = select.select([sys.stdin], [], [], 0.1)
            if not readable:
                continue
            ch = sys.stdin.read(1)
            if ch == "\x03":
                print("\n[console] Saindo.")
                break
            sock.sendall(ch.encode("utf-8"))
    finally:
        stop.set()
        termios.tcsetattr(sys.stdin, termios.TCSADRAIN, old_tty)
        sock.close()

    return 0


def run_command(sock: socket.socket, command: str) -> int:
    stop = threading.Event()
    reader = threading.Thread(target=read_loop, args=(sock, stop), daemon=True)
    reader.start()
    time.sleep(0.2)
    send_line(sock, command)
    time.sleep(2)
    stop.set()
    sock.close()
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="Console serial para Wokwi VS Code")
    parser.add_argument("--cmd", help="envia um comando e sai, exemplo: --cmd STATUS")
    args = parser.parse_args()

    print("[console] Conectando em 127.0.0.1:4000...", flush=True)
    try:
        sock = socket.create_connection((HOST, PORT), timeout=CONNECT_TIMEOUT_S)
    except OSError as exc:
        print(f"[console] Nao conectou: {exc}", file=sys.stderr)
        print("[console] Inicie a simulacao com F1 -> Wokwi: Start Simulator.", file=sys.stderr)
        print("[console] Abra a pasta hercules-i/wokwi no VS Code, pois o wokwi.toml fica la.", file=sys.stderr)
        return 1

    sock.settimeout(None)
    if args.cmd:
        return run_command(sock, args.cmd)
    return run_interactive(sock)


if __name__ == "__main__":
    raise SystemExit(main())
