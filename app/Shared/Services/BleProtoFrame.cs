/*
 * QuailTracker - GPS-synchronized Autonomous Recording Unit
 * Copyright (C) 2026 QuailTracker Project
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

using System;
using System.Buffers.Binary;
using Google.Protobuf;

namespace QuailTracker.Shared.Services;

/// <summary>
/// Topic IDs for the BLE protobuf protocol. Must match firmware ble_proto.h.
/// </summary>
public enum BleProtoTopic : byte
{
    // Device → App (push)
    Status          = 0x01,
    Detection       = 0x02,
    AudioLevel      = 0x03,
    RecordingState  = 0x04,
    GpsFix          = 0x05,
    Log             = 0x06,
    HealthReport    = 0x07,

    // App → Device (command)
    Command         = 0x10,
    SetConfig       = 0x11,
    Subscribe       = 0x12,
    Unsubscribe     = 0x13,

    // Device → App (response)
    CommandAck      = 0x20,
    ConfigDump      = 0x21,

    // Bidirectional
    Ping            = 0x30,
    Pong            = 0x31,

    // OTA
    OtaBegin        = 0x40,
    OtaData         = 0x41,
    OtaEnd          = 0x42,
    OtaCommit       = 0x43,
    OtaAbort        = 0x44,
    OtaStatus       = 0x45,
    OtaRollback     = 0x46,
}

/// <summary>
/// Builds and parses COBS-framed protobuf messages for BLE transport.
///
/// Wire format: [COBS-encoded(4-byte header + protobuf payload)] [0x00]
/// Header: [topic:1] [flags:1] [seq_lo:1] [seq_hi:1]
/// </summary>
public class BleProtoFrame
{
    private const int HeaderSize = 4;
    private ushort _txSeq;

    /// <summary>
    /// Encode a protobuf message into a COBS-framed byte array ready to transmit.
    /// Includes the trailing 0x00 delimiter.
    /// </summary>
    public byte[] Encode(BleProtoTopic topic, IMessage message)
    {
        var pbBytes = message.ToByteArray();
        return EncodeRaw(topic, pbBytes);
    }

    /// <summary>
    /// Encode raw payload bytes into a COBS-framed byte array.
    /// </summary>
    public byte[] EncodeRaw(BleProtoTopic topic, ReadOnlySpan<byte> payload)
    {
        // Build raw frame: header + payload
        int rawLen = HeaderSize + payload.Length;
        // Wire format: [0x01 SOF] [LEN_HI] [LEN_LO] [header + payload]
        var frame = new byte[3 + rawLen];
        frame[0] = 0x01; // start-of-frame marker
        frame[1] = (byte)((rawLen >> 8) & 0xFF);
        frame[2] = (byte)(rawLen & 0xFF);
        frame[3] = (byte)topic;
        frame[4] = 0; // flags
        BinaryPrimitives.WriteUInt16LittleEndian(frame.AsSpan(5), _txSeq++);
        payload.CopyTo(frame.AsSpan(3 + HeaderSize));
        return frame;
    }

    /// <summary>
    /// Encode a simple topic-only frame (no payload), e.g. Ping.
    /// </summary>
    public byte[] EncodeEmpty(BleProtoTopic topic)
    {
        return EncodeRaw(topic, ReadOnlySpan<byte>.Empty);
    }

    /// <summary>
    /// Decode a raw frame (header + protobuf payload) into topic + payload.
    /// Returns false if the frame is invalid.
    /// </summary>
    public static bool TryDecode(ReadOnlySpan<byte> rawFrame, out BleProtoTopic topic,
                                  out ushort sequence, out byte[] payload)
    {
        topic = 0;
        sequence = 0;
        payload = Array.Empty<byte>();

        if (rawFrame.Length < HeaderSize)
            return false;

        topic = (BleProtoTopic)rawFrame[0];
        sequence = BinaryPrimitives.ReadUInt16LittleEndian(rawFrame.Slice(2));
        payload = new byte[rawFrame.Length - HeaderSize];
        rawFrame.Slice(HeaderSize).CopyTo(payload);
        return true;
    }
}

/// <summary>
/// Accumulates BLE characteristic data and extracts complete COBS frames
/// delimited by 0x00 bytes.
/// </summary>
public class BleFrameAssembler
{
    private readonly byte[] _buffer = new byte[512];
    private int _pos;
    private int _expected;  // expected frame length (-1 = waiting for SOF)
    private byte _lenHi;
    private int _state;     // 0=idle, 1=got SOF, 2=got len_hi, 3=receiving

    /// <summary>
    /// Feed received BLE data. Looks for [0x01 SOF] [LEN_HI] [LEN_LO] [data...] frames.
    /// Calls onFrame with the raw frame data (header + protobuf payload).
    /// </summary>
    public void Feed(ReadOnlySpan<byte> data, Action<ReadOnlySpan<byte>> onFrame)
    {
        foreach (var b in data)
        {
            switch (_state)
            {
                case 0: // Idle — looking for SOF
                    if (b == 0x01)
                        _state = 1;
                    // else: ignore (text from PB-03F module)
                    break;

                case 1: // Got SOF — next byte is length high
                    _lenHi = b;
                    _state = 2;
                    break;

                case 2: // Got length high — next byte is length low
                    _expected = (_lenHi << 8) | b;
                    if (_expected > _buffer.Length || _expected < 4)
                    {
                        _state = 0; // invalid length — resync
                    }
                    else
                    {
                        _pos = 0;
                        _state = 3;
                    }
                    break;

                case 3: // Receiving frame data
                    _buffer[_pos++] = b;
                    if (_pos >= _expected)
                    {
                        onFrame(_buffer.AsSpan(0, _pos));
                        _state = 0;
                    }
                    break;
            }
        }
    }

    /// <summary>
    /// Reset assembler state (call on disconnect).
    /// </summary>
    public void Reset()
    {
        _pos = 0;
        _state = 0;
        _expected = 0;
    }
}
