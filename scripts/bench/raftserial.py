#!/usr/bin/env python3
"""Raft serial client for bench work: REST commands, console logs and file uploads over a serial port.

Talks to a Raft device's SerialConsole on a serial port, which carries two things at once:
- text: log output, and REST commands typed as lines (the reply is printed as a line of JSON);
- protocol frames: RICSerial messages, HDLC framed, then "OverASCII" encoded so every byte has its MSB set
  (RaftCore ProtocolOverAscii), so they can share the port with text.

Usage:
    python raftserial.py <port> cmd "devman/typefile"             REST command (text line), prints the JSON reply
    python raftserial.py <port> cmd @request.txt                  the same, the command read from a file (saves
                                                                  quoting & in a URL for the shell)
    python raftserial.py <port> cmdlog @request.txt 10            send a command, then print the console for 10 s
    python raftserial.py <port> log 10                            print the console for 10 seconds
    python raftserial.py <port> put local.json remote.json        upload a file to the device's file system (RICREST)

The control lines (DTR/RTS) are left alone, as on an ESP32's USB Serial/JTAG they can reset the chip. On Windows
with WSL, run it with the Windows Python (py -3) as WSL can't open COM ports. Requires pyserial. See README.md.
"""

import json
import sys
import threading
import time

import serial

# HDLC (RaftCore MiniHDLC / raftjs RaftMiniHDLC)
HDLC_FLAG = 0xE7
HDLC_ESC = 0xD7
HDLC_XOR = 0x20

# OverASCII (RaftCore ProtocolOverAscii)
OA_ESC_1 = 0x85     # byte 0x00-0x0f
OA_ESC_2 = 0x8E     # byte 0x80-0x8f
OA_ESC_3 = 0x8F     # byte 0x90-0xff
OA_MOD = 0x20

# RICSerial / RICREST (raftjs RaftProtocolDefs)
PROTOCOL_RICREST = 2
MSG_TYPE_COMMAND = 0
MSG_TYPE_RESPONSE = 1
ELEM_URL = 0
ELEM_CMDRESPJSON = 1
ELEM_COMMAND_FRAME = 3
ELEM_FILEBLOCK = 4


def crc16(data):
    """CRC-16/CCITT-FALSE (poly 0x1021, initial 0xffff), as MiniHDLC"""
    crc = 0xFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) if (crc & 0x8000) else (crc << 1)
            crc &= 0xFFFF
    return crc


def hdlc_encode(content):
    crc = crc16(content)
    out = bytearray([HDLC_FLAG])
    for byte in bytes(content) + bytes([crc >> 8, crc & 0xFF]):
        if byte in (HDLC_FLAG, HDLC_ESC):
            out += bytes([HDLC_ESC, byte ^ HDLC_XOR])
        else:
            out.append(byte)
    out.append(HDLC_FLAG)
    return bytes(out)


def overascii_encode(data):
    out = bytearray()
    for byte in data:
        if byte <= 0x0F:
            out += bytes([OA_ESC_1, (byte ^ OA_MOD) | 0x80])
        elif byte <= 0x7F:
            out.append(byte | 0x80)
        elif byte <= 0x8F:
            out += bytes([OA_ESC_2, byte ^ OA_MOD])
        else:
            out += bytes([OA_ESC_3, byte])
    return bytes(out)


class RaftSerial:
    def __init__(self, port):
        self._ser = serial.Serial()
        self._ser.port = port
        self._ser.baudrate = 115200
        self._ser.timeout = 0.05
        # Don't toggle the control lines (on the ESP32 USB Serial/JTAG they can reset the chip)
        self._ser.dtr = False
        self._ser.rts = False
        self._ser.open()
        self._msg_num = 0
        self._lock = threading.Lock()
        self._responses = {}        # msgNum -> JSON
        self._okto = None           # latest okto (file upload acknowledgement)
        self._text_lines = []
        self._text_buf = bytearray()
        self._oa_esc = 0
        self._hdlc_buf = bytearray()
        self._hdlc_esc = False
        self._running = True
        self._thread = threading.Thread(target=self._rx_loop, daemon=True)
        self._thread.start()

    def close(self):
        self._running = False
        self._thread.join(1)
        self._ser.close()

    # Receive: text bytes (MSB clear) and OverASCII protocol bytes (MSB set) are interleaved
    def _rx_loop(self):
        while self._running:
            data = self._ser.read(4096)
            for byte in data:
                if byte < 0x80 and self._oa_esc == 0:
                    self._rx_text(byte)
                else:
                    decoded = self._oa_decode(byte)
                    if decoded is not None:
                        self._rx_hdlc(decoded)

    def _rx_text(self, byte):
        if byte == 0x0A:
            line = self._text_buf.decode("utf-8", "replace").rstrip("\r")
            self._text_buf = bytearray()
            with self._lock:
                self._text_lines.append(line)
        else:
            self._text_buf.append(byte)

    def _oa_decode(self, byte):
        esc = self._oa_esc
        if esc:
            self._oa_esc = 0
            if esc == 1:
                return (byte ^ OA_MOD) & 0x7F
            if esc == 2:
                return byte ^ OA_MOD
            return byte
        if byte == OA_ESC_1:
            self._oa_esc = 1
        elif byte == OA_ESC_2:
            self._oa_esc = 2
        elif byte == OA_ESC_3:
            self._oa_esc = 3
        else:
            return byte & 0x7F
        return None

    def _rx_hdlc(self, byte):
        if byte == HDLC_FLAG:
            frame = bytes(self._hdlc_buf)
            self._hdlc_buf = bytearray()
            self._hdlc_esc = False
            if len(frame) > 2 and crc16(frame[:-2]) == (frame[-2] << 8 | frame[-1]):
                self._rx_msg(frame[:-2])
        elif byte == HDLC_ESC:
            self._hdlc_esc = True
        else:
            self._hdlc_buf.append(byte ^ HDLC_XOR if self._hdlc_esc else byte)
            self._hdlc_esc = False

    def _rx_msg(self, msg):
        if len(msg) < 3 or (msg[1] & 0x3F) != PROTOCOL_RICREST:
            return
        msg_num = msg[0]
        elem = msg[2]
        if elem not in (ELEM_URL, ELEM_CMDRESPJSON, ELEM_COMMAND_FRAME):
            return
        text = msg[3:].rstrip(b"\x00").decode("utf-8", "replace")
        try:
            obj = json.loads(text)
        except ValueError:
            return
        with self._lock:
            if "okto" in obj:
                self._okto = obj["okto"]
            if msg_num:
                self._responses[msg_num] = obj

    # Send
    def _send(self, content):
        self._ser.write(overascii_encode(hdlc_encode(content)))

    def _next_msg_num(self):
        self._msg_num = self._msg_num % 255 + 1
        return self._msg_num

    def ricrest(self, text, elem=ELEM_COMMAND_FRAME, timeout_s=10.0):
        """Send a numbered RICREST message and wait for its response (JSON)"""
        msg_num = self._next_msg_num()
        with self._lock:
            self._responses.pop(msg_num, None)
        self._send(bytes([msg_num, (MSG_TYPE_COMMAND << 6) | PROTOCOL_RICREST, elem]) + text.encode() + b"\x00")
        end = time.time() + timeout_s
        while time.time() < end:
            with self._lock:
                if msg_num in self._responses:
                    return self._responses.pop(msg_num)
            time.sleep(0.01)
        raise TimeoutError(f"no response to {text[:60]}")

    def _send_block(self, stream_id, pos, data):
        header = bytes([0, (MSG_TYPE_COMMAND << 6) | PROTOCOL_RICREST, ELEM_FILEBLOCK,
                        stream_id & 0xFF, (pos >> 16) & 0xFF, (pos >> 8) & 0xFF, pos & 0xFF])
        self._send(header + data)

    def put(self, contents, remote_name, block_size=300, batch_ack=2):
        """Upload a file (RICREST ufStart, blocks acknowledged by okto, ufEnd), as raftjs RaftFileHandler.
        Batches are kept under 1 KB on the wire: before RaftSysMods 1.26.0 SerialConsole's receive buffer was 1024
        bytes and it took 100 bytes per loop, so a larger burst lost its end"""
        start = self.ricrest(json.dumps({"cmdName": "ufStart", "reqStr": "fileupload", "fileType": "fs",
                                         "fileName": remote_name, "fileLen": len(contents),
                                         "batchMsgSize": block_size, "batchAckSize": batch_ack},
                                        separators=(",", ":")))
        if start.get("rslt") != "ok":
            raise RuntimeError(f"ufStart: {start}")
        block_size = start.get("batchMsgSize", block_size)
        batch_ack = start.get("batchAckSize", batch_ack)
        stream_id = start.get("streamID", 0)
        acked = 0
        retries = 0
        with self._lock:
            self._okto = None
        while acked < len(contents):
            # The first batch is one block (raftjs: the device may do a long task on the first block)
            batch = 1 if acked == 0 else batch_ack
            pos = acked
            for _ in range(batch):
                if pos >= len(contents):
                    break
                self._send_block(stream_id, pos, contents[pos:pos + block_size])
                pos += block_size
            end = time.time() + 5
            while time.time() < end:
                with self._lock:
                    okto = self._okto
                if okto is not None and okto >= min(pos, len(contents)):
                    break
                time.sleep(0.01)
            with self._lock:
                okto = self._okto
            if okto is not None and okto > acked:
                acked = okto
                retries = 0
            else:
                retries += 1
                if retries > 5:
                    raise TimeoutError(f"no progress at {acked} of {len(contents)}")
        end_resp = self.ricrest(json.dumps({"cmdName": "ufEnd", "reqStr": "fileupload", "fileType": "fs",
                                            "fileName": remote_name, "fileLen": len(contents),
                                            **({"streamID": stream_id} if stream_id else {})},
                                           separators=(",", ":")))
        return start, end_resp

    def cmd(self, rest, timeout_s=5.0):
        """Send a REST command as a console line; return the first JSON reply line for it"""
        with self._lock:
            self._text_lines.clear()
        self._ser.write(rest.encode() + b"\n")
        end = time.time() + timeout_s
        while time.time() < end:
            with self._lock:
                for line in self._text_lines:
                    if line.startswith('{"req":"' + rest.split("?")[0]):
                        return json.loads(line)
            time.sleep(0.02)
        raise TimeoutError(f"no reply to {rest}")

    def take_text(self):
        with self._lock:
            lines, self._text_lines = self._text_lines, []
        return lines


def main():
    port, action = sys.argv[1], sys.argv[2]
    dev = RaftSerial(port)
    try:
        if action == "cmd":
            # "@file" reads the command from a file (avoids shell quoting of & in URLs)
            rest = open(sys.argv[3][1:]).read().strip() if sys.argv[3].startswith("@") else sys.argv[3]
            print(json.dumps(dev.cmd(rest)))
        elif action == "put":
            contents = open(sys.argv[3], "rb").read()
            t0 = time.time()
            start, end = dev.put(contents, sys.argv[4])
            print(f"uploaded {len(contents)} bytes in {time.time() - t0:.1f} s; ufStart {json.dumps(start)}; "
                  f"ufEnd {json.dumps(end)}")
        elif action == "cmdlog":
            # Send a command (as cmd, "@file" allowed), then print the console for the given seconds
            rest = open(sys.argv[3][1:]).read().strip() if sys.argv[3].startswith("@") else sys.argv[3]
            dev._ser.write(rest.encode() + b"\n")
            end = time.time() + float(sys.argv[4])
            while time.time() < end:
                for line in dev.take_text():
                    print(line)
                time.sleep(0.1)
        elif action == "log":
            end = time.time() + float(sys.argv[3])
            while time.time() < end:
                for line in dev.take_text():
                    print(line)
                time.sleep(0.1)
    finally:
        dev.close()


if __name__ == "__main__":
    main()
